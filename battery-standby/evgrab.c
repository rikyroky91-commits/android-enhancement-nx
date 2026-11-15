// evgrab v2: cattura in esclusiva (EVIOCGRAB) i dispositivi di input il cui nome contiene
// uno dei testi indicati, finche' il processo resta vivo. Serve in standby: Android non
// riceve gli eventi dei Joy-Con, quindi un tasto premuto per sbaglio non annulla la
// sospensione. Gli eventi catturati vengono letti e scartati. Terminare il processo
// (SIGTERM) rilascia la cattura.
//
// v2 (2026-11-07):
//  - cattura un dispositivo SOLO quando nessun tasto e nessuna direzione (assi HAT)
//    risultano premuti. Se lo si cattura con un tasto giu', il "rilascio" finisce qui e
//    Android crede che il tasto resti premuto per sempre: al risveglio lo ripete
//    all'infinito (D-pad su "incastrato" -> System UI non risponde);
//  - ricontrolla ogni 2 s i dispositivi: cattura anche quelli creati durante lo
//    standby (joycond ricrea il controller) e chiude quelli spariti.
//
// Uso: evgrab "Joy-Con" ["Pro Controller" ...]

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define MAX_DEV 32
#define BITS_LONG (8 * sizeof(long))
#define NLONGS(x) (((x) + BITS_LONG - 1) / BITS_LONG)

static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }

struct dev {
    int fd;            // -1 = slot libero
    int grabbed;       // 0 = da catturare, 1 = catturato, 2 = gia' tenuto da altri: ignorato
    char path[64];
    char name[256];
};

static struct dev devs[MAX_DEV];

static int test_bit(const unsigned long *b, int i) { return (b[i / BITS_LONG] >> (i % BITS_LONG)) & 1; }

// Nessun tasto premuto e assi HAT (croce direzionale) al centro?
static int idle(int fd) {
    unsigned long keys[NLONGS(KEY_CNT)];
    memset(keys, 0, sizeof keys);
    if (ioctl(fd, EVIOCGKEY(sizeof keys), keys) < 0) return 0;
    for (size_t i = 0; i < NLONGS(KEY_CNT); i++)
        if (keys[i]) return 0;
    unsigned long absbits[NLONGS(ABS_CNT)];
    memset(absbits, 0, sizeof absbits);
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absbits), absbits) >= 0) {
        for (int a = ABS_HAT0X; a <= ABS_HAT3Y; a++) {
            if (!test_bit(absbits, a)) continue;
            struct input_absinfo ai;
            if (ioctl(fd, EVIOCGABS(a), &ai) == 0 && ai.value != 0) return 0;
        }
    }
    return 1;
}

static int known(const char *path) {
    for (int i = 0; i < MAX_DEV; i++)
        if (devs[i].fd >= 0 && !strcmp(devs[i].path, path)) return 1;
    return 0;
}

// Apre i dispositivi nuovi che corrispondono ai nomi (non ancora catturati).
static void scan(int argc, char **argv) {
    DIR *d = opendir("/dev/input");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[64];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        if (known(path)) continue;
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[256] = "";
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        int match = 0;
        for (int i = 1; i < argc; i++) match |= strstr(name, argv[i]) != NULL;
        // Gli IMU non generano risvegli (Android li esclude): non serve catturarli.
        int slot = -1;
        for (int i = 0; i < MAX_DEV && slot < 0; i++)
            if (devs[i].fd < 0) slot = i;
        if (!match || strstr(name, "IMU") || slot < 0) { close(fd); continue; }
        devs[slot].fd = fd;
        devs[slot].grabbed = 0;
        snprintf(devs[slot].path, sizeof devs[slot].path, "%s", path);
        snprintf(devs[slot].name, sizeof devs[slot].name, "%s", name);
        fprintf(stderr, "trovato: %s (%s)\n", name, path);
    }
    closedir(d);
}

static void drop(struct dev *v) {
    if (v->grabbed == 1) ioctl(v->fd, EVIOCGRAB, 0);
    close(v->fd);
    v->fd = -1;
    v->grabbed = 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "uso: %s testo [testo...]\n", argv[0]); return 2; }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    for (int i = 0; i < MAX_DEV; i++) devs[i].fd = -1;

    struct input_event buf[64];
    time_t next_scan = 0;
    while (running) {
        time_t now = time(NULL);
        if (now >= next_scan) { scan(argc, argv); next_scan = now + 2; }

        // Cattura i dispositivi ancora liberi appena sono a riposo.
        for (int i = 0; i < MAX_DEV; i++) {
            struct dev *v = &devs[i];
            if (v->fd < 0 || v->grabbed) continue;
            if (!idle(v->fd)) continue;   // tasto o direzione premuti: si riprova tra poco
            if (ioctl(v->fd, EVIOCGRAB, 1) == 0) {
                v->grabbed = 1;
                fprintf(stderr, "catturato: %s (%s)\n", v->name, v->path);
            } else {
                fprintf(stderr, "ignorato (gia' catturato da altri): %s (%s): %s\n", v->name, v->path, strerror(errno));
                v->grabbed = 2;   // resta aperto solo per accorgersi se sparisce
            }
        }

        // Legge e scarta gli eventi dei dispositivi catturati; chiude quelli spariti.
        struct pollfd fds[MAX_DEV];
        int idx[MAX_DEV], n = 0;
        for (int i = 0; i < MAX_DEV; i++)
            if (devs[i].fd >= 0) { fds[n].fd = devs[i].fd; fds[n].events = POLLIN; fds[n].revents = 0; idx[n++] = i; }
        // Con dispositivi non ancora catturati si ricontrolla spesso (200 ms).
        int waiting = 0;
        for (int i = 0; i < n; i++) waiting |= devs[idx[i]].grabbed == 0;
        int r = poll(fds, n, waiting ? 200 : 1000);
        if (r <= 0) continue;
        for (int i = 0; i < n; i++) {
            struct dev *v = &devs[idx[i]];
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) { drop(v); continue; }
            if (!(fds[i].revents & POLLIN)) continue;
            ssize_t k;
            while ((k = read(v->fd, buf, sizeof buf)) > 0) {}
            if (k < 0 && errno == ENODEV) drop(v);
        }
    }
    for (int i = 0; i < MAX_DEV; i++)
        if (devs[i].fd >= 0) drop(&devs[i]);
    return 0;
}

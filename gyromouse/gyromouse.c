// gyromouse: giroscopio -> mouse virtuale oppure -> stick destro, per Switchroot Android.
// Legge il giroscopio e, mentre ZL e' premuto (o sempre, con -m always), muove:
//  - modalita' MOUSE: un mouse virtuale (uinput "Gyro Mouse"). I Joy-Con non vengono
//    catturati: i tasti restano invariati;
//  - modalita' STICK: lo stick destro del controller. Solo nelle app abilitate, il
//    controller "Nintendo Switch Combined Joy-Cons" di joycond viene catturato in esclusiva
//    (EVIOCGRAB) e sostituito da un clone uinput con lo stesso nome, vendor/product e
//    capacita' (stesso keylayout Android). Tutti gli eventi passano dal clone; tenendo ZL
//    il giroscopio si somma allo stick destro (ABS_RX/RY). La vibrazione chiesta al clone
//    viene girata al controller vero (e quindi a joycond, con il filtro jcfix). Fuori
//    dalle app abilitate o con il gyro spento il clone non esiste e nulla e' catturato.
//
// Uso: gyromouse [-o mouse|stick] [-s sens] [-S sens_stick] [-z antizona] [-m zl|always]
//                [-X] [-Y] [-d deadzone] [-c file] [-a app,app] [-v]
//   -o  uscita: mouse (default) o stick
//   -a  pacchetti in cui il gyro e' attivo, separati da virgola
//       (default com.nvidia.geforcenow,com.limelight); fuori da queste app non fa nulla
//   -s  mouse: pixel per grado di rotazione (default 12, o quella salvata)
//   -S  stick: sensibilita' (default 1.0 = stick al massimo a 100 gradi/s)
//   -z  stick: deflessione minima appena il gyro si muove, per superare la zona morta
//       dei giochi (0..0.5, default 0.15)
//   -m  zl = attivo solo tenendo ZL (default), always = sempre attivo
//   -X  inverte l'asse orizzontale, -Y inverte quello verticale
//   -d  zona morta in gradi/s (default 0.6)
//   -c  file dove salvare le impostazioni (default /data/adb/gyromouse/gyromouse.conf)
//   -H/-V  indice dell'asse (0=x 1=y 2=z) per destra/sinistra e su/giu'
//   -P  modalita' prova: stampa i picchi dei tre assi ogni 0,5 s (per calibrare), niente uscita
//   -v  stampa diagnostica
//
// File di configurazione (una riga): sens sempre acceso stick sens_stick antizona
//   (riletto da solo entro 1 s se cambia, es. dai riquadri del centro notifiche)
//   es. "12.000 0 1 1 1.000 0.150". Le versioni precedenti leggono solo i primi tre valori.
//
// Sorgente del giroscopio: quello interno della console (IIO) se presente, altrimenti
// l'IMU del Joy-Con (esposto solo con Joy-Con via Bluetooth: sui binari il driver
// joycon_serdev non crea il dispositivo IMU).
//
// Comandi dal controller (tenendo premuto Meno "-" oppure Piu' "+", solo nelle app abilitate):
//   Su / Giu'   sensibilita' +25% / -25% (quella della modalita' attiva)
//   Destra      alterna modalita' ZL / sempre attivo
//   Sinistra    ciclo: MOUSE -> STICK DESTRO -> SPENTO -> MOUSE
// Il processo "gyromouse-notif" e' il supporto per le notifiche (vedi notify).
//
// Standby: il clone ha lo stesso nome del controller ("...Joy-Con..."), quindi evgrab lo
// cattura a schermo spento come gli altri. Finche' evgrab e' in esecuzione il programma
// non crea ne' distrugge il clone, cosi' nessun controller resta scoperto.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Indici degli assi (0=x, 1=y, 2=z) per destra/sinistra (yaw) e su/giu' (pitch).
#define JC_YAW_AXIS    0    // IMU Joy-Con via Bluetooth (verificato in GeForce NOW)
#define JC_PITCH_AXIS  1
#define IIO_YAW_AXIS   0    // giroscopio interno, calibrato il 2026-11-05 (assi scambiati, versi giusti)
#define IIO_PITCH_AXIS 1

// Giroscopio interno della Switch OLED (icm42607p), letto via sysfs a 100 Hz.
#define IIO_GYRO   "/sys/bus/iio/devices/iio:device0"
#define IIO_PERIOD 0.01

#define VPAD_PHYS   "gyromouse/vpad"   // marca il clone: non lo confondiamo con l'originale
#define MAX_FX      96                 // id effetto < FF_GAIN (0x60); oltre: guadagno/autocentro
#define STICK_SMOOTH 0.5               // filtro sul contributo gyro (0..1, 1 = nessun filtro)
#define STICK_RAMP_DPS 2.0             // la deflessione minima entra gradualmente nei primi 2 gradi/s
#define APP_GRACE_S 2.0                // uscita dall'app: attesa prima di eliminare il clone

#define BITS_LONG (8 * sizeof(long))
#define NLONGS(x) (((x) + BITS_LONG - 1) / BITS_LONG)

static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int test_bit(const unsigned long *b, int i) { return (b[i / BITS_LONG] >> (i % BITS_LONG)) & 1; }

static void emit(int fd, int type, int code, int val) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = val;
    if (write(fd, &ev, sizeof ev) < 0) { /* best effort */ }
}

// Apre il primo /dev/input/event* il cui nome contiene `needle` (escluso il nostro clone).
static int open_by_name(const char *needle, char *out_name, size_t out_len) {
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *e;
    int fd = -1;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[64];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int f = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (f < 0) continue;
        char name[256] = "", phys[64] = "";
        ioctl(f, EVIOCGNAME(sizeof name), name);
        ioctl(f, EVIOCGPHYS(sizeof phys), phys);
        if (strstr(name, needle) && strcmp(phys, VPAD_PHYS)) {
            if (out_name) snprintf(out_name, out_len, "%s (%s)", name, path);
            fd = f;
            break;
        }
        close(f);
    }
    closedir(d);
    return fd;
}

static int create_mouse(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);   // necessario perche' Android lo classifichi come mouse
    ioctl(fd, UI_SET_EVBIT, EV_REL);
    ioctl(fd, UI_SET_RELBIT, REL_X);
    ioctl(fd, UI_SET_RELBIT, REL_Y);
    struct uinput_user_dev u;
    memset(&u, 0, sizeof u);
    snprintf(u.name, UINPUT_MAX_NAME_SIZE, "Gyro Mouse");
    u.id.bustype = BUS_VIRTUAL;
    u.id.vendor = 0x1d6b;
    u.id.product = 0x0a11;
    u.id.version = 1;
    if (write(fd, &u, sizeof u) != sizeof u || ioctl(fd, UI_DEV_CREATE) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// Vero se uno dei processi in primo piano (cpuset top-app) appartiene a un pacchetto
// dell'elenco `apps` (separato da virgole). I sottoprocessi "pkg:nome" contano come pkg.
static int foreground_allowed(const char *apps) {
    FILE *f = fopen("/dev/cpuset/top-app/cgroup.procs", "r");
    if (!f) return 1;                    // senza informazione non blocchiamo nulla
    int pid, found = 0;
    while (!found && fscanf(f, "%d", &pid) == 1) {
        char path[48], cmd[256] = "";
        snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
        FILE *c = fopen(path, "r");
        if (!c) continue;
        size_t n = fread(cmd, 1, sizeof cmd - 1, c);
        fclose(c);
        cmd[n] = 0;
        char *colon = strchr(cmd, ':');
        if (colon) *colon = 0;
        size_t len = strlen(cmd);
        for (const char *p = apps; len && *p; ) {
            const char *end = strchr(p, ',');
            size_t l = end ? (size_t)(end - p) : strlen(p);
            if (l == len && !strncmp(p, cmd, l)) { found = 1; break; }
            if (!end) break;
            p = end + 1;
        }
    }
    fclose(f);
    return found;
}

// Vero se l'helper di standby (evgrab) e' in esecuzione, cioe' lo schermo e' spento.
static int standby_helper_running(void) {
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        char path[64], comm[32] = "";
        snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ssize_t n = read(fd, comm, sizeof comm - 1);
        close(fd);
        if (n <= 0) continue;
        comm[n] = 0;
        if (!strcmp(comm, "evgrab\n") || !strcmp(comm, "evgrab")) found = 1;
    }
    closedir(d);
    return found;
}

// Notifiche Android, postate come utente shell (uid 2000).
// ATTENZIONE: non si puo' fare fork() dal processo principale dopo aver aperto il
// controller. Ogni processo che chiude una copia del descrittore evdev (anche un figlio
// all'uscita o all'exec) provoca il "flush" del kernel, che CANCELLA tutti gli effetti di
// vibrazione caricati da quel descrittore: il gioco smette di vibrare. Per questo le
// notifiche passano da un processo di supporto creato all'avvio, prima di aprire qualsiasi
// dispositivo, che riceve i testi da una pipe.
static int notify_fd = -1;

static void notifier_start(void) {
    int p[2];
    if (pipe2(p, O_CLOEXEC) < 0) return;
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return; }
    if (pid == 0) {
        close(p[1]);
        prctl(PR_SET_NAME, "gyromouse-notif");
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        FILE *in = fdopen(p[0], "r");
        char line[256];
        while (in && fgets(line, sizeof line, in)) {   // EOF quando il principale termina
            line[strcspn(line, "\n")] = 0;
            pid_t c = fork();
            if (c == 0) {
                if (setgid(2000) || setuid(2000)) _exit(1);
                int null = open("/dev/null", O_RDWR);
                if (null >= 0) { dup2(null, 1); dup2(null, 2); }
                execl("/system/bin/cmd", "cmd", "notification", "post", "-S", "bigtext",
                      "-t", "Gyro", "gyromouse", line, (char *)NULL);
                _exit(1);
            }
            if (c > 0) waitpid(c, NULL, 0);
        }
        _exit(0);
    }
    close(p[0]);
    fcntl(p[1], F_SETFL, O_NONBLOCK);    // se il supporto si blocca, il principale non aspetta
    notify_fd = p[1];
}

static void notify(const char *text) {
    if (notify_fd < 0) return;
    char buf[200];
    int n = snprintf(buf, sizeof buf, "%s\n", text);
    if (n > (int)sizeof buf - 1) n = sizeof buf - 1;
    if (write(notify_fd, buf, n) < 0) { /* best effort */ }
}

// Breve impulso di vibrazione sul controller come conferma.
// *id conserva l'effetto caricato (-1 se nessuno) cosi' da riutilizzarlo.
static void pulse(int pad_rw, int *id, int ms) {
    if (pad_rw < 0) return;
    struct ff_effect fx;
    memset(&fx, 0, sizeof fx);
    fx.type = FF_RUMBLE;
    fx.id = *id;
    fx.u.rumble.strong_magnitude = 0x9000;
    fx.u.rumble.weak_magnitude = 0x9000;
    fx.replay.length = ms;
    if (ioctl(pad_rw, EVIOCSFF, &fx) < 0) {
        if (fx.id < 0) return;
        fx.id = -1;                      // effetto non piu' valido: se ne carica uno nuovo
        if (ioctl(pad_rw, EVIOCSFF, &fx) < 0) return;
    }
    *id = fx.id;
    emit(pad_rw, EV_FF, fx.id, 1);
}

// ---------------------------------------------------------------------------------------
// Impostazioni

static double sens = 12.0;        // mouse: pixel per grado
static double ssens = 1.0;        // stick: 1.0 = deflessione piena a 100 gradi/s
static double adz = 0.15;         // stick: deflessione minima (anti zona morta)
static int always = 0, enabled = 1, stick = 0;

static void save_conf(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%.3f %d %d %d %.3f %.3f\n", sens, always, enabled, stick, ssens, adz);
    fclose(f);
}

// Vero se il file e' cambiato (data di modifica o inode) dall'ultima chiamata.
static int conf_changed(const char *path) {
    static struct timespec mt;
    static ino_t ino;
    struct stat st;
    if (stat(path, &st) < 0) return 0;
    if (st.st_mtim.tv_sec == mt.tv_sec && st.st_mtim.tv_nsec == mt.tv_nsec && st.st_ino == ino) return 0;
    mt = st.st_mtim;
    ino = st.st_ino;
    return 1;
}

static void load_conf(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    double s; int a, e;
    if (fscanf(f, "%lf %d %d", &s, &a, &e) == 3 && s > 0) {
        sens = s; always = a; enabled = e;
        int st; double ss, ad;
        int n = fscanf(f, "%d %lf %lf", &st, &ss, &ad);
        if (n >= 1) stick = st != 0;
        if (n >= 2 && ss > 0) ssens = ss;
        if (n >= 3 && ad >= 0 && ad <= 0.5) adz = ad;
    }
    fclose(f);
}

// ---------------------------------------------------------------------------------------
// Giroscopio interno (IIO)

struct iio_gyro {
    int fd[3];
    double scale_dps;                  // gradi/s per unita' grezza
};

static int iio_open(struct iio_gyro *g, const char *dir) {
    char path[160];
    snprintf(path, sizeof path, "%s/in_anglvel_scale", dir);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    double scale = 0;
    int ok = fscanf(f, "%lf", &scale) == 1 && scale > 0;
    fclose(f);
    if (!ok) return 0;
    g->scale_dps = scale * 57.29577951;        // rad/s -> gradi/s
    static const char axis[3] = {'x', 'y', 'z'};
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/in_anglvel_%c_raw", dir, axis[i]);
        g->fd[i] = open(path, O_RDONLY | O_CLOEXEC);
        if (g->fd[i] < 0) {
            while (i--) close(g->fd[i]);
            return 0;
        }
    }
    return 1;
}

static int iio_read(struct iio_gyro *g, double out[3]) {
    for (int i = 0; i < 3; i++) {
        char buf[32];
        ssize_t n = pread(g->fd[i], buf, sizeof buf - 1, 0);
        if (n <= 0) return -1;              // es. EBUSY se il sensore e' in modalita' buffer
        buf[n] = 0;
        out[i] = atoi(buf) * g->scale_dps;
    }
    return 0;
}

static void iio_close(struct iio_gyro *g) {
    for (int i = 0; i < 3; i++) close(g->fd[i]);
}

// ---------------------------------------------------------------------------------------
// Calcolo del movimento, comune alle due sorgenti (Joy-Con e giroscopio interno).

struct motion {
    double gyro[3];                    // ultima lettura in gradi/s
    double bias[3], bias_acc[3];
    int bias_ready, bias_n;
    double still_since;
    double fx, fy;                     // frazioni di pixel accumulate
    double sx, sy;                     // ultima velocita' (gradi/s, + = destra / giu')
    int yaw_i, pitch_i;
    double dead;
    int inv_x, inv_y, verbose, print_mode, mouse;
    double peak[3], next_print;        // modalita' prova (-P)
};

// Ritorna 1 se ha prodotto un campione valido (attivo e bias pronto) in m->sx/sy.
// Con to_mouse muove anche il mouse virtuale.
static int motion_step(struct motion *m, double dt, int active, int to_mouse) {
    double *g = m->gyro;
    double mag = fabs(g[0]) + fabs(g[1]) + fabs(g[2]);

    // Bias: media iniziale da fermo, poi aggiornamento lento quando la console e' ferma.
    if (!m->bias_ready) {
        if (mag < 6.0) {
            for (int k = 0; k < 3; k++) m->bias_acc[k] += g[k];
            if (++m->bias_n >= 200) {
                for (int k = 0; k < 3; k++) m->bias[k] = m->bias_acc[k] / m->bias_n;
                m->bias_ready = 1;
                fprintf(stderr, "bias: %.2f %.2f %.2f dps\n", m->bias[0], m->bias[1], m->bias[2]);
            }
        } else { m->bias_n = 0; memset(m->bias_acc, 0, sizeof m->bias_acc); }
        return 0;
    }
    double c[3];
    for (int k = 0; k < 3; k++) c[k] = g[k] - m->bias[k];
    if (fabs(c[0]) + fabs(c[1]) + fabs(c[2]) < 3.0) {
        if (m->still_since == 0) m->still_since = now_s();
        if (!active && now_s() - m->still_since > 1.0)
            for (int k = 0; k < 3; k++) m->bias[k] += (g[k] - m->bias[k]) * 0.002;
    } else m->still_since = 0;

    if (m->print_mode) {
        for (int k = 0; k < 3; k++) if (fabs(c[k]) > fabs(m->peak[k])) m->peak[k] = c[k];
        if (now_s() >= m->next_print) {
            printf("x=%7.1f y=%7.1f z=%7.1f dps\n", m->peak[0], m->peak[1], m->peak[2]);
            fflush(stdout);
            memset(m->peak, 0, sizeof m->peak);
            m->next_print = now_s() + 0.5;
        }
        return 0;
    }

    if (!active) { m->fx = m->fy = 0; return 0; }

    double yaw = c[m->yaw_i], pitch = c[m->pitch_i];
    m->sx = m->inv_x ? yaw : -yaw;
    m->sy = m->inv_y ? -pitch : pitch;
    if (!to_mouse) return 1;

    if (fabs(yaw) < m->dead) yaw = 0;
    if (fabs(pitch) < m->dead) pitch = 0;

    m->fx += (m->inv_x ? yaw : -yaw) * sens * dt;
    m->fy += (m->inv_y ? -pitch : pitch) * sens * dt;
    int dx = (int)m->fx, dy = (int)m->fy;
    m->fx -= dx; m->fy -= dy;
    if (dx || dy) {
        if (dx) emit(m->mouse, EV_REL, REL_X, dx);
        if (dy) emit(m->mouse, EV_REL, REL_Y, dy);
        emit(m->mouse, EV_SYN, SYN_REPORT, 0);
        if (m->verbose) fprintf(stderr, "yaw=%.1f pitch=%.1f -> %d,%d\n", yaw, pitch, dx, dy);
    }
    return 1;
}

// Velocita' angolare (gradi/s) -> deflessione dello stick (-1..1 per asse).
// Zona morta radiale, deflessione minima `adz` (entra gradualmente nei primi
// STICK_RAMP_DPS gradi/s per non tremolare da fermi), poi lineare fino a 100/ssens gradi/s.
static void gyro_to_stick(double sx, double sy, double dead, double *gx, double *gy) {
    double mag = hypot(sx, sy);
    *gx = *gy = 0;
    if (mag <= dead) return;
    double e = mag - dead;
    double ramp = e / STICK_RAMP_DPS;
    if (ramp > 1) ramp = 1;
    double out = adz * ramp + (1 - adz) * (e * ssens / 100.0);
    if (out > 1) out = 1;
    *gx = sx / mag * out;
    *gy = sy / mag * out;
}

// ---------------------------------------------------------------------------------------
// Clone uinput del controller (modalita' stick)

struct vpad {
    int ufd;                           // /dev/uinput del clone, -1 = non esiste
    int map[MAX_FX];                   // id effetto sul clone -> id sul controller vero
    unsigned long keybits[NLONGS(KEY_CNT)], absbits[NLONGS(ABS_CNT)];
    int has_stick;                     // il controller ha ABS_RX e ABS_RY
    struct input_absinfo ax[2];        // limiti di RX, RY
    int phys[2], out[2];               // valore fisico / valore inviato al clone
    double g[2];                       // contributo del gyro filtrato
    int gyro_on, dropped;
    double next_try, app_off_since;
};

static struct vpad vp = { .ufd = -1 };

// Nessun tasto premuto, croce e stick quasi al centro, grilletti a riposo?
// Si cattura il controller solo cosi': Android conserva lo stato che aveva al momento
// della cattura e non deve restare con un tasto "premuto" o uno stick inclinato.
static int pad_idle(int fd) {
    unsigned long keys[NLONGS(KEY_CNT)];
    memset(keys, 0, sizeof keys);
    if (ioctl(fd, EVIOCGKEY(sizeof keys), keys) < 0) return 0;
    for (size_t i = 0; i < NLONGS(KEY_CNT); i++)
        if (keys[i]) return 0;
    unsigned long absb[NLONGS(ABS_CNT)];
    memset(absb, 0, sizeof absb);
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absb), absb) < 0) return 0;
    for (int a = 0; a < ABS_CNT; a++) {
        if (!test_bit(absb, a)) continue;
        struct input_absinfo ai;
        if (ioctl(fd, EVIOCGABS(a), &ai) < 0) return 0;
        long range = (long)ai.maximum - ai.minimum;
        if (range <= 0) continue;
        if (ai.minimum < 0) {                       // asse centrato (stick, croce)
            long c = ((long)ai.maximum + ai.minimum) / 2;
            if (labs(ai.value - c) * 5 > range / 2) return 0;   // oltre il 20%
        } else if (ai.value > ai.minimum + range / 4) return 0; // grilletto premuto
    }
    return 1;
}

static void vpad_stick(int syn) {
    if (vp.ufd < 0 || !vp.has_stick) return;
    int changed = 0;
    for (int k = 0; k < 2; k++) {
        int iv = vp.phys[k];
        if (vp.gyro_on) {
            struct input_absinfo *a = &vp.ax[k];
            double c = ((double)a->maximum + a->minimum) / 2, h = ((double)a->maximum - a->minimum) / 2;
            double v = (vp.phys[k] - c) / h + vp.g[k];
            if (v > 1) v = 1;
            if (v < -1) v = -1;
            iv = (int)lround(c + v * h);
        }
        if (iv != vp.out[k]) {
            emit(vp.ufd, EV_ABS, k ? ABS_RY : ABS_RX, iv);
            vp.out[k] = iv;
            changed = 1;
        }
    }
    if (changed && syn) emit(vp.ufd, EV_SYN, SYN_REPORT, 0);
}

// Contributo del gyro: on=0 riporta subito lo stick al valore fisico.
static void vpad_gyro(int on, double gx, double gy) {
    if (vp.ufd < 0 || !vp.has_stick) return;
    if (!on) {
        if (!vp.gyro_on) return;
        vp.gyro_on = 0;
        vp.g[0] = vp.g[1] = 0;
        vpad_stick(1);
        return;
    }
    if (!vp.gyro_on) { vp.gyro_on = 1; vp.g[0] = vp.g[1] = 0; }
    vp.g[0] += (gx - vp.g[0]) * STICK_SMOOTH;
    vp.g[1] += (gy - vp.g[1]) * STICK_SMOOTH;
    vpad_stick(1);
}

// Dopo eventi persi (SYN_DROPPED): reinvia lo stato completo. Il kernel scarta i valori
// uguali a quelli gia' noti, quindi non nascono pressioni doppie.
static void vpad_resync(int pad) {
    unsigned long keys[NLONGS(KEY_CNT)];
    memset(keys, 0, sizeof keys);
    if (ioctl(pad, EVIOCGKEY(sizeof keys), keys) >= 0)
        for (int k = 0; k < KEY_CNT; k++)
            if (test_bit(vp.keybits, k)) emit(vp.ufd, EV_KEY, k, test_bit(keys, k));
    for (int a = 0; a < ABS_CNT; a++) {
        if (!test_bit(vp.absbits, a)) continue;
        struct input_absinfo ai;
        if (ioctl(pad, EVIOCGABS(a), &ai) < 0) continue;
        if (vp.has_stick && (a == ABS_RX || a == ABS_RY)) { vp.phys[a == ABS_RY] = ai.value; continue; }
        emit(vp.ufd, EV_ABS, a, ai.value);
    }
    vpad_stick(0);
}

// Crea il clone con le stesse capacita' del controller `pad`. Ritorna 0 se riuscito.
static int vpad_create(int pad) {
    int ufd = open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (ufd < 0) { perror("uinput (clone)"); return -1; }

    struct input_id id;
    char name[UINPUT_MAX_NAME_SIZE] = "";
    memset(&id, 0, sizeof id);
    ioctl(pad, EVIOCGID, &id);
    ioctl(pad, EVIOCGNAME(sizeof name), name);
    name[sizeof name - 1] = 0;
    int nff = 0;
    if (ioctl(pad, EVIOCGEFFECTS, &nff) < 0) nff = 0;

    unsigned long evb[NLONGS(EV_CNT)];
    memset(evb, 0, sizeof evb);
    ioctl(pad, EVIOCGBIT(0, sizeof evb), evb);

    static const struct { int type, cnt; unsigned long req; } caps[] = {
        {EV_KEY, KEY_CNT, UI_SET_KEYBIT}, {EV_REL, REL_CNT, UI_SET_RELBIT},
        {EV_ABS, ABS_CNT, UI_SET_ABSBIT}, {EV_MSC, MSC_CNT, UI_SET_MSCBIT},
        {EV_LED, LED_CNT, UI_SET_LEDBIT}, {EV_SW, SW_CNT, UI_SET_SWBIT},
        {EV_FF, FF_CNT, UI_SET_FFBIT},
    };
    memset(vp.keybits, 0, sizeof vp.keybits);
    memset(vp.absbits, 0, sizeof vp.absbits);
    for (size_t i = 0; i < sizeof caps / sizeof *caps; i++) {
        int t = caps[i].type;
        if (!test_bit(evb, t)) continue;
        if (t == EV_FF && nff <= 0) continue;          // uinput rifiuta FF senza effetti
        unsigned long bits[NLONGS(KEY_CNT)];
        memset(bits, 0, sizeof bits);
        if (ioctl(pad, EVIOCGBIT(t, sizeof bits), bits) < 0) continue;
        ioctl(ufd, UI_SET_EVBIT, t);
        for (int c = 0; c < caps[i].cnt; c++)
            if (test_bit(bits, c)) ioctl(ufd, caps[i].req, c);
        if (t == EV_KEY) memcpy(vp.keybits, bits, sizeof vp.keybits);
        if (t == EV_ABS) memcpy(vp.absbits, bits, sizeof vp.absbits);
    }
    if (test_bit(evb, EV_REP)) ioctl(ufd, UI_SET_EVBIT, EV_REP);
    unsigned long props[NLONGS(INPUT_PROP_CNT)];
    memset(props, 0, sizeof props);
    if (ioctl(pad, EVIOCGPROP(sizeof props), props) >= 0)
        for (int c = 0; c < INPUT_PROP_CNT; c++)
            if (test_bit(props, c)) ioctl(ufd, UI_SET_PROPBIT, c);
    ioctl(ufd, UI_SET_PHYS, VPAD_PHYS);

    vp.has_stick = test_bit(vp.absbits, ABS_RX) && test_bit(vp.absbits, ABS_RY);
    int ver = 0;
    if (ioctl(ufd, UI_GET_VERSION, &ver) < 0) ver = 0;
    int ok;
    if (ver >= 5) {
        ok = 1;
        for (int a = 0; a < ABS_CNT && ok; a++) {
            if (!test_bit(vp.absbits, a)) continue;
            struct uinput_abs_setup s;
            memset(&s, 0, sizeof s);
            s.code = a;
            if (ioctl(pad, EVIOCGABS(a), &s.absinfo) < 0) continue;
            if (a == ABS_RX || a == ABS_RY) {
                vp.ax[a == ABS_RY] = s.absinfo;
                s.absinfo.fuzz = 0;          // il filtro fuzz arrotonderebbe i piccoli movimenti del gyro
            }
            ok = ioctl(ufd, UI_ABS_SETUP, &s) == 0;
        }
        struct uinput_setup us;
        memset(&us, 0, sizeof us);
        us.id = id;
        snprintf(us.name, sizeof us.name, "%s", name);
        us.ff_effects_max = nff > 0 ? nff : 0;
        ok = ok && ioctl(ufd, UI_DEV_SETUP, &us) == 0;
    } else {
        struct uinput_user_dev u;
        memset(&u, 0, sizeof u);
        u.id = id;
        snprintf(u.name, sizeof u.name, "%s", name);
        u.ff_effects_max = nff > 0 ? nff : 0;
        for (int a = 0; a < ABS_CNT; a++) {
            if (!test_bit(vp.absbits, a)) continue;
            struct input_absinfo ai;
            if (ioctl(pad, EVIOCGABS(a), &ai) < 0) continue;
            if (a == ABS_RX || a == ABS_RY) { vp.ax[a == ABS_RY] = ai; ai.fuzz = 0; }
            u.absmin[a] = ai.minimum; u.absmax[a] = ai.maximum;
            u.absfuzz[a] = ai.fuzz; u.absflat[a] = ai.flat;
        }
        ok = write(ufd, &u, sizeof u) == sizeof u;
    }
    if (!ok || ioctl(ufd, UI_DEV_CREATE) < 0) {
        perror("creazione clone");
        close(ufd);
        return -1;
    }
    vp.ufd = ufd;
    for (int i = 0; i < MAX_FX; i++) vp.map[i] = -1;
    vp.gyro_on = vp.dropped = 0;
    vp.g[0] = vp.g[1] = 0;
    // Valori iniziali: quelli attuali del controller vero (con API legacy partono da 0).
    for (int k = 0; k < 2; k++) { vp.phys[k] = vp.ax[k].value; vp.out[k] = ver >= 5 ? vp.ax[k].value : 0; }
    if (ver < 5) { vpad_resync(pad); emit(ufd, EV_SYN, SYN_REPORT, 0); }
    fprintf(stderr, "clone creato: \"%s\" %04x:%04x bus %04x, %d effetti, uinput v%d\n",
            name, id.vendor, id.product, id.bustype, nff, ver);
    return 0;
}

// Elimina il clone; ferma e rimuove gli effetti girati al controller; rilascia la cattura.
static void vpad_destroy(int pad, int pad_rw) {
    if (vp.ufd < 0) return;
    for (int i = 0; i < MAX_FX; i++) {
        if (vp.map[i] < 0) continue;
        if (pad_rw >= 0) {
            emit(pad_rw, EV_FF, vp.map[i], 0);
            ioctl(pad_rw, EVIOCRMFF, (long)vp.map[i]);
        }
        vp.map[i] = -1;
    }
    ioctl(vp.ufd, UI_DEV_DESTROY);
    close(vp.ufd);
    vp.ufd = -1;
    vp.gyro_on = 0;
    if (pad >= 0) ioctl(pad, EVIOCGRAB, 0);
    fprintf(stderr, "clone eliminato, controller rilasciato\n");
}

// Richieste di Android al clone: vibrazione (upload/cancellazione/play) e LED.
static void vpad_requests(int pad_rw, int verbose) {
    struct input_event ev[32];
    ssize_t r;
    while (vp.ufd >= 0 && (r = read(vp.ufd, ev, sizeof ev)) > 0) {
        for (int i = 0; i < (int)(r / sizeof *ev); i++) {
            struct input_event *e = &ev[i];
            if (e->type == EV_UINPUT && e->code == UI_FF_UPLOAD) {
                struct uinput_ff_upload up;
                memset(&up, 0, sizeof up);
                up.request_id = e->value;
                if (ioctl(vp.ufd, UI_BEGIN_FF_UPLOAD, &up) < 0) continue;
                struct ff_effect fx = up.effect;
                int vid = fx.id;
                up.retval = -EINVAL;
                if (vid >= 0 && vid < MAX_FX && pad_rw >= 0) {
                    fx.id = vp.map[vid];                 // -1 = nuovo effetto
                    int ok = ioctl(pad_rw, EVIOCSFF, &fx) == 0;
                    if (!ok && vp.map[vid] >= 0) {       // effetto sparito sul controller: ricarica
                        fx.id = -1;
                        ok = ioctl(pad_rw, EVIOCSFF, &fx) == 0;
                    }
                    if (ok) { vp.map[vid] = fx.id; up.retval = 0; }
                    else { up.retval = -errno; vp.map[vid] = -1; }
                }
                ioctl(vp.ufd, UI_END_FF_UPLOAD, &up);
                if (verbose) fprintf(stderr, "ff upload %d -> %d (%d)\n", vid, vid >= 0 && vid < MAX_FX ? vp.map[vid] : -1, up.retval);
            } else if (e->type == EV_UINPUT && e->code == UI_FF_ERASE) {
                struct uinput_ff_erase er;
                memset(&er, 0, sizeof er);
                er.request_id = e->value;
                if (ioctl(vp.ufd, UI_BEGIN_FF_ERASE, &er) < 0) continue;
                int vid = er.effect_id;
                if (vid >= 0 && vid < MAX_FX && vp.map[vid] >= 0) {
                    if (pad_rw >= 0) ioctl(pad_rw, EVIOCRMFF, (long)vp.map[vid]);
                    vp.map[vid] = -1;
                }
                er.retval = 0;
                ioctl(vp.ufd, UI_END_FF_ERASE, &er);
            } else if (e->type == EV_FF && pad_rw >= 0) {
                int code = e->code;
                if (code < MAX_FX) {                     // play/stop di un effetto
                    if (vp.map[code] < 0) continue;
                    code = vp.map[code];
                }                                        // altrimenti FF_GAIN / FF_AUTOCENTER
                emit(pad_rw, EV_FF, code, e->value);
            } else if (e->type == EV_LED && pad_rw >= 0) {
                emit(pad_rw, EV_LED, e->code, e->value);
            }
        }
    }
}

// Inoltra un evento del controller vero al clone.
static void vpad_forward(int pad, const struct input_event *e) {
    if (vp.ufd < 0) return;
    switch (e->type) {
    case EV_SYN:
        if (e->code == SYN_DROPPED) { vp.dropped = 1; return; }
        if (vp.dropped) {
            if (e->code != SYN_REPORT) return;
            vp.dropped = 0;
            vpad_resync(pad);
        }
        emit(vp.ufd, EV_SYN, e->code, e->value);
        return;
    case EV_FF:              // eco delle vibrazioni: non va rimandato (farebbe un giro chiuso)
    case EV_LED:
        return;
    case EV_ABS:
        if (vp.has_stick && (e->code == ABS_RX || e->code == ABS_RY)) {
            vp.phys[e->code == ABS_RY] = e->value;
            if (!vp.dropped) vpad_stick(0);
            return;
        }
        break;
    }
    if (!vp.dropped) emit(vp.ufd, e->type, e->code, e->value);
}

// ---------------------------------------------------------------------------------------

int main(int argc, char **argv) {
    double dead = 0.6;
    int inv_x = 0, inv_y = 0, verbose = 0;
    const char *conf = "/data/adb/gyromouse/gyromouse.conf";
    const char *apps = "com.nvidia.geforcenow,com.limelight";
    double opt_sens = 0, opt_ssens = 0, opt_adz = -1;
    const char *opt_mode = NULL, *opt_out = NULL;
    int opt_h = -1, opt_v = -1, print_mode = 0;
    int opt;
    while ((opt = getopt(argc, argv, "o:s:S:z:m:XYd:c:a:H:V:Pv")) != -1) {
        switch (opt) {
        case 'a': apps = optarg; break;
        case 'H': opt_h = atoi(optarg) % 3; break;
        case 'V': opt_v = atoi(optarg) % 3; break;
        case 'P': print_mode = 1; break;
        case 'o': opt_out = optarg; break;
        case 's': opt_sens = atof(optarg); break;
        case 'S': opt_ssens = atof(optarg); break;
        case 'z': opt_adz = atof(optarg); break;
        case 'm': opt_mode = optarg; break;
        case 'X': inv_x = 1; break;
        case 'Y': inv_y = 1; break;
        case 'd': dead = atof(optarg); break;
        case 'c': conf = optarg; break;
        case 'v': verbose = 1; break;
        default:
            fprintf(stderr, "uso: %s [-o mouse|stick] [-s sens] [-S sens_stick] [-z antizona] [-m zl|always] [-X] [-Y] [-d deadzone] [-c file] [-a app,app] [-H asse] [-V asse] [-P] [-v]\n", argv[0]);
            return 2;
        }
    }
    load_conf(conf);   // le opzioni esplicite vincono sul file
    conf_changed(conf);                  // memorizza la data di modifica attuale
    if (opt_sens > 0) sens = opt_sens;
    if (opt_ssens > 0) ssens = opt_ssens;
    if (opt_adz >= 0 && opt_adz <= 0.5) adz = opt_adz;
    if (opt_mode) always = !strcmp(opt_mode, "always");
    if (opt_out) stick = !strcmp(opt_out, "stick");
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
    if (!print_mode) notifier_start();   // prima di aprire qualsiasi dispositivo (vedi notify)
    fprintf(stderr, "uscita: %s, sens mouse %.1f, sens stick %.2f, antizona %.2f, %s, gyro %s\n",
            stick ? "stick destro" : "mouse", sens, ssens, adz, always ? "sempre" : "ZL",
            enabled ? "acceso" : "spento");

    int mouse = print_mode ? -1 : create_mouse();
    if (!print_mode && mouse < 0) { perror("uinput"); return 1; }

    struct motion m;
    memset(&m, 0, sizeof m);
    m.mouse = mouse;
    m.dead = dead;
    m.inv_x = inv_x;
    m.inv_y = inv_y;
    m.verbose = verbose;
    m.print_mode = print_mode;

    // Giroscopio interno della console (IIO): usato se il Joy-Con non espone l'IMU.
    struct iio_gyro ig;
    int use_iio = iio_open(&ig, IIO_GYRO);
    if (use_iio) {
        m.yaw_i = opt_h >= 0 ? opt_h : IIO_YAW_AXIS;
        m.pitch_i = opt_v >= 0 ? opt_v : IIO_PITCH_AXIS;
        fprintf(stderr, "IMU: giroscopio interno %s (%.0f Hz)\n", IIO_GYRO, 1.0 / IIO_PERIOD);
    }
    double next_sample = 0, last_sample = 0;

    int imu = -1, pad = -1, pad_rw = -1, ff_id = -1;
    int minus = 0, plus = 0, hat_x = 0, hat_y = 0;
    double res = 14247.0;              // unita' per grado/s, aggiornata da EVIOCGABS
    int zl_btn = 0, zl_axis = 0;
    long long last_ts = -1;            // MSC_TIMESTAMP in microsecondi
    double next_scan = 0;
    int app_ok = 0;                    // app in primo piano tra quelle abilitate
    int standby = 0;                   // evgrab in esecuzione (schermo spento)
    int hold = 0;                      // controller tenuto catturato da noi durante lo standby
    double next_app_check = 0;

    while (running) {
        if (now_s() >= next_app_check) {
            int ok = foreground_allowed(apps);
            if (ok != app_ok && verbose) fprintf(stderr, "app abilitata in primo piano: %d\n", ok);
            if (app_ok && !ok) vp.app_off_since = now_s();
            app_ok = ok;
            standby = (stick || vp.ufd >= 0 || hold) ? standby_helper_running() : 0;
            if (conf_changed(conf)) {     // modificato da fuori (riquadri del centro notifiche)
                load_conf(conf);
                vp.next_try = 0;
                fprintf(stderr, "impostazioni ricaricate: %s, %s, sens %.1f / stick %.2f, %s\n",
                        enabled ? "acceso" : "spento", stick ? "stick" : "mouse", sens, ssens,
                        always ? "sempre" : "ZL");
            }
            next_app_check = now_s() + 1.0;
        }
        if ((imu < 0 && !use_iio) || pad < 0) {
            if (now_s() < next_scan) { usleep(200000); continue; }
            next_scan = now_s() + 2.0;
            char n1[300] = "", n2[300] = "";
            if (imu < 0 && !use_iio) {
                imu = open_by_name("Right Joy-Con IMU", n1, sizeof n1);
                if (imu < 0) imu = open_by_name("Joy-Con IMU", n1, sizeof n1);
                if (imu >= 0) {
                    struct input_absinfo ai;
                    if (!ioctl(imu, EVIOCGABS(ABS_RX), &ai) && ai.resolution > 0) res = ai.resolution;
                    m.yaw_i = opt_h >= 0 ? opt_h : JC_YAW_AXIS;
                    m.pitch_i = opt_v >= 0 ? opt_v : JC_PITCH_AXIS;
                    m.bias_ready = 0; m.bias_n = 0; memset(m.bias_acc, 0, sizeof m.bias_acc);
                    last_ts = -1;
                    fprintf(stderr, "IMU: %s, res=%.0f\n", n1, res);
                }
            }
            if (pad < 0) {
                pad = open_by_name("Combined Joy-Cons", n2, sizeof n2);
                if (pad < 0) pad = open_by_name("Pro Controller", n2, sizeof n2);
                if (pad >= 0) {
                    hold = 0;
                    // Secondo descrittore in scrittura per la vibrazione.
                    char path[64];
                    snprintf(path, sizeof path, "/proc/self/fd/%d", pad);
                    if (pad_rw >= 0) close(pad_rw);
                    pad_rw = open(path, O_RDWR | O_CLOEXEC);
                    ff_id = -1;
                    minus = plus = hat_x = hat_y = 0;
                    zl_btn = zl_axis = 0;
                    fprintf(stderr, "Pad: %s\n", n2);
                }
            }
            if ((imu < 0 && !use_iio) || pad < 0) continue;
        }

        // Fine dello standby: rilascia il controller tenuto per conto di evgrab.
        if (hold && !standby) {
            ioctl(pad, EVIOCGRAB, 0);
            hold = 0;
            fprintf(stderr, "standby finito: controller rilasciato\n");
        }

        // Modalita' stick: crea/elimina il clone. Mai durante lo standby (evgrab attivo),
        // cosi' il controller che Android vede resta sempre catturato da qualcuno.
        if (!print_mode && !standby && !hold) {
            int want = stick && enabled && app_ok;
            if (want && vp.ufd < 0 && now_s() >= vp.next_try) {
                vp.next_try = now_s() + 1.0;
                if (pad_idle(pad)) {
                    if (ioctl(pad, EVIOCGRAB, 1) < 0) {
                        if (verbose) fprintf(stderr, "cattura non riuscita: %s\n", strerror(errno));
                    } else if (!pad_idle(pad)) {
                        ioctl(pad, EVIOCGRAB, 0);              // premuto proprio ora: si riprova
                    } else {
                        // Scarta gli eventi gia' in coda (Android li ha gia' ricevuti).
                        struct input_event tmp[64];
                        while (read(pad, tmp, sizeof tmp) > 0) {}
                        zl_btn = zl_axis = minus = plus = hat_x = hat_y = 0;
                        if (vpad_create(pad) < 0) ioctl(pad, EVIOCGRAB, 0);
                    }
                }
            } else if (!want && vp.ufd >= 0) {
                int app_left = stick && enabled && !app_ok;
                if (!app_left || now_s() - vp.app_off_since >= APP_GRACE_S) {
                    vpad_destroy(pad, pad_rw);
                    // Lo schermo puo' essersi appena spento: se evgrab e' gia' partito ha
                    // visto il controller occupato e non lo ricattura piu'. In quel caso
                    // lo teniamo catturato noi (eventi scartati) fino alla fine dello standby.
                    if (standby_helper_running() && ioctl(pad, EVIOCGRAB, 1) == 0) {
                        hold = 1;
                        standby = 1;
                        fprintf(stderr, "standby: controller tenuto catturato\n");
                    }
                }
            }
        }

        // Con il giroscopio interno si campiona a tempo: il poll attende al massimo
        // fino al prossimo campione.
        int timeout = 500;
        if (use_iio) {
            double wait = next_sample - now_s();
            timeout = wait > 0 ? (int)(wait * 1000) + 1 : 0;
        }
        struct pollfd p[3] = {{use_iio ? -1 : imu, POLLIN, 0}, {pad, POLLIN, 0}, {vp.ufd, POLLIN, 0}};
        if (poll(p, 3, timeout) < 0) { if (errno == EINTR) continue; break; }

        if (p[2].revents & POLLIN) vpad_requests(pad_rw, verbose);

        struct input_event ev[64];
        if (p[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            vpad_destroy(-1, pad_rw);
            close(pad); pad = -1;
            continue;
        }
        if (p[1].revents & POLLIN) {
            ssize_t r = read(pad, ev, sizeof ev);
            if (r < 0 && errno != EAGAIN) {
                vpad_destroy(-1, pad_rw);
                close(pad); pad = -1;
                continue;
            }
            for (int i = 0; r > 0 && i < (int)(r / sizeof *ev); i++) {
                struct input_event *e = &ev[i];
                vpad_forward(pad, e);
                if (e->type == EV_KEY && e->code == BTN_TL2) zl_btn = e->value != 0;
                if (e->type == EV_KEY && e->code == BTN_SELECT) minus = e->value != 0;
                if (e->type == EV_KEY && e->code == BTN_START) plus = e->value != 0;
                if (e->type == EV_ABS && e->code == ABS_Z) zl_axis = e->value > 0;
                if (e->type != EV_ABS || (e->code != ABS_HAT0X && e->code != ABS_HAT0Y)) continue;

                // Combinazioni con Meno o Piu': agiscono sul fronte di pressione della croce.
                int *prev = e->code == ABS_HAT0X ? &hat_x : &hat_y;
                int pressed = e->value != 0 && *prev == 0;
                *prev = e->value;
                if (!(minus || plus) || !pressed || !app_ok) continue;

                char msg[96];
                if (e->code == ABS_HAT0Y) {
                    if (stick) {
                        ssens = e->value < 0 ? ssens * 1.25 : ssens / 1.25;
                        if (ssens > 10) ssens = 10;
                        if (ssens < 0.2) ssens = 0.2;
                        snprintf(msg, sizeof msg, "Sensibilita' stick: %.2f", ssens);
                    } else {
                        sens = e->value < 0 ? sens * 1.25 : sens / 1.25;
                        if (sens > 80) sens = 80;
                        if (sens < 2) sens = 2;
                        snprintf(msg, sizeof msg, "Sensibilita': %.1f", sens);
                    }
                } else if (e->value > 0) {
                    always = !always;
                    snprintf(msg, sizeof msg, "Modalita': %s", always ? "sempre attivo" : "solo con ZL");
                } else {
                    // Ciclo MOUSE -> STICK DESTRO -> SPENTO -> MOUSE
                    if (!enabled) { enabled = 1; stick = 0; }
                    else if (!stick) stick = 1;
                    else { enabled = 0; stick = 0; }
                    snprintf(msg, sizeof msg, "%s", !enabled ? "Gyro SPENTO" :
                             stick ? "Gyro -> STICK DESTRO" : "Gyro -> MOUSE");
                    vp.next_try = 0;
                }
                save_conf(conf);
                conf_changed(conf);          // la modifica e' nostra: non va ricaricata
                pulse(pad_rw, &ff_id, 120);
                notify(msg);
                fprintf(stderr, "%s\n", msg);
            }
        }

        int active = enabled && app_ok && (always || zl_btn || zl_axis);
        int to_mouse = !stick;

        if (use_iio) {
            double t = now_s();
            if (t < next_sample) continue;
            next_sample = t + IIO_PERIOD;
            double dt = last_sample > 0 ? t - last_sample : 0;
            last_sample = t;
            int got = 0;
            if (iio_read(&ig, m.gyro) == 0 && dt > 0 && dt < 0.1)
                got = motion_step(&m, dt, active || print_mode, to_mouse);
            if (vp.ufd >= 0) {
                double gx = 0, gy = 0;
                if (got && stick) gyro_to_stick(m.sx, m.sy, dead, &gx, &gy);
                vpad_gyro(active && stick && m.bias_ready, gx, gy);
            }
            continue;
        }

        if (p[0].revents & (POLLERR | POLLHUP)) { close(imu); imu = -1; continue; }
        if (!(p[0].revents & POLLIN)) {
            if (!active) vpad_gyro(0, 0, 0);
            continue;
        }
        ssize_t r = read(imu, ev, sizeof ev);
        if (r < 0) { if (errno != EAGAIN) { close(imu); imu = -1; } continue; }

        for (int i = 0; i < (int)(r / sizeof *ev); i++) {
            struct input_event *e = &ev[i];
            if (e->type == EV_ABS) {
                if (e->code == ABS_RX) m.gyro[0] = e->value / res;
                else if (e->code == ABS_RY) m.gyro[1] = e->value / res;
                else if (e->code == ABS_RZ) m.gyro[2] = e->value / res;
                continue;
            }
            if (e->type == EV_MSC && e->code == MSC_TIMESTAMP) {
                long long ts = (unsigned int)e->value;
                double dt = last_ts < 0 ? 0 : (ts - last_ts) / 1e6;
                if (dt < 0) dt += 4294.967296;              // wrap del contatore a 32 bit
                last_ts = ts;
                if (dt <= 0 || dt > 0.05) continue;         // primo campione o salto anomalo
                int got = motion_step(&m, dt, active || print_mode, to_mouse);
                if (vp.ufd >= 0) {
                    double gx = 0, gy = 0;
                    if (got && stick) gyro_to_stick(m.sx, m.sy, dead, &gx, &gy);
                    vpad_gyro(active && stick && m.bias_ready, gx, gy);
                }
            }
        }
    }

    vpad_destroy(pad, pad_rw);         // prima il clone, poi si rilascia la cattura
    if (mouse >= 0) {
        ioctl(mouse, UI_DEV_DESTROY);
        close(mouse);
    }
    if (use_iio) iio_close(&ig);
    if (imu >= 0) close(imu);
    if (pad >= 0) close(pad);
    if (pad_rw >= 0) close(pad_rw);
    return 0;
}

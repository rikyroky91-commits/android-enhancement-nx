// jcfix v2: libreria LD_PRELOAD per joycond (Switchroot/LineageOS).
//
// Problema 1 (vibrazione che si perde): Android, per ogni aggiornamento di vibrazione
// (es. GeForce NOW), ricarica l'effetto (EVIOCSFF) e poi invia di nuovo "play"
// (EV_FF value=1). joycond inoltra quel "play" ai Joy-Con mentre l'effetto e' gia' in
// corso e la vibrazione si perde. Se invece l'effetto viene solo aggiornato, continua
// a vibrare con la nuova intensita'.
// Soluzione: scartiamo i "play" di un effetto che sta gia' vibrando. Il ri-caricamento
// (gia' avvenuto) applica la nuova intensita' e riavvia la durata.
//
// Problema 2 (vibrazione che resta accesa): a volte, finita la vibrazione, il Joy-Con
// continua a vibrare anche se Android l'ha fermata.
// Soluzione: un thread di controllo. Quando su un Joy-Con non dovrebbe piu' vibrare
// niente, dopo 0,2 s e poi dopo 1,5 s gli manda un effetto a intensita' zero (30 ms)
// che riscrive "motori spenti" nel driver, poi lo cancella. Non tocca le vibrazioni
// in corso: gli effetti si sommano e uno a zero non cambia nulla.
//
// Log facoltativo: variabile d'ambiente JCFIX_LOG=percorso (max 300 righe).

#include <dlfcn.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_FD 512
#define MAX_ID 32
#define FLUSH_FIRST_MS 200
#define FLUSH_SECOND_MS 1300
#define FLUSH_EFFECT_MS 30

typedef ssize_t (*write_fn)(int, const void *, size_t);
typedef int (*ioctl_fn)(int, int, ...);
typedef int (*close_fn)(int);

static write_fn real_write;
static ioctl_fn real_ioctl;
static close_fn real_close;

static uint8_t is_ff_fd[MAX_FD];             // fd di un Joy-Con su cui e' stato caricato un effetto
static uint16_t length_ms[MAX_FD][MAX_ID];   // durata dell'ultimo upload (0 = infinita)
static uint8_t nonzero[MAX_FD][MAX_ID];      // l'ultimo upload ha intensita' > 0
static int64_t play_end_ms[MAX_FD][MAX_ID];  // fine prevista della vibrazione, 0 = ferma
static uint8_t was_active[MAX_FD];           // nell'ultimo controllo qualcosa vibrava
static uint8_t flushes_left[MAX_FD];
static int64_t next_flush_ms[MAX_FD];

static int lock_word;
static int thread_started;
static int log_fd = -1, log_lines;

static void lock(void) { while (__atomic_exchange_n(&lock_word, 1, __ATOMIC_ACQUIRE)) {} }
static void unlock(void) { __atomic_store_n(&lock_word, 0, __ATOMIC_RELEASE); }

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void jlog(const char *fmt, ...) {
    if (log_fd < 0 || log_lines >= 300) return;
    char buf[160];
    int k = snprintf(buf, sizeof buf, "%lld ", (long long)now_ms());
    va_list ap;
    va_start(ap, fmt);
    k += vsnprintf(buf + k, sizeof buf - k, fmt, ap);
    va_end(ap);
    if (k > (int)sizeof buf - 2) k = sizeof buf - 2;
    buf[k++] = '\n';
    real_write(log_fd, buf, k);
    log_lines++;
}

// La libc di Android chiama close()/write() gia' durante il proprio avvio, prima del
// nostro costruttore: i puntatori alle funzioni vere si risolvono al primo uso.
static void resolve(void) {
    if (!real_write) real_write = (write_fn)dlsym(RTLD_NEXT, "write");
    if (!real_ioctl) real_ioctl = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
    if (!real_close) real_close = (close_fn)dlsym(RTLD_NEXT, "close");
}

__attribute__((constructor)) static void init(void) {
    resolve();
    const char *p = getenv("JCFIX_LOG");
    if (p && *p) log_fd = open(p, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    jlog("jcfix v2 caricato");
}

static int64_t end_from(int64_t t, uint16_t len) { return len ? t + len : INT64_MAX; }

// Qualcosa su questo fd dovrebbe vibrare adesso? (chiamare con il lock)
static int fd_active(int fd, int64_t t) {
    for (int id = 0; id < MAX_ID; id++)
        if (play_end_ms[fd][id] > t && nonzero[fd][id]) return 1;
    return 0;
}

// Riscrive "motori spenti": effetto a intensita' zero, breve, poi cancellato.
static void flush_zero(int fd) {
    struct ff_effect fx;
    memset(&fx, 0, sizeof fx);
    fx.type = FF_RUMBLE;
    fx.id = -1;
    fx.replay.length = FLUSH_EFFECT_MS;
    if (real_ioctl(fd, (int)EVIOCSFF, &fx) < 0) return;  // non e' (piu') un dispositivo con vibrazione
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = EV_FF;
    ev.code = (uint16_t)fx.id;
    ev.value = 1;
    real_write(fd, &ev, sizeof ev);
    usleep((FLUSH_EFFECT_MS + 20) * 1000);
    real_ioctl(fd, (int)EVIOCRMFF, (void *)(intptr_t)fx.id);
}

static void *watchdog(void *arg) {
    (void)arg;
    for (;;) {
        usleep(50 * 1000);
        int64_t t = now_ms();
        for (int fd = 0; fd < MAX_FD; fd++) {
            int todo = 0;
            lock();
            if (is_ff_fd[fd]) {
                if (fd_active(fd, t)) {
                    was_active[fd] = 1;
                    flushes_left[fd] = 0;
                } else if (was_active[fd]) {
                    was_active[fd] = 0;
                    flushes_left[fd] = 2;
                    next_flush_ms[fd] = t + FLUSH_FIRST_MS;
                } else if (flushes_left[fd] && t >= next_flush_ms[fd]) {
                    flushes_left[fd]--;
                    next_flush_ms[fd] = t + FLUSH_SECOND_MS;
                    todo = 1;
                }
            }
            unlock();
            if (todo) {
                flush_zero(fd);
                jlog("reset vibrazione fd=%d", fd);
            }
        }
    }
    return NULL;
}

static void start_watchdog(void) {
    if (__atomic_exchange_n(&thread_started, 1, __ATOMIC_ACQ_REL)) return;
    pthread_t th;
    if (pthread_create(&th, NULL, watchdog, NULL) == 0) pthread_detach(th);
    else jlog("thread di controllo non avviato");
}

ssize_t write(int fd, const void *buf, size_t n) {
    resolve();
    if (n == sizeof(struct input_event) && fd >= 0 && fd < MAX_FD && is_ff_fd[fd]) {
        const struct input_event *ev = buf;
        if (ev->type == EV_FF && ev->code < MAX_ID) {
            int64_t t = now_ms();
            int drop = 0;
            lock();
            int64_t *end = &play_end_ms[fd][ev->code];
            if (ev->value > 0) {
                drop = *end > t;   // gia' in corso: l'upload ha gia' aggiornato tutto
                *end = end_from(t, length_ms[fd][ev->code]);
                was_active[fd] = 1;
            } else {
                *end = 0;
            }
            unlock();
            if (drop) return (ssize_t)n;
        }
    }
    return real_write(fd, buf, n);
}

int ioctl(int fd, int req, ...) {
    resolve();
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    int ret = real_ioctl(fd, req, arg);
    if (ret < 0 || fd < 0 || fd >= MAX_FD) return ret;

    if ((unsigned int)req == (unsigned int)EVIOCSFF) {
        const struct ff_effect *fx = arg;
        if (fx->id >= 0 && fx->id < MAX_ID) {
            int64_t t = now_ms();
            lock();
            is_ff_fd[fd] = 1;
            length_ms[fd][fx->id] = fx->replay.length;
            nonzero[fd][fx->id] = fx->type != FF_RUMBLE ||
                                  fx->u.rumble.strong_magnitude || fx->u.rumble.weak_magnitude;
            // Un upload su un effetto in corso ne riavvia la durata nel kernel.
            if (play_end_ms[fd][fx->id] > t) play_end_ms[fd][fx->id] = end_from(t, fx->replay.length);
            unlock();
            start_watchdog();
        }
    } else if ((unsigned int)req == (unsigned int)EVIOCRMFF) {
        int id = (int)(intptr_t)arg;
        if (id >= 0 && id < MAX_ID) {
            lock();
            length_ms[fd][id] = 0;
            nonzero[fd][id] = 0;
            play_end_ms[fd][id] = 0;
            unlock();
        }
    }
    return ret;
}

// Se joycond chiude un Joy-Con, dimentichiamo quel fd (potrebbe essere riusato).
int close(int fd) {
    resolve();
    if (fd >= 0 && fd < MAX_FD) {
        lock();
        if (is_ff_fd[fd]) {
            is_ff_fd[fd] = 0;
            was_active[fd] = 0;
            flushes_left[fd] = 0;
            memset(play_end_ms[fd], 0, sizeof play_end_ms[fd]);
            memset(nonzero[fd], 0, sizeof nonzero[fd]);
        }
        unlock();
    }
    return real_close(fd);
}

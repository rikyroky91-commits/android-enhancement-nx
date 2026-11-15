// fftest: fa vibrare un dispositivo di input via force feedback (FF_RUMBLE).
// Uso: fftest /dev/input/eventN [millisecondi] [burst]
// "burst" imita Android/GeForce NOW: stop + nuovo upload + play ogni 100 ms per 3 s.
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "uso: %s /dev/input/eventN [ms]\n", argv[0]); return 2; }
    int ms = argc > 2 ? atoi(argv[2]) : 800;
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    if (argc > 3 && !strcmp(argv[3], "update")) {
        // Un solo play con effetto lungo, poi solo ri-upload (senza play) per cambiare intensita'.
        static const int amp[][2] = {{175, 24}, {6, 72}, {179, 76}, {80, 25}, {87, 77}};
        struct ff_effect fx;
        memset(&fx, 0, sizeof fx);
        fx.type = FF_RUMBLE;
        fx.id = -1;
        fx.replay.length = 4000;
        for (int i = 0; i < 30; i++) {
            fx.u.rumble.strong_magnitude = amp[i % 5][0] * 0xFFFF / 0xFF;
            fx.u.rumble.weak_magnitude = amp[i % 5][1] * 0xFFFF / 0xFF;
            if (ioctl(fd, EVIOCSFF, &fx) < 0) { perror("EVIOCSFF"); return 1; }
            if (i == 0) {
                struct input_event ev;
                memset(&ev, 0, sizeof ev);
                ev.type = EV_FF; ev.code = fx.id; ev.value = 1;
                if (write(fd, &ev, sizeof ev) < 0) perror("play");
            }
            usleep(100000);
        }
        struct input_event ev;
        memset(&ev, 0, sizeof ev);
        ev.type = EV_FF; ev.code = fx.id; ev.value = 0;
        if (write(fd, &ev, sizeof ev) < 0) perror("stop");
        printf("update inviato (1 play + 30 aggiornamenti in 3 s)\n");
        return 0;
    }

    int nostop = argc > 3 && !strcmp(argv[3], "burst-nostop");
    if (argc > 3 && (!strcmp(argv[3], "burst") || nostop)) {
        static const int amp[][2] = {{175, 24}, {6, 72}, {179, 76}, {80, 25}, {87, 77}};
        int id = -1;
        for (int i = 0; i < 30; i++) {
            struct input_event ev;
            memset(&ev, 0, sizeof ev);
            if (id >= 0 && !nostop) {            // cancelVibrate di Android
                ev.type = EV_FF; ev.code = id; ev.value = 0;
                if (write(fd, &ev, sizeof ev) < 0) perror("stop");
            }
            struct ff_effect fx;
            memset(&fx, 0, sizeof fx);
            fx.type = FF_RUMBLE;
            fx.id = id;
            fx.u.rumble.strong_magnitude = amp[i % 5][0] * 0xFFFF / 0xFF;
            fx.u.rumble.weak_magnitude = amp[i % 5][1] * 0xFFFF / 0xFF;
            fx.replay.length = 1000;
            if (ioctl(fd, EVIOCSFF, &fx) < 0) { perror("EVIOCSFF"); return 1; }
            id = fx.id;
            ev.type = EV_FF; ev.code = id; ev.value = 1;
            if (write(fd, &ev, sizeof ev) < 0) perror("play");
            usleep(100000);
        }
        struct input_event ev;
        memset(&ev, 0, sizeof ev);
        ev.type = EV_FF; ev.code = id; ev.value = 0;
        if (write(fd, &ev, sizeof ev) < 0) perror("stop");
        printf("burst inviato (30 aggiornamenti in 3 s)\n");
        return 0;
    }

    struct ff_effect fx;
    memset(&fx, 0, sizeof fx);
    fx.type = FF_RUMBLE;
    fx.id = -1;
    fx.u.rumble.strong_magnitude = 0xc000;
    fx.u.rumble.weak_magnitude = 0xc000;
    fx.replay.length = ms;
    if (ioctl(fd, EVIOCSFF, &fx) < 0) { perror("EVIOCSFF"); return 1; }

    struct input_event play;
    memset(&play, 0, sizeof play);
    play.type = EV_FF;
    play.code = fx.id;
    play.value = 1;
    if (write(fd, &play, sizeof play) != sizeof play) { perror("play"); return 1; }
    usleep((ms + 200) * 1000);
    ioctl(fd, EVIOCRMFF, fx.id);
    close(fd);
    printf("rumble inviato per %d ms (effetto %d)\n", ms, fx.id);
    return 0;
}

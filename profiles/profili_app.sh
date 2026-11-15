#!/system/bin/sh
# Servizio Magisk (service.d): profili automatici in base all'app in primo piano.
#  - Emulatori: joycond senza emulazione analogica dei grilletti (ZL/ZR arrivano solo
#    come tasti: niente doppie pressioni) e ventola sul profilo "Cool".
#  - Streaming (GeForce NOW, Moonlight, CloudPad, Asobi, Chiaki): grilletti analogici attivi
#    e overclock limitato (CPU 1785 MHz, GPU 921 MHz). Il video lo decodifica un circuito
#    dedicato: meno calore e piu' batteria a parita' di immagine. All'uscita l'overclock
#    torna attivo, ma solo se l'aveva tolto questo servizio (il riquadro Overclock vince).
#  - Tutto il resto: grilletti analogici attivi e ventola sul profilo scelto dal sistema.
# Le proprieta' di joycond vengono rilette al volo; i profili ventola sono quelli ufficiali
# (Handheld / Console / Cool) scrivibili in sysfs.

LOG=/data/adb/gyromouse/profili_app.log
FAN_NODES="/sys/devices/pwm-fan/fan_profile /sys/devices/thermal-fan-est/fan_profile"
EMULATORI="io.github.lime3ds.android org.azahar_emu.azahar com.retroarch com.retroarch.aarch64 org.dolphinemu.dolphinemu org.ppsspp.ppsspp org.ppsspp.ppssppgold xyz.aethersx2.android xyz.aethersx2.custom com.github.stenzek.duckstation org.vita3k.emulator dev.eden.eden_emulator"
STREAMING="com.nvidia.geforcenow com.limelight com.cloudpad.stream com.asobi.droid com.gameblabla.chiaki"
TILES=/data/adb/gyromouse/tiles.sh
OCFLAG=/dev/switchtiles_oc_off
CPU_MAX=/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
CPU_LIMITE=1785000

until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 5; done
sleep 15

log() {
    [ -f "$LOG" ] && [ "$(stat -c %s "$LOG")" -gt 32768 ] && mv "$LOG" "$LOG.old"
    echo "$(date '+%F %T') $*" >> "$LOG"
}

# Pacchetto dell'app in primo piano (processo principale nel cgroup top-app)
top_app() {
    for p in $(cat /dev/cpuset/top-app/cgroup.procs 2>/dev/null); do
        c=$(tr '\0' '\n' < /proc/$p/cmdline 2>/dev/null | head -1)
        case "$c" in
            ""|*:*|/*|system_server|com.android.systemui|com.android.launcher3|*inputmethod*) ;;
            *) echo "$c"; return ;;
        esac
    done
}

# Occhiali RayNeo collegati? (stesso controllo di azahar_occhiali.sh: EDID TCL 0x03d4)
occhiali() {
    [ "$(cat /sys/class/switch/tegradc.1/state 2>/dev/null)" != offline ] &&
        head -1 /sys/kernel/debug/tegradc.1/edid 2>/dev/null | grep -q "50 6c d4 03"
}
ra_moved_pid=""

fan_get() { cat /sys/devices/pwm-fan/fan_profile 2>/dev/null; }
fan_set() { for n in $FAN_NODES; do [ -w "$n" ] && echo "$1" > "$n" 2>/dev/null; done; }

mode=""
fan_saved=""
oc_limited=0
log "avvio servizio (ventola attuale: $(fan_get))"
while true; do
    app=$(top_app)
    want=normale
    for e in $EMULATORI; do [ "$app" = "$e" ] && want=emulatore; done
    for s in $STREAMING; do [ "$app" = "$s" ] && want=streaming; done

    if [ "$want" != "$mode" ]; then
        # Uscita dallo streaming: ridà l'overclock solo se l'aveva tolto questo servizio
        if [ "$mode" = streaming ] && [ "$oc_limited" = 1 ]; then
            sh $TILES oc on > /dev/null 2>&1
            oc_limited=0
            log "overclock ripristinato"
        fi
        if [ "$want" = emulatore ]; then
            fan_saved=$(fan_get)
            setprop persist.vendor.joycond.analog 0
            fan_set Cool
            log "profilo emulatore per $app (grilletti solo tasti, ventola Cool; prima: $fan_saved)"
        else
            setprop persist.vendor.joycond.analog 1
            [ -n "$fan_saved" ] && fan_set "$fan_saved"
            [ -n "$mode" ] && log "profilo $want per ${app:-?} (grilletti analogici, ventola ${fan_saved:-invariata})"
            fan_saved=""
        fi
        if [ "$want" = streaming ] && [ ! -e $OCFLAG ]; then
            sh $TILES oc off > /dev/null 2>&1
            oc_limited=1
            log "overclock limitato per lo streaming ($app)"
        fi
        mode=$want
    elif [ "$mode" = emulatore ] && [ "$(fan_get)" != Cool ]; then
        fan_set Cool    # il sistema ha cambiato profilo (es. dock): lo riporto su Cool
    elif [ "$mode" = streaming ] && [ "$oc_limited" = 1 ]; then
        if [ ! -e $OCFLAG ]; then
            oc_limited=0    # l'utente ha riacceso l'overclock dal riquadro: si rispetta
        elif [ "$(cat $CPU_MAX 2>/dev/null)" != $CPU_LIMITE ]; then
            sh $TILES oc off > /dev/null 2>&1    # il sistema ha rialzato i limiti
        fi
    fi

    # RetroArch con gli occhiali RayNeo collegati: lo riapre sullo schermo degli occhiali
    # (in 3D sono 3840x1080 e la copia dello schermo della Switch spezzerebbe l'affiancato).
    # Una volta sola per ogni avvio di RetroArch (controllo sul PID).
    if [ "$app" = com.retroarch.aarch64 ] && occhiali; then
        pid=$(pidof com.retroarch.aarch64)
        if [ -n "$pid" ] && [ "$pid" != "$ra_moved_pid" ]; then
            ext=$(dumpsys display 2>/dev/null | sed -n 's/.*mDisplayId=\([0-9]*\).*/\1/p' | sort -un | grep -v '^0$' | head -1)
            if [ -n "$ext" ]; then
                am force-stop com.retroarch.aarch64
                sleep 1
                am start --display "$ext" -n com.retroarch.aarch64/com.retroarch.browser.mainmenu.MainMenuActivity > /dev/null 2>&1
                sleep 4
                ra_moved_pid=$(pidof com.retroarch.aarch64)
                log "RetroArch riaperto sullo schermo degli occhiali (display $ext)"
            fi
        fi
    fi
    sleep 3
done

#!/system/bin/sh
# Servizio Magisk (service.d): "ibernazione" della Switch con Android.
# La sospensione Android su Switch consuma ~3%/ora. Invece di sospendere:
#  - a schermo spento e NON in carica: la console resta sveglia (wakelock) e dopo
#    TIMEOUT secondi si spegne del tutto (consumo zero fino alla riaccensione da hekate);
#  - in carica: wakelock rilasciato, sospensione normale, nessuno spegnimento.
# Riaccendere lo schermo prima del TIMEOUT azzera il conteggio.

TIMEOUT=1800                       # 30 minuti
LOCK=ibernazione
LOG=/data/adb/gyromouse/ibernazione.log

until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 5; done

wakefulness() { dumpsys power | grep -m1 'mWakefulness=' | sed 's/.*mWakefulness=//'; }
charging() {
    case "$(cat /sys/class/power_supply/battery/status 2>/dev/null)" in
        Charging|Full) return 0 ;;
    esac
    return 1
}
uptime_s() { cut -d. -f1 /proc/uptime; }
log() { echo "$(date '+%F %T') $*" >> "$LOG"; }

held=0
off_since=0
log "avvio servizio, timeout ${TIMEOUT}s"
while true; do
    if charging; then
        # In carica: niente spegnimento, lascia sospendere normalmente.
        [ $held = 1 ] && { echo $LOCK > /sys/power/wake_unlock; held=0; log "in carica: sospensione normale"; }
        off_since=0
    else
        [ $held = 0 ] && { echo $LOCK > /sys/power/wake_lock; held=1; log "a batteria: wakelock attivo"; }
        if [ "$(wakefulness)" = Awake ]; then
            off_since=0
        elif [ $off_since = 0 ]; then
            off_since=$(uptime_s)
            log "schermo spento, spegnimento tra ${TIMEOUT}s"
        elif [ $(( $(uptime_s) - off_since )) -ge $TIMEOUT ]; then
            log "schermo spento da ${TIMEOUT}s: spegnimento"
            svc power shutdown
            sleep 60
            reboot -p
        fi
    fi
    sleep 5
done

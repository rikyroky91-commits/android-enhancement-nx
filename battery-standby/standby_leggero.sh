#!/system/bin/sh
# Servizio Magisk (service.d): "standby leggero" per Switch con LineageOS.
# A schermo spento:
#  - cattura in esclusiva gli eventi dei Joy-Con (evgrab): un tasto premuto per sbaglio
#    non annulla piu' la sospensione. NB: non si scollega il driver dei binari
#    (joycon_serdev unbind/bind manda il kernel in panic);
#  - spegne subito il Bluetooth;
#  - per i primi WIFI_RITARDO secondi tiene la console sveglia con il Wi-Fi acceso e senza
#    Doze forzato: una sessione online in pausa resta collegata e download e aggiornamenti
#    delle app continuano. Uno standby piu' breve non tocca il Wi-Fi;
#  - passato il ritardo spegne il Wi-Fi (se WIFI_OFF=1, ma mai con il firmware Wi-Fi bloccato:
#    spegni/riaccendi in quello stato manda il kernel in panic), forza Android in Doze e
#    lascia sospendere. Alla riaccensione dello schermo ripristina tutto.
# Riquadro "Ibernazione" (Riquadri Switch): crea SUBITO e spegne lo schermo; quello standby
# salta il ritardo e spegne Wi-Fi e servizi subito. Alla riaccensione si ripristina tutto.
# Ogni standby viene registrato nel log con il consumo in mA e %/ora.

WIFI_OFF=1
WIFI_RITARDO=600    # secondi a schermo spento prima di spegnere il Wi-Fi (0 = subito)
EVGRAB=/data/adb/gyromouse/evgrab
LOCK=standby_leggero
LOG=/data/adb/gyromouse/standby.log
BAT=/sys/class/power_supply/battery
SUBITO=/dev/standby_subito   # richiesta del riquadro Ibernazione (tmpfs, valida 30 s)

until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 5; done

log() { echo "$(date '+%F %T') $*" >> "$LOG"; }
wakefulness() { dumpsys power | grep 'mWakefulness=' | sed -n '1s/.*mWakefulness=//p'; }
charging() {
    case "$(cat $BAT/status 2>/dev/null)" in Charging|Full) return 0 ;; esac
    return 1
}
# Firmware Wi-Fi bloccato? (flag di wifi_stabile.sh, oppure errori recenti in dmesg)
# In quel caso spegnere/riaccendere il Wi-Fi manda il kernel in panic: non lo tocchiamo.
fw_bad() {
    [ -e /dev/wifi_fw_bloccato ] && return 0
    dmesg 2>/dev/null | tail -100 | grep -qE "Firmware reported general error|Timeout on response for query command"
}
wifi_off_done=0
deep=0
grab_pid=""
# Il riquadro ha chiesto lo standby profondo subito? (richiesta recente, poi la cancella)
subito_richiesto() {
    [ -e $SUBITO ] || return 1
    age=$(( $(date +%s) - $(stat -c %Y $SUBITO) ))
    rm -f $SUBITO
    [ $age -lt 30 ]
}
grab_on()  { [ -n "$grab_pid" ] || { "$EVGRAB" "Joy-Con" "Pro Controller" 2>/dev/null & grab_pid=$!; }; }
grab_off() { [ -n "$grab_pid" ] && { kill "$grab_pid" 2>/dev/null; wait "$grab_pid" 2>/dev/null; grab_pid=""; }; }

enter_standby() {
    wifi_was=$(settings get global wifi_on)
    bt_was=$(settings get global bluetooth_on)
    adbwifi_was=$(settings get global adb_wifi_enabled)
    grab_on
    [ "$bt_was" = 1 ] && svc bluetooth disable
    wifi_off_done=0
    deep=0
    subito=0; subito_richiesto && subito=1
    c0=$(cat $BAT/charge_counter); p0=$(cat $BAT/capacity); t0=$(date +%s)
    log "standby: batteria ${p0}% (wifi=$wifi_was bt=$bt_was in_carica=$(charging && echo si || echo no), $([ $subito = 1 ] && echo "ibernazione: wifi spento subito" || echo "wifi spento tra $((WIFI_RITARDO / 60)) min"))"
}

# Fine del periodo con Wi-Fi acceso: spegne il Wi-Fi e forza il Doze
go_deep() {
    if [ "$WIFI_OFF" = 1 ] && [ "$(settings get global wifi_on)" != 0 ]; then
        if fw_bad; then
            log "firmware Wi-Fi bloccato: il Wi-Fi resta acceso (spegnerlo causerebbe un crash)"
        else
            cmd wifi set-wifi-enabled disabled && wifi_off_done=1
        fi
    fi
    dumpsys deviceidle force-idle > /dev/null 2>&1
    deep=1
    log "sonno profondo dopo $(( ($(date +%s) - t0) / 60 )) min (wifi spento=$wifi_off_done)"
}

leave_standby() {
    grab_off
    [ "$deep" = 1 ] && dumpsys deviceidle unforce > /dev/null 2>&1
    [ "$bt_was" = 1 ] && svc bluetooth enable
    if [ "$wifi_off_done" = 1 ] && fw_bad; then
        log "firmware Wi-Fi bloccato: il Wi-Fi resta spento fino al riavvio"
    elif [ "$wifi_off_done" = 1 ]; then
        cmd wifi set-wifi-enabled enabled
        [ "$adbwifi_was" = 1 ] && (sleep 15; settings put global adb_wifi_enabled 1) &
    fi
    c1=$(cat $BAT/charge_counter); p1=$(cat $BAT/capacity); t1=$(date +%s)
    secs=$((t1 - t0))
    if [ $secs -ge 600 ]; then
        # charge_counter in uAh: consumo medio in mA e in %/ora sulla capacita' dichiarata
        full=$(cat $BAT/charge_full 2>/dev/null)
        ma=$(( (c0 - c1) * 3600 / secs / 1000 ))
        pph=$(( (c0 - c1) * 3600 * 100 / secs / full ))
        log "risveglio: ${p1}% dopo $((secs / 60)) min, consumo medio ${ma} mA = ~${pph}%/ora"
    else
        log "risveglio: ${p1}% dopo $((secs / 60)) min (wifi spento=$wifi_off_done)"
    fi
    deep=0
}

state=awake
echo $LOCK > /sys/power/wake_lock      # da svegli: nessuna sospensione prima di aver agito
log "avvio servizio (wifi_off=$WIFI_OFF, ritardo=${WIFI_RITARDO}s)"
while true; do
    w=$(wakefulness)
    if [ "$w" != Awake ] && [ $state = awake ]; then
        enter_standby
        state=standby
        if [ "$WIFI_RITARDO" -gt 0 ] && [ "$subito" = 0 ]; then
            # Console sveglia durante il ritardo. Il blocco ha una scadenza (in nanosecondi,
            # scritta come testo): se lo script si fermasse, la console si sospende lo stesso.
            echo "$LOCK $((WIFI_RITARDO + 120))000000000" > /sys/power/wake_lock
        else
            go_deep
            echo $LOCK > /sys/power/wake_unlock
        fi
    elif [ "$w" = Awake ] && [ $state = standby ]; then
        echo $LOCK > /sys/power/wake_lock
        leave_standby
        state=awake
    elif [ $state = standby ] && [ "$deep" = 0 ] && [ $(( $(date +%s) - t0 )) -ge "$WIFI_RITARDO" ]; then
        go_deep
        echo $LOCK > /sys/power/wake_unlock
    fi
    sleep 2
done

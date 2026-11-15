#!/system/bin/sh
# Servizio Magisk (service.d): temperature nella notifica fissa di "Riquadri Switch".
# I sensori sono leggibili solo da root: qui si compone una riga di testo e la si scrive nel
# file dell'app, che aggiorna una notifica silenziosa (service TempService).
# Lavora solo a schermo acceso: il wakelock "standby_leggero" e' attivo solo in quel caso.

PKG=local.switchtiles
FILE=/data/data/$PKG/files/temps.txt
FAN=/sys/devices/pwm-fan

until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 5; done
sleep 15

# Trova il sensore in base al nome: la numerazione delle zone puo' cambiare.
zone() {
    for z in /sys/class/thermal/thermal_zone*; do
        [ "$(cat "$z/type" 2>/dev/null)" = "$1" ] && { echo "$z/temp"; return; }
    done
}
Z_CPU=$(zone CPU-therm)
Z_GPU=$(zone GPU-therm)
Z_BAT=$(zone battery)
Z_BRD=$(zone Tboard_tegra)

deg() { [ -n "$1" ] && v=$(cat "$1" 2>/dev/null) && [ -n "$v" ] && echo "$((v / 1000))°" || echo "?"; }

start_app() {
    pm grant $PKG android.permission.POST_NOTIFICATIONS > /dev/null 2>&1
    cmd activity start-foreground-service -n $PKG/.TempService > /dev/null 2>&1
}

raw() { [ -n "$1" ] && v=$(cat "$1" 2>/dev/null) && [ -n "$v" ] && echo $((v / 1000)) || echo 0; }

# Soglie dell'avviso (gradi): scatta a SOGLIA, si chiude sotto RIENTRO, cosi' non lampeggia.
CHIP_SOGLIA=85; CHIP_RIENTRO=78
BATT_SOGLIA=45; BATT_RIENTRO=42

start_app
tick=0
alert=""
while true; do
    if grep -q standby_leggero /sys/power/wake_lock 2>/dev/null; then
        pwm=$(cat $FAN/cur_pwm 2>/dev/null)
        cap=$(cat $FAN/pwm_cap 2>/dev/null)
        fan="?"
        [ -n "$pwm" ] && [ "${cap:-0}" -gt 0 ] && fan="$((pwm * 100 / cap))%"
        c=$(raw "$Z_CPU"); g=$(raw "$Z_GPU"); b=$(raw "$Z_BAT")
        if [ -z "$alert" ]; then
            if [ "$c" -ge $CHIP_SOGLIA ] || [ "$g" -ge $CHIP_SOGLIA ]; then
                alert="Chip molto caldo: CPU ${c}°, GPU ${g}°. Fai una pausa o abbassa il carico."
            elif [ "$b" -ge $BATT_SOGLIA ]; then
                alert="Batteria calda: ${b}°. Meglio staccarla dal caricatore e farla raffreddare."
            fi
        elif [ "$c" -lt $CHIP_RIENTRO ] && [ "$g" -lt $CHIP_RIENTRO ] && [ "$b" -lt $BATT_RIENTRO ]; then
            alert=""
        fi
        # ">" su un file esistente ne conserva proprietario e contesto SELinux.
        # Riga 1: testo della notifica; riga 2: avviso (vuota se tutto e' a posto).
        [ -f "$FILE" ] && printf '%s\n%s\n' "CPU ${c}°  GPU ${g}°  Batt ${b}°  Sch $(deg "$Z_BRD")  Fan $fan" "$alert" > "$FILE"
        tick=$((tick + 1))
        if [ $tick -ge 12 ]; then       # ~ ogni minuto: riavvia la notifica se il servizio e' stato chiuso
            tick=0
            # controlla il servizio, non solo l'app: dopo un aggiornamento l'app riparte
            # per i riquadri ma il servizio delle temperature no
            dumpsys activity services $PKG/.TempService 2>/dev/null | grep -q ServiceRecord || start_app
        fi
    fi
    sleep 5
done

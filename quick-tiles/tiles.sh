#!/system/bin/sh
# Riquadri Switch: comandi eseguiti come root dall'app "Riquadri Switch" (local.switchtiles).
# Va in /data/adb/gyromouse/tiles.sh (cartella scrivibile solo da root).
# Accetta solo questi comandi; alla fine stampa sempre lo stato su una riga:
#   tiles.sh stato
#   tiles.sh oc on|off|toggle      overclock CPU/GPU (non permanente: al riavvio torna attivo)
#   tiles.sh gyro ciclo            MOUSE -> STICK DESTRO -> SPENTO -> MOUSE
#   tiles.sh gyro zl               alterna "solo con ZL" / "sempre attivo"
#   tiles.sh sens su|giu           sensibilita' della modalita' attiva +25% / -25%
#   tiles.sh colore ciclo          colore pannello OLED: Vivid -> Natural -> Basic -> Night
#   tiles.sh iberna ora            "Ibernazione": standby profondo subito (Wi-Fi e servizi spenti;
#                                  li spegne e riaccende standby_leggero.sh)
# gyromouse rilegge da solo gyromouse.conf entro 1 s.

CONF=/data/adb/gyromouse/gyromouse.conf
OCFLAG=/dev/switchtiles_oc_off      # in tmpfs: sparisce al riavvio, come la limitazione
CPUDIR=/sys/devices/system/cpu
GPU=/sys/devices/57000000.gpu/devfreq/57000000.gpu
CPU_STD=1785000                     # kHz, frequenza massima senza overclock
GPU_STD=921600000                   # Hz

cpu_top() { cat $CPUDIR/cpu0/cpufreq/cpuinfo_max_freq; }
gpu_top() { tr ' ' '\n' < $GPU/available_frequencies | grep . | sort -n | tail -1; }
set_cpu() { for f in $CPUDIR/cpu[0-9]*/cpufreq/scaling_max_freq; do echo "$1" > "$f" 2>/dev/null; done; }
set_gpu() { echo "$1" > $GPU/max_freq 2>/dev/null; }
oc_on()  { rm -f $OCFLAG; set_cpu "$(cpu_top)"; set_gpu "$(gpu_top)"; }
oc_off() { touch $OCFLAG; set_cpu $CPU_STD; set_gpu $GPU_STD; }

PANEL=/sys/devices/50000000.host1x/tegradc.0/panel_color_mode
PANEL_CONF=/data/adb/gyromouse/pannello_oled.conf
SC_PREFS=/data/data/org.lineageos.settings.device/shared_prefs/org.lineageos.settings.device_preferences.xml

# Applica il preset al pannello e lo salva nei due posti letti all'avvio
set_color() {
    echo "$1" > $PANEL
    echo "$1" > $PANEL_CONF
    if [ -f $SC_PREFS ]; then
        sed "s|<string name=\"panel_color_mode\">[^<]*</string>|<string name=\"panel_color_mode\">$1</string>|" $SC_PREFS > /data/local/tmp/sc_prefs.$$ &&
            cat /data/local/tmp/sc_prefs.$$ > $SC_PREFS     # cat > mantiene proprietario e contesto
        rm -f /data/local/tmp/sc_prefs.$$
    fi
}

read_conf() {
    set -- $(cat $CONF 2>/dev/null)
    sens=${1:-12.000}; always=${2:-0}; enabled=${3:-1}; stick=${4:-0}; ssens=${5:-1.000}; adz=${6:-0.150}
}
write_conf() {
    echo "$sens $always $enabled $stick $ssens $adz" > $CONF.tmp && chmod 644 $CONF.tmp && mv -f $CONF.tmp $CONF
}
# valore * fattore, limitato tra minimo e massimo, 3 decimali
scale() { awk -v v="$1" -v f="$2" -v lo="$3" -v hi="$4" 'BEGIN { v *= f; if (v > hi) v = hi; if (v < lo) v = lo; printf "%.3f", v }'; }

case "$1 $2" in
    "oc on")     oc_on ;;
    "oc off")    oc_off ;;
    "oc toggle") if [ -e $OCFLAG ]; then oc_on; else oc_off; fi ;;
    "colore ciclo")
        case "$(cat $PANEL)" in
            0x65) set_color 0x23 ;;
            0x23) set_color 0x03 ;;
            0x03) set_color 0x43 ;;
            *)    set_color 0x65 ;;
        esac ;;
    "gyro ciclo")
        read_conf
        if [ "$enabled" = 0 ]; then enabled=1; stick=0
        elif [ "$stick" = 0 ]; then stick=1
        else enabled=0; stick=0; fi
        write_conf ;;
    "gyro zl")   read_conf; always=$((1 - always)); write_conf ;;
    "sens su"|"sens giu")
        read_conf
        f=1.25; [ "$2" = giu ] && f=0.8
        if [ "$stick" = 1 ]; then ssens=$(scale $ssens $f 0.2 10); else sens=$(scale $sens $f 2 80); fi
        write_conf ;;
    "iberna ora")
        # Segnala a standby_leggero.sh di saltare il ritardo, poi spegne lo schermo
        # (in un processo separato, cosi' lo stato arriva comunque al riquadro).
        touch /dev/standby_subito
        setsid sh -c 'sleep 1; input keyevent 223' > /dev/null 2>&1 < /dev/null &
        ;;
    "stato ") ;;
    *) echo "comando non valido: $*" >&2; exit 2 ;;
esac

read_conf
oc=1; [ -e $OCFLAG ] && oc=0
cpu=$(( $(cat $CPUDIR/cpu0/cpufreq/scaling_max_freq) / 1000 ))
gpu=$(( $(cat $GPU/max_freq) / 1000000 ))
echo "oc=$oc cpu=$cpu gpu=$gpu enabled=$enabled stick=$stick always=$always sens=$sens ssens=$ssens color=$(cat $PANEL)"

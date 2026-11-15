#!/system/bin/sh
# Servizio Magisk (service.d): modalita' colore del pannello OLED della Switch a ogni avvio.
# Su questa build l'app Switch Configuration non ha l'opzione, ma il kernel espone il nodo
# panel_color_mode: sono i preset interni del pannello Samsung (nessun rischio hardware).
# Valori: Basic 0x03 (fedele) | Natural 0x23 (predefinito) | Vivid 0x65 | Washed 0x45
#         Night 0x43 / 0x15 / 0x35 / 0x75
# Per cambiarlo: scrivi il valore in /data/adb/gyromouse/pannello_oled.conf (es. "0x03").

NODE=/sys/devices/50000000.host1x/tegradc.0/panel_color_mode
CONF=/data/adb/gyromouse/pannello_oled.conf
LOG=/data/adb/gyromouse/pannello_oled.log

until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 5; done
sleep 5

[ -f "$CONF" ] || echo 0x65 > "$CONF"
val=$(tr -d ' \r\n' < "$CONF")
case "$val" in
    0x03|0x23|0x65|0x45|0x43|0x15|0x35|0x75) ;;
    *) echo "$(date '+%F %T') valore non valido in $CONF: '$val' (lasciato il predefinito)" >> "$LOG"; exit 0 ;;
esac
[ -w "$NODE" ] || { echo "$(date '+%F %T') nodo $NODE assente" >> "$LOG"; exit 0; }
echo "$val" > "$NODE"
echo "$(date '+%F %T') modalita' colore pannello: $(cat "$NODE")" >> "$LOG"

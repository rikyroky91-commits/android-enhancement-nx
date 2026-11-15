#!/system/bin/sh
# Servizio Magisk (service.d): avvia gyromouse a ogni accensione e lo riavvia se si chiude.
# Impostazioni (sensibilita', modalita') in /data/adb/gyromouse/gyromouse.conf.

BIN=/data/adb/gyromouse/gyromouse
LOG=/data/adb/gyromouse/gyromouse.log

until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 5; done
sleep 10    # lascia a joycond il tempo di creare il controller dei Joy-Con

while true; do
    "$BIN" -a "com.nvidia.geforcenow,com.limelight,com.cloudpad.stream,com.asobi.droid,com.gameblabla.chiaki" > "$LOG" 2>&1
    echo "$(date '+%F %T') gyromouse terminato (codice $?), riavvio tra 5 s" >> "$LOG.restart"
    sleep 5
done

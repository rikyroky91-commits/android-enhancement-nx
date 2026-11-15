#!/system/bin/sh
# Servizio Magisk (service.d): avvia joycond con il filtro jcfix (vibrazione Joy-Con in
# GeForce NOW & co.). Il linker dei processi vendor accetta librerie solo da /vendor:
# si monta un tmpfs su /vendor/lost+found (sparisce allo spegnimento) e ci si copia la libreria.
# Se joycond con il filtro termina 3 volte di fila entro 30 s, si torna al servizio originale.

LIB_SRC=/data/adb/jcfix/libjcfix.so
LIBDIR=/vendor/lost+found
BIN=/vendor/bin/hw/android.hardware.nintendo.joycond-service
LOG=/data/adb/jcfix/jcfix.log

log() { echo "$(date '+%F %T') $*" >> "$LOG"; }

fallback() {
    log "ritorno a joycond originale: $1"
    start vendor.joycond-default
    exit 1
}

[ -f "$LIB_SRC" ] || fallback "libreria mancante"
if ! mountpoint -q "$LIBDIR"; then
    mount -t tmpfs -o mode=0755 tmpfs "$LIBDIR" || fallback "mount tmpfs fallito"
fi
cp "$LIB_SRC" "$LIBDIR/libjcfix.so" && chmod 644 "$LIBDIR/libjcfix.so" || fallback "copia libreria fallita"

stop vendor.joycond-default
sleep 1
log "avvio joycond con jcfix"
: > /data/adb/jcfix/jcfix_debug.log   # log del filtro: si riparte vuoto a ogni avvio

fails=0
while true; do
    started=$(cut -d. -f1 /proc/uptime)
    JCFIX_LOG=/data/adb/jcfix/jcfix_debug.log LD_PRELOAD="$LIBDIR/libjcfix.so" "$BIN"
    rc=$?
    ran=$(( $(cut -d. -f1 /proc/uptime) - started ))
    log "joycond terminato (codice $rc) dopo ${ran}s"
    if [ $ran -lt 30 ]; then fails=$((fails + 1)); else fails=0; fi
    [ $fails -ge 3 ] && fallback "3 chiusure ravvicinate"
    sleep 2
done

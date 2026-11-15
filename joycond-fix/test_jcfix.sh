#!/system/bin/sh
# Test manuale: avvia joycond con jcfix; se il controller non ricompare entro 20 s
# ripristina il servizio originale. Nulla di permanente: il mount tmpfs sparisce al riavvio.
LIBDIR=/vendor/lost+found
BIN=/vendor/bin/hw/android.hardware.nintendo.joycond-service
LOG=/data/local/tmp/jcfix_test.log

controller_ok() {
    for e in /sys/class/input/event*; do
        grep -q "Combined Joy-Cons" "$e/device/name" 2>/dev/null && return 0
    done
    return 1
}

rollback() {
    echo "ROLLBACK: $1"
    pkill -f "$BIN"
    sleep 1
    start vendor.joycond-default
    sleep 5
    controller_ok && echo "servizio originale ripristinato, controller OK" || echo "ATTENZIONE: controller non ancora visibile"
    exit 1
}

if ! mountpoint -q "$LIBDIR"; then
    mount -t tmpfs -o mode=0755 tmpfs "$LIBDIR" || { echo "mount fallito"; exit 1; }
fi
cp /data/local/tmp/libjcfix.so "$LIBDIR/libjcfix.so"
chmod 644 "$LIBDIR/libjcfix.so"
chcon u:object_r:vendor_file:s0 "$LIBDIR/libjcfix.so"

stop vendor.joycond-default
sleep 1
LD_PRELOAD="$LIBDIR/libjcfix.so" "$BIN" > "$LOG" 2>&1 &
PID=$!
echo "joycond con jcfix avviato, pid=$PID"

i=0
while [ $i -lt 20 ]; do
    sleep 1
    kill -0 $PID 2>/dev/null || rollback "il processo e' terminato ($(tail -3 $LOG))"
    controller_ok && break
    i=$((i+1))
done
controller_ok || rollback "controller non ricomparso in 20 s"

grep -q libjcfix /proc/$PID/maps && echo "libjcfix caricata nel processo" || rollback "libreria non caricata"
echo "OK: controller presente con jcfix"

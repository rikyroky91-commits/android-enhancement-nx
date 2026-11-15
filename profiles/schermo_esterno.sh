#!/system/bin/sh
# Servizio Magisk (service.d): elimina lo "schermo esterno fantasma".
# Bug: a volte, staccando gli occhiali RayNeo (o la TV), il driver del display non aggiorna
# il suo stato interno: il kernel segna l'uscita come offline, ma alla domanda di HWC
# risponde ancora "collegato" con i dati del vecchio schermo. Android tiene quindi uno
# "Schermo HDMI" inesistente (le app come Asobi ci mandano il video -> immagine rovinata).
# Rimedio (provato il 2026-11-07): forzare per un attimo l'uscita su "scollegato"
# (debugfs tegradc.1/hotplug = -1), poi tornare al rilevamento normale (0).
# Interviene solo se il kernel dice offline E Android vede ancora uno schermo esterno.

STATE=/sys/class/switch/tegradc.1/state
HOTPLUG=/sys/kernel/debug/tegradc.1/hotplug
LOG=/data/adb/gyromouse/schermo_esterno.log

until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 5; done
sleep 20

log() {
    [ -f "$LOG" ] && [ "$(stat -c %s "$LOG")" -gt 32768 ] && mv "$LOG" "$LOG.old"
    echo "$(date '+%F %T') $*" >> "$LOG"
}
kernel_offline() { [ "$(cat $STATE 2>/dev/null)" = offline ]; }
android_has_external() { dumpsys display 2>/dev/null | grep -q 'uniqueId="local:1"'; }

fix_if_ghost() {
    kernel_offline || return 0
    android_has_external || return 0
    [ -w "$HOTPLUG" ] || { log "fantasma rilevato ma $HOTPLUG non disponibile"; return 0; }
    log "schermo esterno fantasma ($1): forzo il distacco"
    echo -1 > "$HOTPLUG"
    sleep 4
    echo 0 > "$HOTPLUG"
    sleep 2
    if android_has_external; then log "  ancora presente dopo la correzione"; else log "  rimosso"; fi
}

log "avvio servizio"
prev=$(cat $STATE 2>/dev/null)
fix_if_ghost "all'avvio"
tick=0
while true; do
    cur=$(cat $STATE 2>/dev/null)
    if [ "$cur" = offline ] && [ "$prev" != offline ]; then
        # appena staccato: controllo dopo 5 s e dopo 15 s
        sleep 5;  fix_if_ghost "5 s dopo il distacco"
        sleep 10; fix_if_ghost "15 s dopo il distacco"
        tick=0
    fi
    prev=$cur
    tick=$((tick + 1))
    if [ $tick -ge 20 ]; then     # ~ ogni 60 s: controllo di sicurezza
        fix_if_ghost "controllo periodico"
        tick=0
    fi
    sleep 3
done

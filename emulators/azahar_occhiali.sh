#!/system/bin/sh
# Servizio Magisk (service.d): adatta il layout di Azahar/AzaharPlus allo schermo collegato.
#  - occhiali RayNeo (EDID TCL 0x03d4): Switch = schermo inferiore touch,
#    occhiali = schermo superiore in 3D affiancato
#  - altro schermo esterno (es. TV): layout normale, 3D spento
#  - nessuno schermo esterno: layout normale
# Lo scambio schermi Azahar lo tiene nelle SharedPreferences (EmulationMenuSettings_SwapScreens),
# che si possono modificare solo ad app chiusa.
#
# Versione 2 (2026-11-07): MAI chiudere l'emulatore se c'e' una partita aperta (anche in pausa
# in sottofondo), altrimenti si perdono i progressi non salvati. Il layout si applica:
#  - subito, se l'emulatore non e' aperto (cosi' e' gia' pronto quando lo avvii);
#  - se l'app e' aperta in sottofondo SENZA partita, la chiude e applica;
#  - se c'e' una partita aperta o l'app e' in primo piano, aspetta che tu esca.
# Consiglio: collega gli occhiali PRIMA di aprire l'emulatore (anche per il blocco dell'audio).

# AzaharPlus (io.github.lime3ds.android) o Azahar (org.azahar_emu.azahar): si usa quello installato.
pick_pkg() {
    for p in io.github.lime3ds.android org.azahar_emu.azahar; do
        [ -d "/data/data/$p" ] && { PKG=$p; PREFS=/data/data/$p/shared_prefs/${p}_preferences.xml; return 0; }
    done
    return 1
}
pick_pkg
CONF=/storage/emulated/0/Azhar/config/config.ini
SWAP_KEY=EmulationMenuSettings_SwapScreens
DISPLAY_STATE=/sys/class/switch/tegradc.1/state
EDID=/sys/kernel/debug/tegradc.1/edid
LOG=/data/adb/gyromouse/azahar_occhiali.log

log() { echo "$(date '+%F %T') $*" >> "$LOG"; }

# Attende che la memoria interna sia montata e che Azahar abbia creato la configurazione.
until [ -f "$CONF" ]; do sleep 10; done

detect() {
    [ "$(cat "$DISPLAY_STATE" 2>/dev/null)" = offline ] && { echo none; return; }
    head -1 "$EDID" 2>/dev/null | grep -q "50 6c d4 03" && echo glasses || echo other
}

azahar_in_foreground() {
    pid=$(pidof $PKG) || return 1
    grep -qx "$pid" /dev/cpuset/top-app/cgroup.procs
}

# Partita aperta? (EmulationActivity esiste, anche in pausa/sottofondo)
partita_aperta() {
    dumpsys activity activities 2>/dev/null | grep -q "$PKG/org.citra.citra_emu.activities.EmulationActivity"
}

set_swap_pref() {
    # Protezioni (v3): mai scrivere se il file e' vuoto/incompleto o se il risultato non e' valido,
    # altrimenti si azzerano tutte le preferenze dell'emulatore (tasti, cartella dati...).
    [ -s "$PREFS" ] && grep -q "</map>" "$PREFS" || { log "preferenze assenti o incomplete: non tocco niente"; return; }
    tmp=/data/local/tmp/azahar_prefs.$$
    if grep -q "name=\"$SWAP_KEY\"" "$PREFS"; then
        sed "s|<boolean name=\"$SWAP_KEY\" value=\"[a-z]*\" />|<boolean name=\"$SWAP_KEY\" value=\"$1\" />|" "$PREFS" > "$tmp"
    else
        sed "s|</map>|    <boolean name=\"$SWAP_KEY\" value=\"$1\" />\n</map>|" "$PREFS" > "$tmp"
    fi
    if [ -s "$tmp" ] && grep -q "</map>" "$tmp" && grep -q "CITRA_DIRECTORY" "$tmp"; then
        cp -p "$PREFS" /data/adb/gyromouse/azahar_prefs_ultimo_backup.xml
        # cat > mantiene proprietario e contesto SELinux del file originale
        cat "$tmp" > "$PREFS"
    else
        log "modifica preferenze non valida: annullata"
    fi
    rm -f "$tmp"
}

# Spegnimento/riavvio in corso o avvio non completato: non toccare niente
sistema_stabile() {
    [ "$(getprop sys.boot_completed)" = 1 ] && [ -z "$(getprop sys.shutdown.requested)" ]
}

# Scrive il layout (l'app DEVE essere gia' chiusa)
apply() {
    # 3D (affiancato a piena larghezza, render_3d=2) SOLO con gli occhiali e solo sul
    # secondo schermo; in tutti gli altri casi 3D spento (render_3d=0).
    case "$1" in
        glasses) layout=1; swap=true;  r3d=2; which=3 ;;
        other)   layout=2; swap=false; r3d=0; which=0 ;;
        *)       layout=2; swap=false; r3d=0; which=0 ;;
    esac
    sed -i -e "s/^layout_option = .*/layout_option = $layout/" \
           -e "s/^swap_screen = .*/swap_screen = $swap/" \
           -e "s/^render_3d = .*/render_3d = $r3d/" \
           -e "s/^render_3d_which_display = .*/render_3d_which_display = $which/" "$CONF"
    set_swap_pref $swap
    log "schermo esterno: $1 -> layout=$layout swap=$swap 3d=$r3d 3d_display=$which"
}

applied=""
waiting=""
while true; do
    pick_pkg
    cur=$(detect)
    if [ "$cur" != "$applied" ] && sistema_stabile; then
        if ! pidof $PKG > /dev/null; then
            apply "$cur"; applied=$cur; waiting=""
        elif azahar_in_foreground || partita_aperta; then
            # partita aperta o app in uso: non tocco niente, riprovo quando esci
            [ "$waiting" != "$cur" ] && log "schermo esterno: $cur -> in attesa (emulatore aperto, non lo chiudo)"
            waiting=$cur
        else
            # app solo in cache, nessuna partita: chiuderla e' sicuro
            am force-stop $PKG
            sleep 1
            apply "$cur"; applied=$cur; waiting=""
        fi
    fi
    sleep 3
done

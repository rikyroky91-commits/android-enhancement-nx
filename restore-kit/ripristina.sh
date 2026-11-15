#!/bin/bash
# Rimette sulla Switch quello che e' stato salvato da crea_kit.sh.
# Uso (Git Bash, Switch collegata via ADB, Magisk gia' attivo):
#   bash ripristina.sh kit_AAAA-MM-GG_HHMM --prova     mostra cosa farebbe, senza toccare nulla
#   bash ripristina.sh kit_AAAA-MM-GG_HHMM             esegue il ripristino
# Tocca solo /data/adb/{service.d,gyromouse,jcfix}, le proprieta' salvate e le app mancanti.
# NON tocca giochi, salvataggi o la scheda SD. La voce hekate va rimessa solo se serve:
# la copia e' in config/android.ini. Alla fine serve un riavvio.
set -e
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")"
KIT="${1:?indica la cartella del kit}"
PROVA=0; [ "$2" = "--prova" ] && PROVA=1
[ -f "$KIT/kit_adb.tar" ] || { echo "Kit non valido: $KIT"; exit 1; }
( cd "$KIT" && sha256sum -c --quiet SHA256.txt ) || { echo "Kit danneggiato: mi fermo"; exit 1; }
echo "Kit integro."

A="${ADB:-adb}"   # adb (variabile ADB per indicarne un altro)
S=$("$A" devices -l | grep Switch_OLED | grep -v offline | awk '{print $1}' | head -1)
[ -n "$S" ] || { echo "Switch non collegata via ADB"; exit 1; }
adbs() { "$A" -s "$S" "$@"; }

adbs shell 'su -c "id -u"' | tr -d '\r' | grep -qx 0 || {
    echo "Root non disponibile: prima reinstalla Magisk dalla recovery."; exit 1; }

# App da installare se mancano: nome file -> pacchetto
installa() {
    f=$(ls "$KIT"/app/$1 2>/dev/null | sort | tail -1)
    [ -n "$f" ] || return 0
    if adbs shell pm list packages | tr -d '\r' | grep -qx "package:$2"; then
        echo "  gia' presente: $2"
    elif [ $PROVA = 1 ]; then
        echo "  installerei: $(basename "$f")"
    else
        echo "  installo: $(basename "$f")"
        adbs install "$(cygpath -m "$PWD/$f")" > /dev/null
    fi
}
echo "App:"
installa 'RiquadriSwitch.apk' local.switchtiles
installa 'cloudpad-vibrazione-*.apk' com.cloudpad.stream
installa 'azaharplus-*.apk' io.github.lime3ds.android
installa 'RetroArch_*.apk' com.retroarch.aarch64

echo "Proprieta':"
while IFS= read -r l; do
    k=$(echo "$l" | sed -n 's/^\[\(.*\)\]: \[.*\]$/\1/p')
    v=$(echo "$l" | sed -n 's/^\[.*\]: \[\(.*\)\]$/\1/p')
    [ -n "$k" ] || continue
    echo "  $k = $v"
    [ $PROVA = 1 ] || adbs shell "su -c 'setprop $k $v'"
done < "$KIT/config/proprieta.txt"

echo "Servizi e programmi: $(wc -l < "$KIT/contenuto.txt") voci in /data/adb"
if [ $PROVA = 1 ]; then
    echo "Prova finita: non ho modificato nulla."
    exit 0
fi
adbs push "$(cygpath -m "$PWD/$KIT/kit_adb.tar")" /data/local/tmp/kit_adb.tar > /dev/null
adbs shell 'su -c "cd /data/adb && tar -xf /data/local/tmp/kit_adb.tar && chmod 755 service.d/*.sh && chown -R 0:0 service.d gyromouse jcfix && rm -f /data/local/tmp/kit_adb.tar && ls service.d"'
echo
echo "Fatto. Riavvia la Switch: i servizi ripartono da soli."
echo "Poi apri Riquadri Switch e concedi il root se Magisk lo chiede."

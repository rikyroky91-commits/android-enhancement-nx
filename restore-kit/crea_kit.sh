#!/bin/bash
# Kit di ripristino Switch: salva sul PC tutto il lavoro personalizzato della console.
# Uso (Git Bash, Switch collegata via ADB):  bash crea_kit.sh
# Crea la cartella kit_AAAA-MM-GG_HHMM con servizi, programmi, configurazioni e app.
# NON salva login o token: CloudPad, Asobi e gli altri account vanno rifatti a mano.
set -e
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")"
ROOT="$(cd .. && pwd)"
A="${ADB:-adb}"   # adb (variabile ADB per indicarne un altro)
S=$("$A" devices -l | grep Switch_OLED | grep -v offline | awk '{print $1}' | head -1)
[ -n "$S" ] || { echo "Switch non collegata via ADB"; exit 1; }
adbs() { "$A" -s "$S" "$@"; }

KIT="kit_$(date +%Y-%m-%d_%H%M)"
mkdir -p "$KIT/app" "$KIT/config"
echo "Salvo in $KIT"

# 1. Servizi e programmi in /data/adb (senza registri)
adbs shell 'su -c "cd /data/adb && tar -cf /data/local/tmp/kit_adb.tar --exclude=*.log --exclude=*.log.old --exclude=*.restart service.d gyromouse jcfix && chmod 644 /data/local/tmp/kit_adb.tar"'
adbs pull /data/local/tmp/kit_adb.tar "$(cygpath -m "$PWD/$KIT/kit_adb.tar")" > /dev/null
adbs shell 'su -c "rm -f /data/local/tmp/kit_adb.tar"'

# 2. Voci di avvio hekate sulla SD
adbs shell 'su -c "cat /mnt/media_rw/5D63-7F10/bootloader/ini/android.ini"' | tr -d '\r' > "$KIT/config/android.ini"

# 3. Impostazioni utili (nessun dato personale)
adbs shell 'getprop' | tr -d '\r' | grep -E '^\[persist\.sys\.sf\.' > "$KIT/config/proprieta.txt" || true
adbs shell 'su -c "getprop"' | tr -d '\r' | grep -E '^\[persist\.vendor\.joycond\.' >> "$KIT/config/proprieta.txt" || true
adbs shell 'su -c "cat /data/data/org.lineageos.settings.device/shared_prefs/org.lineageos.settings.device_preferences.xml"' | tr -d '\r' > "$KIT/config/switch_configuration_prefs.xml"
adbs shell 'su -c "cat /data/data/io.github.lime3ds.android/shared_prefs/io.github.lime3ds.android_preferences.xml"' | tr -d '\r' > "$KIT/config/azaharplus_prefs.xml" || true
adbs shell 'cat /storage/emulated/0/Azhar/config/config.ini' | tr -d '\r' > "$KIT/config/azahar_config.ini" || true
adbs shell 'cat "/sdcard/RetroArch/config/Beetle VB/Beetle VB.opt"' | tr -d '\r' > "$KIT/config/Beetle VB.opt" || true
adbs shell 'cat "/sdcard/RetroArch/config/Beetle VB/Beetle VB.cfg"' | tr -d '\r' > "$KIT/config/Beetle VB.cfg" || true

# 4. App personalizzate e installatori
cp "$ROOT/11_riquadri/RiquadriSwitch.apk" "$KIT/app/"
cp "$ROOT"/12_cloudpad/cloudpad-vibrazione-*.apk "$KIT/app/" 2>/dev/null || true
cp "$ROOT/6_emulatori/azaharplus/azaharplus-2126.2-A-android_replaces_azahar.apk" "$KIT/app/" 2>/dev/null || true
cp "$ROOT/6_emulatori/RetroArch_aarch64-1.22.2.apk" "$KIT/app/" 2>/dev/null || true
cp "$ROOT/6_emulatori/core_vb/mednafen_vb_libretro_android.so" "$KIT/app/" 2>/dev/null || true
cp "$ROOT"/3_root_magisk/Magisk-*.zip "$ROOT"/3_root_magisk/Magisk-*.apk "$KIT/app/" 2>/dev/null || true

# 5. Controllo
tar -tf "$KIT/kit_adb.tar" > "$KIT/contenuto.txt"
( cd "$KIT" && sha256sum kit_adb.tar app/* config/* > SHA256.txt )
echo "File nel pacchetto: $(wc -l < "$KIT/contenuto.txt")"
echo "Fatto: $KIT"

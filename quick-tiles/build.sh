#!/bin/bash
# Compila l'app "Riquadri Switch" senza Gradle (Git Bash su Windows).
# Uscita: RiquadriSwitch.apk. La chiave di firma (riquadri.jks) va conservata:
# serve per installare gli aggiornamenti sopra la versione esistente.
set -e
cd "$(dirname "$0")"
SDK="${ANDROID_HOME:-$LOCALAPPDATA/Android/Sdk}"
BT=$SDK/build-tools/35.0.0
JAR=$SDK/platforms/android-34/android.jar
JDK="${JAVA_HOME:-/c/Program Files/Microsoft/jdk-17.0.20.101-hotspot}/bin"

rm -rf build && mkdir -p build/gen build/classes build/dex
"$BT/aapt2" compile --dir res -o build/res.zip
"$BT/aapt2" link -I "$JAR" --manifest AndroidManifest.xml --java build/gen \
    --min-sdk-version 30 --target-sdk-version 34 -o build/base.apk build/res.zip
"$JDK/javac" --release 11 -encoding UTF-8 -classpath "$JAR" \
    -d build/classes build/gen/local/switchtiles/R.java src/local/switchtiles/*.java
"$JDK/java" -cp "$BT/lib/d8.jar" com.android.tools.r8.D8 --release --min-api 30 \
    --lib "$JAR" --output build/dex $(find build/classes -name '*.class')
"$JDK/jar" -u -M -f build/base.apk -C build/dex classes.dex
"$BT/zipalign" -f -p 4 build/base.apk build/aligned.apk
[ -f riquadri.jks ] || "$JDK/keytool" -genkeypair -keystore riquadri.jks -storepass riquadri \
    -keypass riquadri -alias riquadri -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Riquadri Switch"
"$JDK/java" -jar "$BT/lib/apksigner.jar" sign --ks riquadri.jks --ks-pass pass:riquadri \
    --out RiquadriSwitch.apk build/aligned.apk
"$JDK/java" -jar "$BT/lib/apksigner.jar" verify RiquadriSwitch.apk && echo "OK: RiquadriSwitch.apk"

# Android (LineageOS 22.2 / Android 15) su Switch OLED con modchip

Fonte: guida ufficiale https://wiki.lineageos.org/devices/nx_tab/install/variant4/
Installazione su **SD** (la memoria interna eMMC e Horizon NON vengono toccati).

## Cosa c'è in questa cartella (hash SHA256 verificati)

| Cartella | Contenuto | Uso |
|---|---|---|
| `0_hekate_6.5.4/` | hekate 6.5.4 + Nyx 1.9.4 (zip) | aggiornamento hekate (serve ≥ 6.1.0) |
| `1_SD_da_copiare/` | `switchroot/…` + `bootloader/ini/android.ini` | va copiata nella **radice** della SD dopo il partizionamento |
| `2_da_PC_con_adb/` | LineageOS 22.2 (01/11/2026) + MindTheGapps 15 arm64 | installati dal PC con `adb sideload` |

Serve `adb` sul PC: [Android platform-tools](https://developer.android.com/tools/releases/platform-tools).

---

## ⚠️ Regola d'oro sui dati

Il partizionamento di hekate **cancella tutta la SD**. Se i file superano ~1,2 GB, hekate salva e
ripristina **solo** `bootloader/` e `payload.bin` (verificato nel codice sorgente di hekate).
Atmosphere, giochi (`Nintendo/`), emuMMC, homebrew: **tutto il resto va salvato sul PC prima**.

I salvataggi dei giochi in **sysMMC** stanno nella memoria interna e non vengono toccati.
Se usi **emuMMC**, i salvataggi stanno dentro l'emuMMC sulla SD → va salvato anche quello (vedi fase 1).

---

## Fase 0 — Controlli (5 min)
1. Joy-Con attaccati alla console, avvia Horizon (quello che usi di solito: sysMMC o emuMMC) almeno una volta.
2. Nota la capienza della SD. Android vuole **minimo 10 GB**, consigliati 16–32 GB. SD di buona qualità (A1/A2, U3).

## Fase 1 — Backup completo della SD sul PC
1. Avvia hekate → **Tools → USB Tools → SD Card** (UMS) e collega la Switch al PC con USB‑C
   *(oppure togli la SD e mettila nel lettore).*
2. Copia **tutto** il contenuto della SD in una cartella sul PC (es. `J:\Backup_SD_Switch\`).
3. **Se usi emuMMC a partizione** (in hekate → emuMMC compare "SD Raw Partition"): la partizione è
   invisibile da Windows. Salvala da hekate: **Tools → Backup eMMC**, attiva il toggle **"SD emuMMC Raw
   Partition"**, poi fai **eMMC BOOT0 & BOOT1** e **eMMC RAW GPP**. I file finiscono in `backup/` sulla SD
   (su OLED il GPP è ~58 GB: serve spazio sulla SD).
   Se usi emuMMC **a file** (cartella `emuMMC/SD00/`), basta la copia del punto 2.
4. **Consigliato (sicurezza console):** se non hai mai fatto un backup della NAND di sistema, fallo ora:
   Tools → Backup eMMC (toggle emuMMC spento) → BOOT0 & BOOT1 + RAW GPP. Tienilo al sicuro sul PC.

## Fase 2 — Aggiornare hekate
1. Con la SD montata sul PC, estrai `0_hekate_6.5.4/hekate_ctcaer_6.5.4_Nyx_1.9.4.zip` nella radice della SD
   (sovrascrivi `bootloader/`).
2. Se il tuo modchip avvia `payload.bin` dalla radice della SD: copia `hekate_ctcaer_6.5.4.bin`
   sopra a `payload.bin` (rinominandolo). Se il chip usa `bootloader/update.bin`, va lì.
3. Riavvia e controlla che hekate mostri **v6.5.4**.

## Fase 3 — Partizionamento (CANCELLA LA SD — solo dopo la Fase 1!)
1. hekate → **Tools → Partition SD Card** → OK.
2. Imposta: **Android** almeno 16 GB (minimo 10), **emuMMC** se lo usavi (stesso tipo/size di prima),
   il resto **HOS (FAT32)** per giochi/Atmosphere.
3. Se richiesto, scegli **"Dynamic: Android 13+"** (di default nelle versioni nuove).
4. **Next Step → Start**, aspetta la pausa di sicurezza, premi **Power**.
5. A fine processo scegli **SD UMS** e collega al PC.

## Fase 4 — Copia file + ripristino (con SD in UMS sul PC)
1. Copia il contenuto di `1_SD_da_copiare/` nella **radice** della SD (unisci con `bootloader/` esistente).
2. Ripristina dal backup: `atmosphere/`, `Nintendo/`, `switch/`, `emuMMC/` (se a file), ecc.
   Nota: la partizione HOS è FAT32 → file singoli > 4 GB (es. NSP/XCI) non ci stanno.
3. Espelli la SD da Windows, poi su Switch **Close**.
4. Se hai ripristinato la cartella `Nintendo/`: **Tools → Arch bit • RCM • Touch • Pkg1/2 → Fix Archive bit**
   (altrimenti i giochi possono risultare corrotti).
5. Se avevi l'emuMMC a partizione: **Tools → Restore eMMC** con toggle "SD emuMMC Raw Partition" dai file di backup.

## Fase 5 — Flash Android
1. **Tools → Partition SD Card → Flash Android 13+** (o dal messaggio di fine partizionamento).
2. Alla domanda "reboot into recovery?" → **No**. Poi **X Close**.

## Fase 6 — Dump dei Joy-Con
**Prima:** i Joy-Con devono essere stati attaccati alla console in Horizon (Fase 0).
1. Joy-Con attaccati ai binari della console.
2. Menù principale hekate → **Nyx Settings** (in basso a sinistra).
3. **Dump Joy-Con BT** (in alto a destra).
4. Deve comparire: `Found 2 out of 2 Joy-Con Pairing data!` / `Both pairing data are HOS based!` → **OK**.
   Il file viene salvato in `switchroot/joycon_mac.ini`.
5. Se fallisce: avvia Horizon con i Joy-Con attaccati, torna in hekate e rifai il dump.

Note:
- Fallo **dopo** il partizionamento (la cartella `switchroot/` verrebbe cancellata). Se l'hai già fatto prima, non è un problema: rifallo ora.
- Se in futuro ri-associ i Joy-Con in Horizon (es. ad un'altra console) e smettono di funzionare in Android, rifai il dump.

## Fase 7 — Recovery e installazione
1. hekate → **More Configs** → tieni premuto **Volume +** e tocca **LineageOS**. Rilascia solo quando vedi lo splash LineageOS.
   (Se compaiono scritte in alto a sinistra sullo splash: qualcosa è andato storto, fermati.)
2. In recovery: **Factory Reset → Format data / factory reset** (cancella SOLO i dati Android, non Horizon).
3. Torna al menù → **Apply update → Apply from ADB**. Collega la Switch al PC.
4. Dal PC (posso lanciarlo io):
   ```
   adb -d sideload "<cartella-download>\lineage-22.2-20261101-nightly-nx_tab-signed.zip"
   ```
   È normale se si ferma al 47% con `adb: failed to read command: Success/No error`.
5. **Non riavviare.** Di nuovo **Apply update → Apply from ADB**, poi:
   ```
   adb -d sideload "<cartella-download>\MindTheGapps-15.0.0-arm64-20260915_032013.zip"
   ```
   Alla schermata **Signature verification failed** → **Yes** (normale per le GApps).
6. Freccia indietro → **Reboot system now**. Il primo avvio può durare fino a 15 minuti.

⚠️ Le GApps vanno installate **prima** del primo avvio. Se avvii Android senza, serve rifare Format data e reinstallare.

## Avviare Android in futuro
hekate → **More Configs → LineageOS** (senza Volume +).
Impostazioni extra: app **Switch Configuration** in Android, e righe commentate in `bootloader/ini/android.ini`.

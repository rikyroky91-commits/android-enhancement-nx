# Kit di ripristino della Switch

Salva sul PC tutto il lavoro fatto sulla console e permette di rimetterlo.

## Quando serve

- **Aggiornamento di LineageOS:** i servizi in `/data/adb` restano al loro posto, ma Magisk si perde e quindi non partono. Di solito basta rifare il flash di Magisk dalla recovery e riavviare. Il kit qui non serve, salvo problemi.
- **Reset di Android, reinstallazione o SD nuova:** serve il kit.

## Creare un kit

Con la Switch collegata via ADB:

```bash
bash crea_kit.sh
```

Conviene rifarlo dopo ogni modifica importante.

## Ripristinare

1. Installa Android e Magisk (vedi `LEGGIMI.md` nella cartella principale).
2. Attiva il debug e collega la Switch.
3. Prova senza toccare nulla:

```bash
bash ripristina.sh kit_AAAA-MM-GG_HHMM --prova
```

4. Se l'elenco è giusto, esegui:

```bash
bash ripristina.sh kit_AAAA-MM-GG_HHMM
```

5. Riavvia la Switch e concedi il root a Riquadri Switch.

## Cosa non contiene

- Login e token (PSN, Google, GeForce NOW): vanno rifatti a mano.
- Giochi e salvataggi.

La voce di avvio hekate è in `config/android.ini`: va rimessa a mano in `bootloader/ini/` sulla SD solo se l'hai persa.

## App limitate in background

Per lasciare più memoria libera (il blocco del Wi-Fi nasceva da lì) queste app non possono lavorare in background: Chrome, Amazon Business, Prime Video, Reddit, Netflix, Stremio, Monster Hunter Outlanders. Funzionano normalmente quando le apri, ma le loro notifiche possono arrivare in ritardo.

Per riabilitarne una: Impostazioni → App → nome app → Batteria → "Ottimizzata" oppure "Senza limitazioni".

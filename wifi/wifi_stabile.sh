#!/system/bin/sh
# Servizio Magisk (service.d): Wi-Fi stabile per lo streaming (GeForce NOW, Moonlight).
#  - impedisce ad Android di cambiare rete o punto di accesso mentre e' connesso
#    (i cambi router <-> access point causavano secondi di stacco);
#  - non scollega il Wi-Fi per un'occasionale perdita di contatto col router;
#  - tiene spento il risparmio energetico del Wi-Fi quando e' connesso (Android lo
#    riattiva da solo; in standby il Wi-Fi viene comunque spento da standby_leggero);
#  - monitor: ogni 5 s un ping al router; registra solo le interruzioni (inizio,
#    durata, rete, segnale) in wifi_stabile.log.
#  - firmware Wi-Fi bloccato: avviso e riavvio pulito automatico (evita il kernel panic)
# NB: non usare "dumpsys wifi" su questa console: manda in crash il servizio Wi-Fi.

LOG=/data/adb/gyromouse/wifi_stabile.log

until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 5; done
sleep 20

log() {
    [ -f "$LOG" ] && [ "$(stat -c %s "$LOG")" -gt 65536 ] && mv "$LOG" "$LOG.old"
    echo "$(date '+%F %T') $*" >> "$LOG"
}
link_info() {
    iw dev wlan0 link 2>/dev/null | sed -n 's/.*SSID: //p; s/.*signal: //p' | tr '\n' ' '
}

log "avvio servizio"

# Riserva di memoria per le allocazioni urgenti dei driver (2026-11-07): con 16 MB il
# driver Wi-Fi e' rimasto senza memoria ("brcmf: Failed to alloc SKB"), il firmware si e'
# bloccato e un successivo spegni/riaccendi del Wi-Fi ha mandato il kernel in panic.
# NB: se il firmware Wi-Fi e' bloccato NON spegnere/riaccendere il Wi-Fi: riavviare.
[ "$(cat /proc/sys/vm/min_free_kbytes)" -lt 32768 ] && echo 32768 > /proc/sys/vm/min_free_kbytes
[ -w /proc/sys/vm/extra_free_kbytes ] && echo 24576 > /proc/sys/vm/extra_free_kbytes
log "riserva memoria: min_free=$(cat /proc/sys/vm/min_free_kbytes) KB extra_free=$(cat /proc/sys/vm/extra_free_kbytes 2>/dev/null) KB"

# Firmware Wi-Fi bloccato -> riavvio pulito (2026-11-07). Con il firmware bloccato Android
# prima o poi ricarica da solo il driver ("auto-ripristino" del Wi-Fi) e il driver brcmfmac
# va in kernel panic (__inval_cache_range in brcmf_flowring...): crash senza salvataggio.
# Un riavvio pulito (Android chiude le app e scrive i dati) e' molto piu' sicuro.
# Blocco = almeno FW_SOGLIA errori del firmware negli ultimi 120 s.
RIAVVIO_AUTO=1                  # 0 = solo avviso, nessun riavvio automatico
RIAVVIO_ATTESA=60               # secondi di preavviso a schermo acceso
FW_SOGLIA=6
FW_FLAG=/dev/wifi_fw_bloccato   # letto da standby_leggero.sh: con il flag non tocca il Wi-Fi
fw_last=0                       # timestamp dmesg dell'ultimo errore gia' contato
fw_hits=""                      # "uptime:errori" degli ultimi 120 s
fw_done=0

# Nuovi errori del firmware in dmesg dopo il timestamp $fw_last: stampa "numero ultimo_ts"
fw_new_errors() {
    dmesg 2>/dev/null | awk -v last="$fw_last" '
        /Firmware reported general error|Timeout on response for query command|Failed to alloc SKB/ {
            if (match($0, /^\[ *[0-9]+\.[0-9]+\]/)) {
                t = substr($0, RSTART + 1, RLENGTH - 2) + 0
                if (t > last) { n++; m = t }
            }
        }
        END { printf "%d %s\n", n, (n ? m : last) }'
}
screen_on() { dumpsys power 2>/dev/null | grep 'mWakefulness=' | sed -n 1p | grep -q Awake; }
fw_check() {
    set -- $(fw_new_errors)
    [ -n "$2" ] && fw_last=$2
    now_s=$(cut -d. -f1 /proc/uptime)
    [ "${1:-0}" -gt 0 ] && fw_hits="$fw_hits $now_s:$1"
    total=0; keep=""
    for h in $fw_hits; do
        [ $((now_s - ${h%:*})) -lt 120 ] && { keep="$keep $h"; total=$((total + ${h#*:})); }
    done
    fw_hits=$keep
    [ "$total" -lt "$FW_SOGLIA" ] || [ "$fw_done" = 1 ] && return
    fw_done=1
    touch $FW_FLAG
    log "FIRMWARE WI-FI BLOCCATO: $total errori in 120 s (memoria libera $(sed -n 's/MemFree: *//p' /proc/meminfo))"
    if [ "$RIAVVIO_AUTO" != 1 ]; then
        cmd notification post -S bigtext -t "Wi-Fi bloccato" wifi_fw "Il chip Wi-Fi si e' bloccato. Riavvia la console (non spegnere e riaccendere il Wi-Fi)." > /dev/null 2>&1
        return
    fi
    if screen_on; then
        cmd notification post -S bigtext -t "Wi-Fi bloccato: riavvio tra $RIAVVIO_ATTESA s" wifi_fw "Il chip Wi-Fi si e' bloccato. Salva la partita: la console si riavvia da sola tra $RIAVVIO_ATTESA secondi per evitare un crash." > /dev/null 2>&1
        sleep $RIAVVIO_ATTESA
    fi
    log "riavvio pulito per firmware Wi-Fi bloccato"
    sync
    svc power reboot wifi_fw_bloccato > /dev/null 2>&1
    sleep 90                                  # se Android non risponde: riavvio da init
    sync
    setprop sys.powerctl reboot,wifi_fw_bloccato
}
set -- $(fw_new_errors); fw_last=${2:-0}     # contano solo gli errori da adesso in poi

applied=0
ps_fix=0
down_since=0     # uptime di inizio interruzione, 0 = connessione ok
down_info=""
while true; do
    now=$(cut -d. -f1 /proc/uptime)
    if iw dev wlan0 link 2>/dev/null | grep -q '^Connected'; then
        case "$(iw dev wlan0 get power_save 2>/dev/null)" in
            *on*) iw dev wlan0 set power_save off && ps_fix=$((ps_fix + 1)) ;;
        esac
        if [ "$applied" = 0 ] || [ $((now - applied)) -ge 300 ]; then
            cmd wifi set-network-selection-config enabled enabled -a 2 > /dev/null 2>&1
            cmd wifi set-ipreach-disconnect disabled > /dev/null 2>&1
            [ "$applied" = 0 ] && log "impostazioni applicate (rete: $(link_info))"
            applied=$now
        fi
        if [ $ps_fix -ge 50 ]; then log "risparmio energetico rispento 50 volte"; ps_fix=0; fi

        # Monitor: un ping al router (2 tentativi da 1 s prima di dichiarare interruzione)
        gw=$(ip route show table wlan0 2>/dev/null | sed -n 's/^default via \([0-9.]*\).*/\1/p' | head -1)
        if [ -n "$gw" ]; then
            if ping -c 2 -W 1 -i 0.5 "$gw" > /dev/null 2>&1; then
                if [ "$down_since" != 0 ]; then
                    log "interruzione finita: durata ~$((now - down_since)) s (iniziata con $down_info)"
                    down_since=0
                fi
            elif [ "$down_since" = 0 ]; then
                down_since=$now
                down_info="rete/segnale: $(link_info)router $gw"
                log "INTERRUZIONE: il router $gw non risponde ($(link_info))"
            fi
        fi
    elif [ "$down_since" != 0 ]; then
        log "Wi-Fi scollegato durante l'interruzione (iniziata ~$((now - down_since)) s fa)"
        down_since=0
    fi
    # Firmware Wi-Fi bloccato? -> avviso e riavvio pulito (vedi fw_check)
    fw_check
    sleep 5
done

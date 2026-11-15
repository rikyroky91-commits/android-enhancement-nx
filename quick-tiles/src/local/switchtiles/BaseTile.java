package local.switchtiles;

import android.content.ComponentName;
import android.os.Handler;
import android.os.Looper;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Map;
import java.util.Set;
import java.util.WeakHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Riquadro generico: al clic esegue un comando root, poi mostra lo stato restituito.
 * Android tiene collegati al massimo 3 riquadri della stessa app: quelli senza uno stato
 * da mostrare sempre (Gyro +, Gyro -, Ibernazione) sono "attivi" (ACTIVE_TILE nel manifest)
 * e si collegano solo al tocco o quando vengono chiamati da qui. Cosi' i 3 posti restano a
 * Gyro, Overclock e Colore, che altrimenti potevano restare "Non disponibile".
 * Quando lo stato cambia, i riquadri ancora collegati vengono ridisegnati subito; Gyro + e
 * Gyro - non collegati vengono richiamati con requestListeningState.
 */
public abstract class BaseTile extends TileService {
    private static final ExecutorService WORKER = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());
    // Usati solo dal thread principale
    private static String lastStatus = "";   // ultimo stato visto da un qualsiasi riquadro
    private static final Set<BaseTile> LISTENING = Collections.newSetFromMap(new WeakHashMap<>());

    /** Comando da eseguire al clic (es. "oc toggle"). */
    abstract String clickCommand();

    /** Aggiorna aspetto del riquadro in base allo stato. */
    abstract void render(Tile t, Map<String, String> s);

    @Override
    public void onStartListening() {
        LISTENING.add(this);
        refresh("stato");
    }

    @Override
    public void onStopListening() {
        LISTENING.remove(this);
    }

    @Override
    public void onClick() {
        refresh(clickCommand());
    }

    /** Ridisegna questo riquadro con lo stato dato. */
    private void show(Map<String, String> s) {
        Tile t = getQsTile();
        if (t == null) return;
        if (s == null) {
            t.setState(Tile.STATE_UNAVAILABLE);
            t.setSubtitle("serve root");
        } else {
            render(t, s);
        }
        t.updateTile();
    }

    /** Stato cambiato: aggiorna gli altri riquadri collegati e richiama Gyro + / Gyro -. */
    private void spread(Map<String, String> s) {
        Set<Class<?>> done = new java.util.HashSet<>();
        for (BaseTile other : new ArrayList<>(LISTENING)) {
            done.add(other.getClass());
            if (other != this) other.show(s);
        }
        for (Class<?> c : new Class<?>[] {SensUpTile.class, SensDownTile.class}) {
            if (done.contains(c)) continue;
            try {
                requestListeningState(this, new ComponentName(this, c));
            } catch (Exception ignored) {
            }
        }
    }

    void refresh(final String cmd) {
        WORKER.execute(() -> {
            final Map<String, String> s = Root.run(cmd);
            main.post(() -> {
                show(s);
                String now = String.valueOf(s);
                if (s != null && !now.equals(lastStatus)) {
                    lastStatus = now;
                    spread(s);
                }
            });
        });
    }
}

package local.switchtiles;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.util.Map;

/** Schermata dell'app: spiega come aggiungere i riquadri e chiede il permesso root. */
public class MainActivity extends Activity {
    private TextView status;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);
        root.setGravity(Gravity.TOP);

        TextView info = new TextView(this);
        info.setTextSize(16);
        info.setText("Riquadri per il centro notifiche:\n"
                + "• Overclock: acceso/spento (al riavvio torna acceso)\n"
                + "• Gyro: Mouse → Stick destro → Spento\n"
                + "• Gyro + / Gyro −: sensibilità ±25%\n"
                + "• Ibernazione: standby con Wi-Fi spento subito\n\n"
                + "Per aggiungerli: apri il centro notifiche, tocca la matita (modifica) "
                + "e trascina i riquadri in alto.\n\n"
                + "Al primo uso Magisk chiede il permesso root: concedilo.");
        root.addView(info);

        Button check = new Button(this);
        check.setText("Verifica permesso root");
        check.setOnClickListener(v -> refresh());
        root.addView(check);

        status = new TextView(this);
        status.setTextSize(16);
        status.setPadding(0, pad, 0, 0);
        root.addView(status);

        setContentView(root);
        refresh();
    }

    private void refresh() {
        status.setText("Controllo…");
        final Handler main = new Handler(Looper.getMainLooper());
        new Thread(() -> {
            final Map<String, String> s = Root.run("stato");
            main.post(() -> status.setText(s == null
                    ? "Root non disponibile: concedi il permesso in Magisk, poi riprova."
                    : "Root OK.\nOverclock: " + ("1".equals(s.get("oc")) ? "acceso" : "spento")
                      + " (CPU " + s.get("cpu") + " MHz, GPU " + s.get("gpu") + " MHz)\n"
                      + "Gyro: " + (!"1".equals(s.get("enabled")) ? "spento"
                          : ("1".equals(s.get("stick")) ? "stick destro" : "mouse"))
                      + ", sensibilità mouse " + Root.num(s.get("sens"))
                      + ", stick " + Root.num(s.get("ssens"))));
        }).start();
    }
}

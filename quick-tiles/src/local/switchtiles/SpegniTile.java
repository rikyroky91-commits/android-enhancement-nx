package local.switchtiles;

import android.service.quicksettings.Tile;
import java.util.Map;

/**
 * "Ibernazione": standby profondo subito. Spegne lo schermo e chiede a standby_leggero.sh di
 * saltare i 10 minuti con il Wi-Fi acceso: Wi-Fi e Bluetooth spenti e Doze forzato da subito.
 * Alla riaccensione dello schermo torna tutto come prima. (Il nome della classe resta
 * SpegniTile per non spostare il riquadro nel pannello.)
 */
public class SpegniTile extends BaseTile {
    @Override
    String clickCommand() { return "iberna ora"; }

    @Override
    void render(Tile t, Map<String, String> s) {
        t.setState(Tile.STATE_INACTIVE);
        t.setSubtitle("Wi-Fi spento subito");
    }
}

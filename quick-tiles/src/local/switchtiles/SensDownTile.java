package local.switchtiles;

import android.service.quicksettings.Tile;
import java.util.Map;

/** Sensibilita' del gyro -25% (della modalita' attiva). */
public class SensDownTile extends BaseTile {
    @Override
    String clickCommand() { return "sens giu"; }

    @Override
    void render(Tile t, Map<String, String> s) {
        SensUpTile.renderSens(t, s);
    }
}

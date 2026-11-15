package local.switchtiles;

import android.service.quicksettings.Tile;
import java.util.Map;

/** Sensibilita' del gyro +25% (della modalita' attiva). */
public class SensUpTile extends BaseTile {
    @Override
    String clickCommand() { return "sens su"; }

    @Override
    void render(Tile t, Map<String, String> s) {
        renderSens(t, s);
    }

    static void renderSens(Tile t, Map<String, String> s) {
        boolean stick = "1".equals(s.get("stick"));
        t.setState(Tile.STATE_INACTIVE);
        t.setSubtitle((stick ? "stick " + Root.num(s.get("ssens")) : "mouse " + Root.num(s.get("sens"))));
    }
}

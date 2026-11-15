package local.switchtiles;

import android.service.quicksettings.Tile;
import java.util.Map;

/** Gyro: un tocco cicla MOUSE -> STICK DESTRO -> SPENTO. */
public class GyroTile extends BaseTile {
    @Override
    String clickCommand() { return "gyro ciclo"; }

    @Override
    void render(Tile t, Map<String, String> s) {
        if (!"1".equals(s.get("enabled"))) {
            t.setState(Tile.STATE_INACTIVE);
            t.setSubtitle("Spento");
            return;
        }
        t.setState(Tile.STATE_ACTIVE);
        String mode = "1".equals(s.get("stick")) ? "Stick destro" : "Mouse";
        t.setSubtitle(mode + ("1".equals(s.get("always")) ? " · sempre" : " · con ZL"));
    }
}

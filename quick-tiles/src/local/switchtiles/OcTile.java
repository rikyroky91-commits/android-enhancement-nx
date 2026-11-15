package local.switchtiles;

import android.service.quicksettings.Tile;
import java.util.Map;

/** Overclock acceso/spento (CPU e GPU). */
public class OcTile extends BaseTile {
    @Override
    String clickCommand() { return "oc toggle"; }

    @Override
    void render(Tile t, Map<String, String> s) {
        boolean on = "1".equals(s.get("oc"));
        t.setState(on ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
        t.setSubtitle("CPU " + s.get("cpu") + " · GPU " + s.get("gpu") + " MHz");
    }
}

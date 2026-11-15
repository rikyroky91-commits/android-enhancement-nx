package local.switchtiles;

import android.service.quicksettings.Tile;
import java.util.Map;

/** Colore del pannello OLED: Vivid -> Natural -> Basic -> Night. */
public class ColorTile extends BaseTile {
    @Override
    String clickCommand() { return "colore ciclo"; }

    @Override
    void render(Tile t, Map<String, String> s) {
        String c = s.get("color");
        String name;
        if ("0x65".equals(c)) name = "Vivid";
        else if ("0x23".equals(c)) name = "Natural";
        else if ("0x03".equals(c)) name = "Basic";
        else if ("0x43".equals(c)) name = "Night";
        else name = String.valueOf(c);
        t.setState(Tile.STATE_ACTIVE);
        t.setSubtitle(name);
    }
}

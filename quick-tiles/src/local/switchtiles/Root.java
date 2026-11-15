package local.switchtiles;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

/** Esegue come root lo script dei riquadri con un comando fisso e ne legge lo stato. */
final class Root {
    static final String SCRIPT = "/data/adb/gyromouse/tiles.sh";

    private Root() {}

    /** Ritorna lo stato (chiave=valore) o null se root negato / errore. */
    static Map<String, String> run(String cmd) {
        try {
            Process p = new ProcessBuilder("su", "-c", "sh " + SCRIPT + " " + cmd)
                    .redirectErrorStream(true).start();
            StringBuilder out = new StringBuilder();
            try (BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()))) {
                String line;
                while ((line = r.readLine()) != null) out.append(line).append('\n');
            }
            if (!p.waitFor(15, TimeUnit.SECONDS)) { p.destroy(); return null; }
            if (p.exitValue() != 0) return null;
            Map<String, String> m = new HashMap<>();
            for (String tok : out.toString().trim().split("\\s+")) {
                int eq = tok.indexOf('=');
                if (eq > 0) m.put(tok.substring(0, eq), tok.substring(eq + 1));
            }
            return m.containsKey("oc") ? m : null;
        } catch (Exception e) {
            return null;
        }
    }

    /** "1.250" -> "1.25", "12.000" -> "12" */
    static String num(String s) {
        if (s == null) return "?";
        if (s.contains(".")) s = s.replaceAll("0+$", "").replaceAll("\\.$", "");
        return s;
    }
}

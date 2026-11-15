package local.switchtiles;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

/**
 * Notifica fissa e silenziosa con le temperature della console.
 * L'app non puo' leggere i sensori: lo fa lo script root service.d/temperature.sh, che scrive
 * una riga di testo in files/temps.txt. Qui la si rilegge e si aggiorna la notifica solo se
 * e' cambiata. Il canale ha importanza bassa: nessun suono ne' comparsa a ogni aggiornamento.
 */
public class TempService extends Service {
    private static final String CHANNEL = "temperature";
    private static final String CHANNEL_ALERT = "temperature_alert";
    private static final int ID = 1;
    private static final int ID_ALERT = 2;
    private static final long PERIOD_MS = 5000;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private File file;
    private String last = "";
    private String lastAlert = "";

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            update();
            handler.postDelayed(this, PERIOD_MS);
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        NotificationChannel ch = new NotificationChannel(CHANNEL, "Temperature",
                NotificationManager.IMPORTANCE_LOW);
        ch.setShowBadge(false);
        getSystemService(NotificationManager.class).createNotificationChannel(ch);
        getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(
                CHANNEL_ALERT, "Avvisi temperatura", NotificationManager.IMPORTANCE_HIGH));
        file = new File(getFilesDir(), "temps.txt");
        try {
            file.createNewFile();   // lo crea l'app: cosi' ha proprietario e contesto giusti
        } catch (Exception ignored) {
        }
        startForeground(ID, build("Lettura dei sensori..."),
                ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        handler.post(tick);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        handler.removeCallbacks(tick);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void update() {
        String line = null, alert = null;
        try (BufferedReader r = new BufferedReader(new FileReader(file))) {
            line = r.readLine();
            alert = r.readLine();
        } catch (Exception ignored) {
        }
        if (alert == null) alert = "";
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (line != null && !line.isEmpty() && !line.equals(last)) {
            last = line;
            nm.notify(ID, build(line));
        }
        if (!alert.equals(lastAlert)) {
            if (alert.isEmpty()) nm.cancel(ID_ALERT);
            else if (lastAlert.isEmpty()) nm.notify(ID_ALERT, buildAlert(alert));
            lastAlert = alert;
        }
    }

    private Notification buildAlert(String text) {
        return new Notification.Builder(this, CHANNEL_ALERT)
                .setSmallIcon(R.drawable.ic_temp)
                .setContentTitle("Temperatura alta")
                .setContentText(text)
                .setStyle(new Notification.BigTextStyle().bigText(text))
                .setCategory(Notification.CATEGORY_ALARM)
                .build();
    }

    private Notification build(String text) {
        return new Notification.Builder(this, CHANNEL)
                .setSmallIcon(R.drawable.ic_temp)
                .setContentTitle("Temperature")
                .setContentText(text)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setShowWhen(false)
                .setCategory(Notification.CATEGORY_STATUS)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE)
                .build();
    }
}

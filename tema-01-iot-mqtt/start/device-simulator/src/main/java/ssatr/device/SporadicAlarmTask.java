package ssatr.device;

import java.util.Locale;
import java.util.concurrent.BlockingQueue;

/**
 * TASK SPORADIC: tratează alarmele de temperatură.
 *
 * Evenimentele sosesc în coada alarmEvents (puse de taskul periodic).
 * Intervalul minim între sosiri este MIN_INTERARRIVAL_MS: între două eliberări
 * ale taskului (două alarme publicate) trebuie să treacă cel puțin atât.
 * Intervalul este impus de cod: evenimentele care sosesc mai des sunt
 * respinse, ca să nu fie inundat brokerul.
 */
public class SporadicAlarmTask implements Runnable {

    static final long MIN_INTERARRIVAL_MS = 5000;

    private final String deviceId;
    private final MqttConnection mqtt;
    private final BlockingQueue<Double> alarmEvents;
    private PeriodicTelemetryTask telemetryTask;

    public SporadicAlarmTask(String deviceId, MqttConnection mqtt, BlockingQueue<Double> alarmEvents) {
        this.deviceId = deviceId;
        this.mqtt = mqtt;
        this.alarmEvents = alarmEvents;
    }

    public void setTelemetryTask(PeriodicTelemetryTask telemetryTask) {
        this.telemetryTask = telemetryTask;
    }

    @Override
    public void run() {
        long lastRelease = 0;

        while(!Thread.currentThread().isInterrupted()) {
            try {
                double temperature = alarmEvents.take();
                long now = System.currentTimeMillis();

                if(now - lastRelease < MIN_INTERARRIVAL_MS) {
                    System.out.println("Alarma refuzata! Au trecut mai putin de: " + MIN_INTERARRIVAL_MS + " ms.");
                    continue;
                }

                lastRelease = now;
                double threshold = (telemetryTask != null) ? telemetryTask.getThreshold() : 28.0;

                String message = String.format(Locale.US, "%s;%d;%.2f;%.2f", deviceId, now, temperature, threshold);
                mqtt.publish(Config.alarmTopic(deviceId), message);

            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
        }
    }
}

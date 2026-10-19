package ssatr.device;

import java.util.Locale;
import java.util.concurrent.BlockingQueue;

/**
 * TASK PERIODIC, cu perioada T = periodMs: fiecare job citește senzorul și
 * publică valoarea pe topic-ul de telemetrie.
 *
 * Dacă temperatura depășește pragul, taskul NU tratează el alarma, ci doar
 * semnalează un eveniment în coada alarmEvents. Alarma este tratată de
 * taskul sporadic.
 */
public class PeriodicTelemetryTask implements Runnable {

    private final String deviceId;
    private final TemperatureSensor sensor;
    private final MqttConnection mqtt;
    private final BlockingQueue<Double> alarmEvents;

    // volatile: sunt citite aici, dar modificate de taskul aperiodic (alt thread)
    private volatile long periodMs = 2000;
    private volatile double threshold = 28.0;

    public PeriodicTelemetryTask(String deviceId, TemperatureSensor sensor, MqttConnection mqtt,
                                 BlockingQueue<Double> alarmEvents) {
        this.deviceId = deviceId;
        this.sensor = sensor;
        this.mqtt = mqtt;
        this.alarmEvents = alarmEvents;
    }

    @Override
    public void run() {
        long nextRelease = System.nanoTime(); //setare timp de start

        while(!Thread.currentThread().isInterrupted()) {
            try {
                double temperature = sensor.read();
                long timestamp = System.currentTimeMillis();

                String message = String.format(Locale.US, "%s;%d;%.2f", deviceId, timestamp, temperature);
                mqtt.publish(Config.telemetryTopic(deviceId), message);

                if(temperature > threshold) {
                    alarmEvents.offer(temperature); //daca e prea cald, arunca valoarea in coada, nu trimite alarma!
                }

                long periodNano = periodMs * 1_000_000L;
                nextRelease += periodNano; //stabileste exact urmatoarea citire

                long waitTime = nextRelease - System.nanoTime();

                if (waitTime > 0) {
                    Thread.sleep(waitTime / 1_000_000L, (int) (waitTime % 1_000_000L)); // sleep pana ajunge la val stabilita
                } else {
                    nextRelease = System.nanoTime(); // reseteaza la timpul actual daca a trecut peste val stabilita
                }

            } catch (InterruptedException e){
                Thread.currentThread().interrupt();
                break;
            }
        }
    }

    public void setPeriodMs(long periodMs) {
        this.periodMs = periodMs;
    }

    public void setThreshold(double threshold) {
        this.threshold = threshold;
    }

    public long getPeriodMs() {
        return periodMs;
    }

    public double getThreshold() {
        return threshold;
    }
}

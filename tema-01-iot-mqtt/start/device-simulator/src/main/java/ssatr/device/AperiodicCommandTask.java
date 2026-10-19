package ssatr.device;

import java.util.Locale;
import java.util.concurrent.BlockingQueue;

/**
 * TASK APERIODIC: execută comenzile primite de la business service.
 *
 * Comenzile sosesc oricând, pe MQTT, fără un interval minim între ele, și sunt
 * puse în coada commands. Fiecare comandă eliberează un job al taskului.
 * Comenzi care trebuie suportate:
 *
 *   SET_INTERVAL <ms>       schimbă perioada T a taskului periodic
 *   SET_THRESHOLD <valoare> schimbă pragul de alarmă
 *   RESET                   readuce senzorul la valoarea inițială
 *   STATUS                  raportează perioada și pragul curente
 *
 * După executarea fiecărei comenzi, device-ul publică pe Config.statusTopic(deviceId)
 * o confirmare: "deviceId;timestamp;text", de exemplu
 * "device-1;1727600000000;OK SET_INTERVAL 1000".
 */
public class AperiodicCommandTask implements Runnable {

    private final String deviceId;
    private final TemperatureSensor sensor;
    private final MqttConnection mqtt;
    private final BlockingQueue<String> commands;
    private final PeriodicTelemetryTask periodic;

    public AperiodicCommandTask(String deviceId, TemperatureSensor sensor, MqttConnection mqtt,
                                BlockingQueue<String> commands, PeriodicTelemetryTask periodic) {
        this.deviceId = deviceId;
        this.sensor = sensor;
        this.mqtt = mqtt;
        this.commands = commands;
        this.periodic = periodic;
    }

    @Override
    public void run() {
        while (!Thread.currentThread().isInterrupted()) {
            try {
                String commandStr = commands.take();
                long timestamp = System.currentTimeMillis();
                String response = processCommand(commandStr);

                String statusMessage = String.format(Locale.US, "%s;%d;%s", deviceId, timestamp, response);
                mqtt.publish(Config.statusTopic(deviceId), statusMessage);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
        }
    }

    private String processCommand(String commandStr) {
        if (commandStr == null || commandStr.trim().isEmpty()) {
            return "Error! Command empty!";
        }

        String[] parts = commandStr.trim().split("\\s+");
        String cmd = parts[0].toUpperCase();

        try {
            switch (cmd) {
                case "SET_INTERVAL":
                    if (parts.length < 2) return "ERROR! The interval value is missing!";
                    long newInterval = Long.parseLong(parts[1]);
                    periodic.setPeriodMs(newInterval);
                    return "OK SET_INTERVAL: " + newInterval;

                case "SET_THRESHOLD":
                    if (parts.length < 2) return "ERROR! The threshold value is missing!";
                    double newThreshold = Double.parseDouble(parts[1]);
                    periodic.setThreshold(newThreshold);
                    return String.format(Locale.US, "OK SET_THRESHOLD %.2f: ", newThreshold);

                case "RESET":
                    sensor.reset();
                    return "OK RESET";

                case "STATUS":
                    return String.format(Locale.US, "OK STATUS: period=%d threshold=%.2f", periodic.getPeriodMs(), periodic.getThreshold());

                default:
                    return "ERROR! Unkwon command: " + cmd;

            }
        } catch (NumberFormatException e) {
            return "ERROR! Unknown parameter for: " + cmd;
        } catch (Exception e) {
            return "ERROR " + e.getMessage();
        }
    }
}

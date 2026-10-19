package ssatr.service;

import java.util.concurrent.BlockingQueue;

/**
 * Preia mesajele de un anumit tip ("telemetry", "alarm" sau "status") din coada
 * inbox, le salvează și actualizează dashboard-ul.
 *
 * Formatul mesajelor (câmpuri separate prin ";"):
 *   telemetry:  deviceId;timestamp;temperatura
 *   alarm:      deviceId;timestamp;temperatura;prag
 *   status:     deviceId;timestamp;text
 */
public class MessageProcessor implements Runnable {

    private final String kind;
    private final BlockingQueue<String> inbox;
    private final CsvStorage storage;
    private final Dashboard dashboard;

    public MessageProcessor(String kind, BlockingQueue<String> inbox, CsvStorage storage, Dashboard dashboard) {
        this.kind = kind;
        this.inbox = inbox;
        this.storage = storage;
        this.dashboard = dashboard;
    }

    @Override
    public void run() {
        while(!Thread.currentThread().isInterrupted()) {
            try {
                String message = inbox.take();

                storage.append(kind, message);

                String[] parts = message.split(";");
                if(parts.length < 3) {
                    System.err.println("Invalid message format ignored (" + kind + "): " + message);
                    continue;
                }

                String deviceId = parts[0];
                long timestamp = Long.parseLong(parts[1]);

                switch(kind) {
                    case "telemetry":
                        double temperature = Double.parseDouble(parts[2]);
                        dashboard.updateTelemetry(deviceId, timestamp, temperature);
                        break;

                    case "alarm":
                        if (parts.length >= 4) {
                            double tempAlarm = Double.parseDouble(parts[2]);
                            double threshold = Double.parseDouble(parts[3]);
                            dashboard.addAlarm(deviceId, timestamp);
                            System.out.printf("\n[ALARM] Device %s exceeded threshold! (Temp.: %.2f > Threshold: %.2f)\n", deviceId, tempAlarm, threshold);
                        }
                        break;

                    case "status":
                        String statusText = parts[2];
                        dashboard.updateStatus(deviceId, timestamp, statusText);
                        System.out.printf("\n[STATUS] Device %s responded: %s\n", deviceId, statusText);
                        break;

                    default:
                        System.err.println("Unknown message kind: " + kind);
                        break;
                }

            } catch(InterruptedException e) {
                Thread.currentThread().interrupt();
                break;

            } catch (Exception e) {
                System.err.println("Error processing message (" + kind + "): " + e.getMessage());
            }
        }
    }
}

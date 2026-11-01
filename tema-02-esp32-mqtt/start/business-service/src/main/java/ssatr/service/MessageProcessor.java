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
        while (!Thread.currentThread().isInterrupted()) {
            try {
                String payload = inbox.take();

                storage.append(kind, payload);

                String[] parts = payload.split(";");

                if (parts.length >= 3) {
                    String deviceId = parts[0];
                    long timestamp = Long.parseLong(parts[1]);

                    switch (kind) {
                        case "telemetry":
                            double temperature = Double.parseDouble(parts[2]);
                            dashboard.updateTelemetry(deviceId, timestamp, temperature);
                            break;

                        case "alarm":
                            double alarmTemp = Double.parseDouble(parts[2]);
                            dashboard.addAlarm(deviceId, timestamp);
                            System.out.printf("[ALARM] Device %s a depășit pragul! Temperatura: %.2f°C%n", deviceId, alarmTemp);
                            break;

                        case "status":
                            int firstSemicolon = payload.indexOf(';');
                            int secondSemicolon = payload.indexOf(';', firstSemicolon + 1);
                            String statusText = payload.substring(secondSemicolon + 1);

                            dashboard.updateStatus(deviceId, timestamp, statusText);
                            System.out.printf("[STATUS] Device %s: %s%n", deviceId, statusText);
                            break;
                    }
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            } catch (Exception e) {
                System.err.println("[Eroare MessageProcessor - " + kind + "] Mesaj invalid sărit: " + e.getMessage());
            }
        }
    }
}
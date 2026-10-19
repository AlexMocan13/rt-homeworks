package ssatr.service;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.Locale;
/**
 * Ultima stare cunoscută a fiecărui device: ultima temperatură, momentul
 * ultimului mesaj, numărul de alarme etc.
 *
 * Este actualizată de thread-urile MessageProcessor și citită de CommandConsole,
 * deci accesul trebuie sincronizat.
 */
public class Dashboard {

    private static class DeviceState {
        final String deviceId;
        double lastTemperature;
        long lastMessageTimestamp;
        int alarmCount;
        String lastStatus = "N/A";

        DeviceState(String deviceId) {
            this.deviceId = deviceId;
        }
    }

    private final Map<String, DeviceState> devices = new ConcurrentHashMap<>();

    public synchronized void updateTelemetry(String deviceId, long timestamp, double temperature) {
        DeviceState state = devices.computeIfAbsent(deviceId, DeviceState::new);
        state.lastTemperature = temperature;
        state.lastMessageTimestamp = timestamp;
    }

    public synchronized void addAlarm(String deviceId, long timestamp) {
        DeviceState state = devices.computeIfAbsent(deviceId, DeviceState::new);
        state.alarmCount++;
        state.lastMessageTimestamp = timestamp;
    }

    public synchronized void updateStatus(String deviceId, long timestamp, String statusText) {
        DeviceState state = devices.computeIfAbsent(deviceId, DeviceState::new);
        state.lastStatus = statusText;
        state.lastMessageTimestamp = timestamp;
    }

    /** Afișează un tabel cu starea tuturor device-urilor. */
    public synchronized void print() {
        if(devices.isEmpty()) {
            System.out.println("No devices connected yet.");
            return;
        }

        long now = System.currentTimeMillis();
        System.out.println("----------------------------------------------------------------------------------");
        System.out.printf("%-12s | %-10s | %-15s | %-8s | %-25s%n",
                "DEVICE", "TEMP (°C)", "LAST MESSAGE", "ALARMS", "LAST STATUS");
        System.out.println("----------------------------------------------------------------------------------");

        for(DeviceState state : devices.values()) {
            long agoSeconds = (now-state.lastMessageTimestamp)/1000;
            String lastSeen = agoSeconds + " s ago";

            System.out.printf(Locale.US, "%-12s | %-10.2f | %-15s | %-8d | %-25s%n", state.deviceId, state.lastTemperature, lastSeen, state.alarmCount, state.lastStatus);
        }
        System.out.println("----------------------------------------------------------------------------------");
    }
}

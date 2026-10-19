package ssatr.service;

import java.util.Scanner;

/**
 * Consola operatorului: citește comenzi de la tastatură.
 *
 *   show                               afișează dashboard-ul
 *   <deviceId> <COMANDA> [parametru]   trimite o comandă unui device, de exemplu
 *                                      device-1 SET_INTERVAL 1000
 *   help                               afișează comenzile
 *   exit                               oprește serviciul
 */
public class CommandConsole implements Runnable {

    private final MqttConnection mqtt;
    private final Dashboard dashboard;

    public CommandConsole(MqttConnection mqtt, Dashboard dashboard) {
        this.mqtt = mqtt;
        this.dashboard = dashboard;
    }

    @Override
    public void run() {
        Scanner scanner = new Scanner(System.in);
        printHelp();

        while (scanner.hasNextLine()) {
            String line = scanner.nextLine().trim();

            if (line.isEmpty()) {
                continue;
            } else if (line.equals("exit")) {
                break;
            } else if (line.equals("help")) {
                printHelp();
            } else if (line.equals("show")) {
                dashboard.print();
            } else {
                sendCommand(line);
            }
        }
    }

    private void sendCommand(String line) {
        String[] parts = line.split("\\s+", 2);

        if(parts.length < 2) {
            System.err.println("Invalid command format! Use: <deviceId> <COMMAND> [parameter]");
            return;
        }

        String deviceId = parts[0];
        String commandPayload = parts[1];

        try {
            mqtt.publish(Config.commandTopic(deviceId), commandPayload);
            System.out.println("Sent command to " + deviceId + ": " + commandPayload);
        } catch (Exception e) {
            System.err.println("Failed to send command to " + deviceId + ": " + e.getMessage());
        }
    }

    private void printHelp() {
        System.out.println("Commands: show | <deviceId> SET_INTERVAL <ms> | <deviceId> SET_THRESHOLD <val>");
        System.out.println("         <deviceId> RESET | <deviceId> STATUS | help | exit");
    }
}

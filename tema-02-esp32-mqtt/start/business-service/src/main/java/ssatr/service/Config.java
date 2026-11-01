package ssatr.service;

/**
 * Setările aplicației și numele topic-urilor MQTT.
 *
 * Brokerul este partajat de toți studenții. Înlocuiți STUDENT cu numele vostru
 * (fără spații sau diacritice), identic în ambele aplicații.
 */
public class Config {

    public static final String BROKER = "tcp://control.aut.utcluj.ro:11188";
    //public static final String BROKER = "tcp://broker.hivemq.com:1883";

    public static final String STUDENT = "mocan-alex";

    private static final String DEVICES = "ssatr/" + STUDENT + "/devices/";

    public static String telemetryTopic(String deviceId) {
        return DEVICES + deviceId + "/telemetry";
    }

    public static String alarmTopic(String deviceId) {
        return DEVICES + deviceId + "/alarm";
    }

    public static String commandTopic(String deviceId) {
        return DEVICES + deviceId + "/commands";
    }

    public static String statusTopic(String deviceId) {
        return DEVICES + deviceId + "/status";
    }
}

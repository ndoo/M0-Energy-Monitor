#include <SPI.h>

#include <Adafruit_WINC1500.h>
#include <Adafruit_WINC1500SSLClient.h>
#include <EmonLib_3PH.h>

#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>

// ATWINC1500 pins on Adafruit Feather M0 Wi-Fi
#define WINC_CS   8
#define WINC_IRQ  7
#define WINC_RST  4
#define WINC_EN   2
Adafruit_WINC1500 WiFi(WINC_CS, WINC_IRQ, WINC_RST);

// Wi-Fi Network
#define WLAN_SSID "CHANGEME"
#define WLAN_PASS "CHANGEME"

#define SENSORS 3
EnergyMonitor emon[SENSORS];

// EnergyMonitor Struct
typedef struct {
  uint8_t ct_pin;     // Analog pin for current transformer
  float ct_cal;       // Calibration value for current transformer (90.9 for 22 ohm burden resistor on 3.3V)
  char ct_feed[48];   // MQTT feed for Irms (leave any MQTT feed blank to not publish value)
  uint8_t vt_pin;     // Analog pin for 12VAC input
  float vt_cal;       // Calibration value for 12VAC transformer
  float vt_phasecal;  // Calibration angle for phase angle
  uint8_t vt_phase;   // Phase angle, 1/2/3
  float vt_voltage;   // Override voltage if not using 12VAC input with current transformer
  char vt_feed[48];   // MQTT feed for Vrms
  char ap_feed[48];   // MQTT feed for apparent power (Vrms * Irms)
  char rp_feed[48];   // MQTT feed for calculated real power
  char pf_feed[48];   // MQTT feed for calculated power factor
} sEmon;

// Adafruit MQTT
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  8883
#define AIO_USERNAME    "CHANGEME"
#define AIO_KEY         "CHANGEME"

// Sensors/MQTT feeds definition
sEmon sensors[SENSORS] = {
  { 0, 90.9,                AIO_USERNAME "/feeds/l1-current",
    3, 268.97, 2.0, 1, 240, AIO_USERNAME "/feeds/l1-voltage",
    AIO_USERNAME "/feeds/l1-apparent-power",
    AIO_USERNAME "/feeds/l1-real-power",
    AIO_USERNAME "/feeds/l1-power-factor"
  },
  { 1, 90.9,                AIO_USERNAME "/feeds/l2-current",
    4, 268.97, 2.0, 2, 240, AIO_USERNAME "/feeds/l2-voltage",
    AIO_USERNAME "/feeds/l2-apparent-power",
    AIO_USERNAME "/feeds/l2-real-power",
    AIO_USERNAME "/feeds/l2-power-factor"
  },
  { 2, 90.9,                AIO_USERNAME "/feeds/l3-current",
    5, 268.97, 2.0, 3, 240, AIO_USERNAME "/feeds/l3-voltage",
    AIO_USERNAME "/feeds/l3-apparent-power",
    AIO_USERNAME "/feeds/l3-real-power",
    AIO_USERNAME "/feeds/l3-power-factor"
  }
};

Adafruit_WINC1500SSLClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);  

void setup() {

  // Serial USB
  SerialUSB.begin(115200);
  uint8_t timeout = 30;
  while (timeout-- > 0 && !SerialUSB) delay(100);

  // Enable 12-bit ADC
  analogReadResolution(ADC_BITS);

  // Enable Wi-Fi
#ifdef WINC_EN
  pinMode(WINC_EN, OUTPUT);
  digitalWrite(WINC_EN, HIGH);
#endif

  for (uint8_t i = 0; i < SENSORS; i++) {
    SerialUSB.println(sensors[i].vt_pin);
    SerialUSB.println(sensors[i].ct_pin);
    emon[i].voltage(sensors[i].vt_pin, sensors[i].vt_cal, sensors[i].vt_phasecal, sensors[i].vt_phase);
    if (sensors[i].vt_voltage > 0) emon[i].voltage(sensors[i].vt_voltage);
    emon[i].current(sensors[i].ct_pin, sensors[i].ct_cal);
  }

}

void loop() {
  WiFi_connect();
  MQTT_connect();

  for (int i = 0; i < SENSORS; i++) {
    // perform measurements
    emon[i].calcVI(16, 2000);   // 2 h.c. for buffering + 14 h.c. for measuring

    if (strlen(sensors[i].ct_feed)) {
      SerialUSB.print("Publishing Irms to ");
      SerialUSB.println(sensors[i].ct_feed);
      SerialUSB.println((float)emon[i].Irms);
      Adafruit_MQTT_Publish(&mqtt, sensors[i].ct_feed).publish((float)emon[i].Irms);
    }

    if (strlen(sensors[i].vt_feed)) {
      SerialUSB.print("Publishing Vrms to ");
      SerialUSB.println(sensors[i].vt_feed);
      SerialUSB.println((float)emon[i].Vrms);
      Adafruit_MQTT_Publish(&mqtt, sensors[i].vt_feed).publish((float)emon[i].Vrms);
    }

    if (strlen(sensors[i].ap_feed)) {
      SerialUSB.print("Publishing Apparent Power to ");
      SerialUSB.println(sensors[i].ap_feed);
      SerialUSB.println((float)emon[i].apparentPower);
      Adafruit_MQTT_Publish(&mqtt, sensors[i].ap_feed).publish((float)emon[i].apparentPower);      
    }

    if (strlen(sensors[i].rp_feed)) {
      SerialUSB.print("Publishing Real Power to ");
      SerialUSB.println(sensors[i].rp_feed);
      SerialUSB.println((float)emon[i].realPower);
      Adafruit_MQTT_Publish(&mqtt, sensors[i].rp_feed).publish((float)emon[i].realPower);
    }
    
    if (strlen(sensors[i].pf_feed)) {
      SerialUSB.print("Publishing Power Factor to ");
      SerialUSB.println(sensors[i].pf_feed);
      SerialUSB.println((float)emon[i].powerFactor);
      Adafruit_MQTT_Publish(&mqtt, sensors[i].pf_feed).publish((float)emon[i].powerFactor);
    }
  }

}

void WiFi_connect() {
  while (WiFi.status() != WL_CONNECTED) {
    SerialUSB.print("Wi-Fi connecting (");
    SerialUSB.print(WLAN_SSID);
    SerialUSB.print(") ... ");
    uint8_t timeout = 100;
    WiFi.begin(WLAN_SSID, WLAN_PASS);
    while (timeout-- > 0 && WiFi.status() != WL_CONNECTED) delay(100);
    SerialUSB.println((WiFi.status() == WL_CONNECTED) ? "connected" : "timed out");
  }
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
  while (!mqtt.connected()) {
    SerialUSB.print("MQTT connecting (");
    SerialUSB.print(AIO_SERVER);
    SerialUSB.print(":");
    SerialUSB.print(AIO_SERVERPORT);
    SerialUSB.print(") ... ");
    int8_t ret;
    while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
      SerialUSB.println(mqtt.connectErrorString(ret));
      SerialUSB.println("Retrying in 5 seconds...");
      mqtt.disconnect();
      delay(5000);  // wait 5 seconds
    }
    SerialUSB.println(mqtt.connected() ? "connected" : "timed out");
  }
}

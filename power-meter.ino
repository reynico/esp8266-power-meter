#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ADS1X15.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266WiFi.h>


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Adafruit_ADS1115 ads;
const float SENSOR_CURRENT_A = 20.0;
const float CORRECTION_FACTOR = 1.6;
const float LINE_VOLTAGE = 220.0;
const int SAMPLING_DURATION_MS = 600;  // ~ 30 AC cycles on 50 Hz
const int NUM_SENSORS = 4;

const char* prometheus_server_address = "http://monitoring.home:9091/metrics/job/power";
const char* ssid = "xxx";
const char* password = "yyy";
WiFiClient client;
HTTPClient http;

void setup(void) {
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }

  display.clearDisplay();
  display.setRotation(1);
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Hello"));
  display.display();
  delay(1000);

  wifi_connect();

  //                                                                ADS1015  ADS1115
  //                                                                -------  -------
  // ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 3mV      0.1875mV (default)
  // ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  ads.setGain(GAIN_TWO);  // 2x gain   +/- 2.048V  1 bit = 1mV      0.0625mV
  // ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.5mV    0.03125mV
  // ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV
  // ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.125mV  0.0078125mV

  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS.");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("ADS Error"));
    display.display();
    for (;;)
      ;  // Don't proceed, loop forever
  }
}

void loop(void) {
  float* currents = measure();
  float sum = 0;
  float watts = 0;
  static bool printCurrent = true;

  for (int sensor = 0; sensor < NUM_SENSORS; ++sensor) {
    watts = currents[sensor] * LINE_VOLTAGE;
    Serial.printf("Sensor %d current: %f\n", sensor, currents[sensor]);
    char buffer[6];
    if (printCurrent) {
      snprintf(buffer, sizeof(buffer), "%4.1fA", currents[sensor]);
    } else {
      if (watts < 1000) {
        snprintf(buffer, sizeof(buffer), "%4dW", (int)watts);
      } else {
        snprintf(buffer, sizeof(buffer), "%4.1fk", watts / 1000);
      }
    }

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(buffer);
    sum += currents[sensor];
  }

  display.println("-----");
  char buffer[6];
  if (printCurrent) {
    snprintf(buffer, sizeof(buffer), "%4.1fA", sum);
  } else {
    watts = sum * LINE_VOLTAGE;
    if (watts < 1000) {
      snprintf(buffer, sizeof(buffer), "%4dW", (int)watts);
    } else {
      snprintf(buffer, sizeof(buffer), "%4.1fk", watts / 1000);
    }
  }
  display.println(buffer);
  display.display();
  printCurrent = !printCurrent;
  prometheus_report(currents);
}

float* measure() {
  static float currents[NUM_SENSORS];
  long start_time = millis();

  for (int sensor = 0; sensor < NUM_SENSORS; ++sensor) {
    float current = 0;
    float sum = 0;
    float max_current = 0;
    float min_current = 0;
    int samples = 0;

    while (millis() - start_time < SAMPLING_DURATION_MS) {
      int adc_value = ads.readADC_SingleEnded(sensor);
      current = ads.computeVolts(adc_value) * SENSOR_CURRENT_A;

      // Read peaks
      if (current > max_current) max_current = current;
      if (current < min_current) min_current = current;

      // Sum of Squares
      sum += sq(((max_current - min_current) / 2) * CORRECTION_FACTOR);
      samples++;
      delay(1);
    }

    // Compensate for the squares of negative half cycles.
    sum *= 2;
    // RMS equation
    currents[sensor] = sqrt(sum / samples);

    // Reset start time for each sensor
    start_time = millis();
  }
  return currents;
}

void wifi_connect() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  Serial.println("WiFi connecting...");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("WiFi");

  while (!WiFi.isConnected()) {
    delay(50);
    Serial.print(".");
    display.print(".");
    display.display();
  }

  IPAddress ip;
  ip = WiFi.localIP();
  Serial.printf("WiFi connected, IP: %s\n", ip.toString().c_str());
  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("OK!\n\n%d\n%d\n%d\n%d", ip[0], ip[1], ip[2], ip[3]);
  display.display();
  delay(500);
}

void prometheus_report(float* currents) {
  String data;
  data += "# HELP line_current_amps Current (amps) flowing through a voltage line\n";
  data += "# TYPE line_current_amps gauge\n";
  for (int sensor = 0; sensor < NUM_SENSORS; ++sensor) {
    data += "line_current_amps{line=\"line_" + String(sensor) + "\"} " + String(currents[sensor]) + "\n";
  }

  data += "# HELP line_watts Power (watts) consumed by a voltage line\n";
  data += "# TYPE line_watts gauge\n";
  for (int sensor = 0; sensor < NUM_SENSORS; ++sensor) {
    data += "line_watts{line=\"line_" + String(sensor) + "\"} " + String(currents[sensor] * LINE_VOLTAGE) + "\n";
  }

  http.begin(client, prometheus_server_address);
  int http_response_code = http.POST(data);
  http.end();
}

#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads;
const float SENSOR_CURRENT_A = 30.0;
const int SAMPLING_DURATION_MS = 300;  // ~15 AC cycles on 50 Hz
const int NUM_SENSORS = 4;

int16_t adc0, adc1, adc2, adc3;
float volts0, volts1, volts2, volts3;


void setup(void) {
  Serial.begin(115200);
  //                                                                ADS1015  ADS1115
  //                                                                -------  -------
  // ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 3mV      0.1875mV (default)
  // ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  // ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 1mV      0.0625mV
  // ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.5mV    0.03125mV
  // ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV
  // ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.125mV  0.0078125mV

  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS.");
    while (1)
      ;
  }
  ads.setGain(GAIN_TWO);
}

void loop(void) {
  measureCurrent();
}


float* measureCurrent() {
  static float currents[NUM_SENSORS];
  float squaredSums[NUM_SENSORS] = { 0 };
  long startTime = millis();
  int sampleCount = 0;

  while (millis() - startTime < SAMPLING_DURATION_MS) {
    for (int i = 0; i < NUM_SENSORS; i++) {
      int adcValue = ads.readADC_SingleEnded(i);
      float volts = ads.computeVolts(adcValue);
      squaredSums[i] += sq(volts * SENSOR_CURRENT_A);
    }
    sampleCount++;
    delay(1);  // Delay for stability
  }

  // Compensate for negative half cycles
  for (int i = 0; i < NUM_SENSORS; i++) {
    squaredSums[i] *= 2;
  }

  // RMS calculation and store current values
  for (int i = 0; i < NUM_SENSORS; i++) {
    currents[i] = sqrt(squaredSums[i] / sampleCount);
    Serial.print("V");
    Serial.print(i);
    Serial.print(": ");
    Serial.print(currents[i]);
    Serial.println("A");
  }
  Serial.println("");

  // Return the currents array
  return currents;
}

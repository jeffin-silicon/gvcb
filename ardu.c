
#include <Arduino.h>
#include <WiFi.h>
#include <DHT.h>
#include "AdafruitIO_WiFi.h"

// ---------- ADAFRUIT IO CREDENTIALS ----------
#define IO_USERNAME "jeffin106019"
#define IO_KEY      
// ---------- WIFI CREDENTIALS ----------
#define WIFI_SSID "
#define WIFI_PASS "12345678"

// ---------- HARDWARE ----------
#define DHT_PIN    4
#define BUZZER_PIN 25
#define LED_PIN    2       // Built-in LED on many ESP32 DevKit boards
#define DHT_TYPE   DHT11

// ---------- ALARM THRESHOLDS ----------
const float ON_TEMP  = 30.0;
const float OFF_TEMP = 29.0;
const float ON_HUM   = 70.0;
const float OFF_HUM  = 65.0;

// ---------- SENSOR DATA ----------
struct SensorData {
  float temp;
  float humidity;
};

DHT dht(DHT_PIN, DHT_TYPE);

// ---------- ADAFRUIT IO ----------
AdafruitIO_WiFi io(
  IO_USERNAME,
  IO_KEY,
  WIFI_SSID,
  WIFI_PASS
);

AdafruitIO_Feed *tempFeed = io.feed("temperature");
AdafruitIO_Feed *humFeed = io.feed("humidity");
AdafruitIO_Feed *controlFeed = io.feed("control");

// ---------- FREERTOS QUEUES ----------
QueueHandle_t displayQueue;
QueueHandle_t alertQueue;

// ---------- SHARED STATES ----------
volatile bool tempAlarm = false;
volatile bool humAlarm = false;
volatile bool manualControl = false;

// ---------- OUTPUT CONTROL ----------
void updateOutputs() {
  bool outputOn = manualControl || tempAlarm || humAlarm;

  digitalWrite(LED_PIN, outputOn ? HIGH : LOW);
  digitalWrite(BUZZER_PIN, outputOn ? HIGH : LOW);
}

// ---------- DASHBOARD BUTTON CALLBACK ----------
void handleControl(AdafruitIO_Data *data) {
  int command = data->toInt();

  manualControl = (command == 1);

  Serial.print("Dashboard button: ");
  Serial.println(manualControl ? "ON" : "OFF");

  updateOutputs();
}

// ---------- SENSOR TASK ----------
void sensorTask(void *parameter) {
  SensorData d;
  unsigned long lastUpload = millis() - 10000;

  while (true) {
    d.temp = dht.readTemperature();
    d.humidity = dht.readHumidity();

    if (!isnan(d.temp) && !isnan(d.humidity)) {
      xQueueOverwrite(displayQueue, &d);
      xQueueOverwrite(alertQueue, &d);

      // Upload readings every 10 seconds
      if (millis() - lastUpload >= 10000) {
        lastUpload = millis();

        if (io.status() == AIO_CONNECTED) {
          tempFeed->save(d.temp);
          humFeed->save(d.humidity);

          Serial.println("Temperature and humidity upload requested");
        } else {
          Serial.println("Adafruit IO not connected");
        }
      }
    } else {
      Serial.println("DHT11 reading failed - check wiring");
    }

    vTaskDelay(pdMS_TO_TICKS(2500));
  }
}

// ---------- DISPLAY TASK ----------
void displayTask(void *parameter) {
  SensorData d;

  while (true) {
    if (xQueueReceive(displayQueue, &d, portMAX_DELAY) == pdTRUE) {
      Serial.printf(
        "Temperature: %.1f C | Humidity: %.1f %%\n",
        d.temp,
        d.humidity
      );
    }
  }
}

// ---------- AUTOMATIC ALARM TASK ----------
void alertTask(void *parameter) {
  SensorData d;
  bool localTempAlarm = false;
  bool localHumAlarm = false;

  while (true) {
    if (xQueueReceive(alertQueue, &d, portMAX_DELAY) == pdTRUE) {

      // Temperature hysteresis
      if (d.temp > ON_TEMP) {
        localTempAlarm = true;
      } else if (d.temp < OFF_TEMP) {
        localTempAlarm = false;
      }

      // Humidity hysteresis
      if (d.humidity > ON_HUM) {
        localHumAlarm = true;
      } else if (d.humidity < OFF_HUM) {
        localHumAlarm = false;
      }

      tempAlarm = localTempAlarm;
      humAlarm = localHumAlarm;

      updateOutputs();

      Serial.printf(
        "Temperature alarm: %s | Humidity alarm: %s\n",
        localTempAlarm ? "ON" : "OFF",
        localHumAlarm ? "ON" : "OFF"
      );
    }
  }
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("ESP32 DHT11 Monitor and Dashboard LED Control");

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();

  // Create queues
  displayQueue = xQueueCreate(1, sizeof(SensorData));
  alertQueue = xQueueCreate(1, sizeof(SensorData));

  if (displayQueue == NULL || alertQueue == NULL) {
    Serial.println("ERROR: Queue creation failed!");
    while (true) {
      delay(1000);
    }
  }

  // Register dashboard button callback before connecting
  controlFeed->onMessage(handleControl);

  // Start FreeRTOS tasks
  if (xTaskCreate(sensorTask, "Sensor", 4096, NULL, 2, NULL) != pdPASS ||
      xTaskCreate(displayTask, "Display", 3072, NULL, 1, NULL) != pdPASS ||
      xTaskCreate(alertTask, "Alert", 3072, NULL, 3, NULL) != pdPASS) {
    Serial.println("ERROR: Task creation failed!");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("Sensor, display and alarm tasks started.");
  Serial.println("Connecting to Adafruit IO...");

  io.connect();
}

// ---------- MAIN LOOP ----------
void loop() {
  // Services Wi-Fi, MQTT, and incoming dashboard commands
  io.run();

  static unsigned long lastStatus = 0;

  if (millis() - lastStatus >= 5000) {
    lastStatus = millis();

    Serial.print("Wi-Fi status: ");
    Serial.print(WiFi.status());

    Serial.print(" | Adafruit IO status: ");
    Serial.println(io.status());
  }

  vTaskDelay(pdMS_TO_TICKS(10));
}

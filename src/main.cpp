#include <Arduino.h>
#include <WiFi.h>
#include "time.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "esp_sleep.h"

// wifi credentials file (optional)
#if __has_include("wifi_credits.h")
#include "wifi_credits.h"
#else
const char *ssid = "ssid_of_your_wifi";
const char *password = "your_wifi_password";
#endif

#define LED_PIN GPIO_NUM_4
#define FEED_BUTTON_PIN 3 // GPIO RTC capable

// Configuration MQTT (ThingsBoard)
const char *mqtt_server = "192.168.1.14";
const int mqtt_port = 1883;
const char *mqtt_client_id = "ESP32C3_Client";
const char *mqtt_topic = "v1/feedfish/esp32/";
WiFiClient espClient;
PubSubClient client(espClient);

// battery level
const int BATTERY_ADC_PIN = 0;     // GPIO0 (ADC1_CH0)
const float R1 = 85000.0;          // 100k
const float R2 = 100000.0;         // 100k
const float ADC_REF_VOLTAGE = 3.3; // référence ADC
const int ADC_RESOLUTION = 4095;   // 12 bits sur ESP32

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR bool wifiFailed = false;
RTC_DATA_ATTR bool mqttFailed = false;
RTC_DATA_ATTR bool syncTimeFailed = false;
RTC_DATA_ATTR struct tm lastSyncTime = {0};
RTC_DATA_ATTR esp_sleep_wakeup_cause_t lastWakeupReason = ESP_SLEEP_WAKEUP_UNDEFINED;
RTC_DATA_ATTR float lastBatteryLevel = 0.0;
RTC_DATA_ATTR int feedButtonState = HIGH;

IPAddress local_IP(192, 168, 1, 111);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

float getCurrentHour();
uint64_t getSecondsToNextWake(float currentHour);
struct tm timeinfo;

void setupADC()
{
  // Configure ADC for battery level measurement
  analogReadResolution(12);       // résolution 12 bits
  analogSetAttenuation(ADC_11db); // pour mesurer jusqu’à ~3.3V
}

float readBatteryLevel()
{
  // Lire la valeur de la batterie
  int adcValue = analogRead(BATTERY_ADC_PIN);
  float voltage = (adcValue * ADC_REF_VOLTAGE) / ADC_RESOLUTION;
  float batteryLevel = (voltage * (R1 + R2)) / R2; // diviseur de tension
  Serial.printf("Battery Level: %.2f V\n", batteryLevel);
  return batteryLevel;
}

void connectMQTT()
{
  if (wifiFailed)
  {
    Serial.println("WiFi connection failed, cannot connect to MQTT");
    mqttFailed = true;
  }
  client.setServer(mqtt_server, mqtt_port);
  client.setKeepAlive(60);
  if (!client.connect(mqtt_client_id))
  {
    Serial.printf("Failed to connect to MQTT server %s:%d, state: %d\n", mqtt_server, mqtt_port, client.state());
    mqttFailed = true;
  }
  mqttFailed = false;
  Serial.printf("✅ MQTT connection status: %s\n", client.connected() ? "Connected" : "Failed");
  return;
}

void disconnectMQTT()
{
  if (client.connected())
  {
    client.disconnect();
    delay(100);
    Serial.println("Disconnected from MQTT server");
  }
  else
  {
    Serial.println("MQTT client was not connected");
  }
}

void sendMQTTInfo()
{
  if (mqttFailed)
  {
    Serial.println("MQTT connection failed, cannot send info");
    return;
  }

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"time\":\"%04d-%02d-%02d %02d:%02d:%02d\",\"wakeup_reason\":%d,\"battery\":%.2f}",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
           lastWakeupReason,
           lastBatteryLevel);

  Serial.printf("Sending MQTT info: %s\n", payload);

  if (client.publish(mqtt_topic, payload))
  {
    for (uint8_t i = 0; i < 10; ++i)
    {
      client.loop();
      delay(5);
    }

    Serial.println("✅ MQTT info sent successfully");
  }
  else
  {
    Serial.println("Failed to send MQTT info");
    mqttFailed = true;
  }
}

void syncTime()
{
  if (wifiFailed)
  {
    Serial.println("WiFi connection failed, cannot synchronize time");
    syncTimeFailed = true;
    return;
  }
  Serial.println("Synchronizing time...");
  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", "WET0WEST,M3.5.0/1,M10.5.0/2", 1);
  tzset();
  int retries = 0;
  while (time(nullptr) < 100000 && retries++ < 200)
    delay(500);

  getLocalTime(&timeinfo);
  Serial.printf("Current time: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  if (retries >= 200)
  {
    Serial.println("Failed to synchronize time");
    syncTimeFailed = true;
    return;
  }
  syncTimeFailed = false;
  Serial.println("✅ Time synchronized");
}

void connectToWiFi()
{
  WiFi.config(local_IP, gateway, subnet, IPAddress(8, 8, 8, 8), IPAddress(8, 8, 4, 4));
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000)
  {
    Serial.println("Connecting to WiFi...");
    switch (WiFi.status())
    {
    case WL_CONNECTED:
      Serial.println("Connected");
      break;
    case WL_NO_SSID_AVAIL:
      Serial.println("SSID not available");
      break;
    case WL_CONNECT_FAILED:
      Serial.println("Connection failed");
      break;
    case WL_IDLE_STATUS:
      Serial.println("Idle");
      break;
    case WL_DISCONNECTED:
      Serial.println("Disconnected");
      break;
    default:
      Serial.println("Unknown status");
    }
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n✅ Connected to WiFi!");
    WiFi.printDiag(Serial);
    Serial.println(WiFi.localIP());
    wifiFailed = false;
  }
  else
  {
    Serial.println("\n❌ Failed to connect to WiFi");
    wifiFailed = true;
  }
}
void disconnectFromWiFi()
{
  WiFi.disconnect();
  Serial.println("Disconnected from WiFi");
}

float getCurrentHour()
{
  return timeinfo.tm_hour + (timeinfo.tm_min / 60.0);
}

uint64_t getSecondsToNextWake(float currentHour)
{
  // Cibles : 7.0 et 19.0
  float nextWake;
  float delta;

  if (currentHour < 7.0)
  {
    delta = 7.0 - currentHour;
    nextWake = delta > 3.0 ? 7.0 : 19.0;
  }
  else if (currentHour < 19.0)
  {
    delta = 19.0 - currentHour;
    nextWake = delta > 3.0 ? 19.0 : 7.0 + 24.0; // lendemain 7h
  }
  else
  {
    delta = (24.0 - currentHour) + 7.0;
    nextWake = delta > 3.0 ? 7.0 + 24.0 : 19.0 + 24.0; // lendemain
  }

  float seconds = (nextWake - currentHour) * 3600.0;
  return round(seconds);
}

void goToSleep(uint64_t seconds);
void setLEDState(uint8_t state)
{

  gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = (1ULL << LED_PIN);
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io_conf);

  gpio_hold_dis(LED_PIN);
  gpio_set_level(LED_PIN, state);

  if (state == HIGH)
  {
    gpio_hold_en(LED_PIN);
  }
  else
  {
    gpio_hold_dis(LED_PIN);
  }
  Serial.printf("LED state set to: %s\n", state == HIGH ? "ON" : "OFF");
}

void goToSleep(uint64_t seconds)
{
  esp_deep_sleep_enable_gpio_wakeup(1ULL << FEED_BUTTON_PIN, feedButtonState ? ESP_GPIO_WAKEUP_GPIO_LOW : ESP_GPIO_WAKEUP_GPIO_HIGH);
  Serial.printf("Enabling GPIO wakeup on feed button %s", feedButtonState == HIGH ? "LOW" : "HIGH");
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  Serial.printf("Going to sleep for %f hours\n", (uint64_t)seconds / 3600.0);
  Serial.flush();
  esp_deep_sleep_start();
}

void wakeUpRaison(esp_sleep_wakeup_cause_t wakeup_reason)
{
  switch (wakeup_reason)
  {
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.println("Woken up by timer");
    break;
  case ESP_SLEEP_WAKEUP_GPIO:
    Serial.println("Woken up by GPIO");
    break;
  default:
    Serial.printf("Woken up by unknown reason: %d\n", wakeup_reason);
    break;
  }
}

void setup()
{
  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = 1ULL << FEED_BUTTON_PIN;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_config(&io_conf);
  feedButtonState = gpio_get_level((gpio_num_t)FEED_BUTTON_PIN);

  Serial.begin(115200);
  delay(2000);
  ++bootCount;
  Serial.println("Starting up...");
  Serial.printf("Boot count: %d\n", bootCount);
  Serial.printf("***************************************************\n");
  Serial.printf("Last sync time: %02d:%02d:%02d\n", lastSyncTime.tm_hour, lastSyncTime.tm_min, lastSyncTime.tm_sec);
  Serial.printf("Last wakeup reason: \n");
  wakeUpRaison(lastWakeupReason);
  Serial.printf("feedButtonState: %s\n", feedButtonState == HIGH ? "HIGH" : "LOW");
  Serial.printf("***************************************************\n");
  setupADC();
  lastBatteryLevel = readBatteryLevel();
  delay(900);
  connectToWiFi();
  syncTime();
  lastSyncTime = timeinfo;
  lastWakeupReason = esp_sleep_get_wakeup_cause();
  connectMQTT();
  sendMQTTInfo();
  disconnectMQTT();
  disconnectFromWiFi();

  if (lastBatteryLevel < 3.1)
  {
    Serial.println("Battery level is low");
    while (readBatteryLevel() < 3.1)
    {
      setLEDState(HIGH);
      delay(500);
      setLEDState(LOW);
      delay(500);
    }
  }

  if (wifiFailed || syncTimeFailed)
  {
    Serial.println("WiFi connection or time synchronization failed, going to sleep immediately");
    goToSleep(600); // Sleep for 10 minute
    return;
  }

  float hour = getCurrentHour();
  Serial.printf("Current hour: %f\n", hour);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch (wakeup_reason)
  {
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.println("Woken up by timer");
    setLEDState(HIGH);
    goToSleep(getSecondsToNextWake(hour));
    break;
  case ESP_SLEEP_WAKEUP_GPIO:
    Serial.println("Woken up by GPIO");
    setLEDState(LOW);
    goToSleep(getSecondsToNextWake(hour));
    break;

  default:
    Serial.printf("Woken up by unknown reason: %d\n", wakeup_reason);
    setLEDState(HIGH);
    delay(1000); // Keep LED on for 1 second
    setLEDState(LOW);
    // goToSleep(getSecondsToNextWake(hour));
    goToSleep(20);
    break;
  }
}

void loop() {}

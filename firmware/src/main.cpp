#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

#include "configuration.h"
#include "display_task.h"
#include "root_task.h"
#include "motor_foc/motor_task.h"
#include "network/wifi_task.h"
#include "sensors/sensors_task.h"
#include "microphone/microphone_task.h"
#include "error_handling_flow/reset_task.h"
#include "led_ring/led_ring_task.h"
#include "esp_task_wdt.h"

#include "driver/temp_sensor.h"

Configuration config;

#if SK_DISPLAY
static DisplayTask display_task(0);
static DisplayTask *display_task_p = &display_task;
#else
static DisplayTask *display_task_p = nullptr;
#endif

#if SK_LEDS
static LedRingTask led_ring_task(0);
static LedRingTask *led_ring_task_p = &led_ring_task;
#else
static LedRingTask *led_ring_task_p = nullptr;
#endif

static MotorTask motor_task(1, config);

#if SK_WIFI
static WifiTask wifi_task(1);
static WifiTask *wifi_task_p = &wifi_task;
#else
static WifiTask *wifi_task_p = nullptr;

#endif

#if SK_MQTT
static MqttTask mqtt_task(1);
static MqttTask *mqtt_task_p = &mqtt_task;
#else
static MqttTask *mqtt_task_p = nullptr;

#endif

static SensorsTask sensors_task(1, &config);
static SensorsTask *sensors_task_p = &sensors_task;

#if SK_MICROPHONE
static MicrophoneTask microphone_task(1);
static MicrophoneTask *microphone_task_p = &microphone_task;
#else
static MicrophoneTask *microphone_task_p = nullptr;
#endif

static ResetTask reset_task(1, config);
static ResetTask *reset_task_p = &reset_task;

RootTask root_task(0, &config, motor_task, display_task_p, wifi_task_p, mqtt_task_p, led_ring_task_p, sensors_task_p, microphone_task_p, reset_task_p);

void initTempSensor()
{
    temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
    temp_sensor.dac_offset = TSENS_DAC_L2; // TSENS_DAC_L2 is default; L4(-40°C ~ 20°C), L2(-10°C ~ 80°C), L1(20°C ~ 100°C), L0(50°C ~ 125°C)
    temp_sensor_set_config(temp_sensor);
    temp_sensor_start();
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    LOGD("Last Packet Send Status: %d", uint8_t(status));
    LOGD(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void setup()
{
    // Disable Task Watchdog Timer for the current task (i.e., loop task)
    // esp_task_wdt_init(10, true); // 10 seconds timeout, second parameter is "true" to reset the system on timeout

    // Add the current task (loop task) to the watchdog
    // esp_task_wdt_add(NULL);

    // Disable Task Watchdog Timer for the current task (i.e., loop task)
    // esp_task_wdt_delete(NULL);

    // Optional: Disable the Task Watchdog Timer for all tasks
    // esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0)); // Core 0 IDLE task
    // esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1)); // Core 1 IDLE task
    disableCore0WDT(); // Disables the watchdog timer on Core 0
    disableCore1WDT(); // Disables the watchdog timer on Core 1
    initTempSensor();

    WiFi.mode(WIFI_STA);

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    // register data sent callback
    esp_now_register_send_cb(OnDataSent);

    // TODO: move from eeprom to ffatfs
    if (!EEPROM.begin(EEPROM_SIZE))
    {
        LOGE("Failed to start EEPROM");
    }

#if SK_DISPLAY
    LOGE("Test");
    display_task.begin();

    // Connect display to motor_task's knob state feed
    root_task.addListener(display_task.getKnobStateQueue());

#endif

#if SK_LEDS
    led_ring_task_p->begin();
#endif

    // TODO: remove this. Wait for display task init finishes
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    root_task.begin();

    if (!config.loadFromDisk())
    {
        config.saveToDisk();
    }

    root_task.loadConfiguration();

    motor_task.begin();

#if SK_WIFI
    wifi_task.addStateListener(root_task.getConnectivityStateQueue());
    wifi_task.begin();
#endif

#if SK_MQTT
    // IF WIFI CONNECTED CONNECT MQTT
    mqtt_task.addAppSyncListener(root_task.getAppSyncQueue());
    mqtt_task.begin();
#endif

    sensors_task_p->addStateListener(root_task.getSensorsStateQueue());
    sensors_task_p->begin();

#if SK_MICROPHONE
    microphone_task_p->addStateListener(root_task.getMicrophoneStateQueue());
    // Print memory information
    Serial.printf("Free heap before microphone: %d bytes\n",
                  heap_caps_get_free_size(MALLOC_CAP_8BIT));
    Serial.printf("Largest free block: %d bytes\n",
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    microphone_task_p->begin();
#endif

    reset_task_p->begin();

    // Free up the Arduino loop task
    vTaskDelete(NULL);
}

void loop()
{
}
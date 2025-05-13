#pragma once

#if SK_MQTT

#include <PubSubClient.h>
#include <WiFi.h>
#include <vector>
#include <map>

#include "logger.h"
#include "task.h"
#include "cJSON.h"
#include "../app_config.h"
#include "../events/events.h"
#include "notify/mqtt_notifier/mqtt_notifier.h"

class MqttTask : public Task<MqttTask>
{
    friend class Task<MqttTask>; // Allow base Task to invoke protected run()

public:
    MqttTask(const uint8_t task_core);
    ~MqttTask();

    QueueHandle_t getEntityStateReceivedQueue();

    void enqueueEntityStateToSend(EntityStateUpdate);
    void addAppSyncListener(QueueHandle_t queue);
    void unlock();
    cJSON *getApps();
    void handleEvent(WiFiEvent event);
    void handleCommand(MqttCommand command);
    void setSharedEventsQueue(QueueHandle_t shared_events_queue);

    bool setup(MQTTConfiguration config);

    bool reset();
    bool connect();
    bool disconnect();
    bool reconnect();
    bool init();

    MqttNotifier *getNotifier();

    void setConfig(MQTTConfiguration config);

protected:
    void run();

private:
    char hexbuffer_[9];

    std::map<std::string, EntityStateUpdate> entity_states_to_send;

    std::map<std::string, std::string> unacknowledged_ids;
    std::map<std::string, EntityStateUpdate> unacknowledged_states;

    MQTTConfiguration config_;
    bool is_config_set;
    uint8_t retry_count = 0;

    bool hass_init_acknowledged = true;

    // Manager status publishing
    unsigned long last_status_publish_time_ = 0;
    const unsigned long STATUS_PUBLISH_INTERVAL_MS = 2000; // 2 seconds

    QueueHandle_t entity_state_to_send_queue_;
    QueueHandle_t shared_events_queue;
    std::vector<QueueHandle_t> app_sync_listeners_;

    SemaphoreHandle_t mutex_app_sync_;
    WiFiClient wifi_client;
    PubSubClient mqtt_client;
    cJSON *apps;

    MqttNotifier mqtt_notifier;

    // Client lock management
    bool client_locked_ = false;
    String authorized_client_id_ = "";
    unsigned long lock_timestamp_ = 0;
    const unsigned long LOCK_TIMEOUT_MS = 30000; // 30 seconds

    // MQTT Topics
    static const char *MQTT_LOCK_REQUEST_TOPIC;
    static const char *MQTT_LOCK_RESPONSE_TOPIC;
    static const char *MQTT_MANAGER_STATUS_TOPIC;

    void callback(char *topic, byte *payload, unsigned int length);

    void publishAppSync(const cJSON *state);

    void publishEvent(WiFiEvent event);

    bool setupAndConnectNewCredentials(MQTTConfiguration config);

    // Client lock methods
    bool requestClientLock(const char *client_id);
    bool releaseClientLock(const char *client_id);
    bool isClientAuthorized(const char *client_id);
    void checkLockTimeout();
    void publishLockResponse(const char *client_id, bool success);
    void publishManagerStatus();

    void lock();
};

#else

class MqttTask
{
};

#endif

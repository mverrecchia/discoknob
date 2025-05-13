#if SK_MQTT
#include "mqtt_task.h"

static const char *MQTT_TAG = "MQTT";
const char *MqttTask::MQTT_LOCK_REQUEST_TOPIC = "smartknob/lock/request";
const char *MqttTask::MQTT_LOCK_RESPONSE_TOPIC = "smartknob/lock/response";
const char *MqttTask::MQTT_MANAGER_STATUS_TOPIC = "smartknob/manager/status";
MqttTask::MqttTask(const uint8_t task_core) : Task{"mqtt", 1024 * 8, 1, task_core}
{
    mutex_app_sync_ = xSemaphoreCreateMutex();

    entity_state_to_send_queue_ = xQueueCreate(50, sizeof(EntityStateUpdate));
    assert(entity_state_to_send_queue_ != NULL);

    mqtt_notifier = MqttNotifier();
    mqtt_notifier.setCallback([this](MqttCommand command)
                              { this->handleCommand(command); });

    // Initialize lock-related variables
    client_locked_ = false;
    authorized_client_id_ = "";
    lock_timestamp_ = 0;
}

MqttTask::~MqttTask()
{
    vQueueDelete(entity_state_to_send_queue_);
    vSemaphoreDelete(mutex_app_sync_);
}

void MqttTask::setConfig(MQTTConfiguration config)
{
    config_ = config;
}

void MqttTask::handleCommand(MqttCommand command)
{
    switch (command.type)
    {
    case RequestSetupConnect:
        if (this->setupAndConnectNewCredentials(command.body.mqtt_config))
        {
            LOGD("setupconnect");
            this->init();
        }
        break;
    case RequestConnect:
        if (this->setup(command.body.mqtt_config))
        {
            LOGD("connect");
            if (this->connect())
            {
                this->init();
            }
        }
        break;
    default:
        break;
    }
}

void MqttTask::handleEvent(WiFiEvent event)
{
    switch (event.type)
    {
    case SK_MQTT_CONNECTED:
    case SK_MQTT_CONNECTED_NEW_CREDENTIALS:
        init();
        break;
    case SK_RESET_ERROR:
        retry_count = 0;
        break;
    case SK_DISMISS_ERROR:
        break;
    default:
        break;
    }
}

void MqttTask::run()
{
    static uint32_t mqtt_pull;
    static uint32_t mqtt_push; // to prevent spam
    static uint32_t mqtt_init_interval;
    static uint32_t mqtt_keepalive;
    const uint16_t mqtt_pull_interval_ms = 50; // Reduced to 50ms for more frequent communication
    const uint16_t mqtt_push_interval_ms = 200;
    const uint16_t mqtt_keepalive_interval_ms = 5000; // Send a ping every 5 seconds

    static EntityStateUpdate entity_state_to_process_;

    static uint32_t last_mqtt_state_sent;

    static uint32_t message_count = 0;

    static bool has_been_connected = false;

    while (1)
    {
        if (is_config_set && retry_count < 3)
        {
            // Check if client lock has timed out
            checkLockTimeout();

            // Publish manager status periodically
            if (millis() - last_status_publish_time_ > STATUS_PUBLISH_INTERVAL_MS && mqtt_client.connected())
            {
                publishManagerStatus();
                last_status_publish_time_ = millis();
            }

            if (millis() - last_mqtt_state_sent > 1000 && !mqtt_client.connected() && WiFi.isConnected())
            {
                if (!has_been_connected || retry_count > 0)
                {
                    WiFiEvent event;
                    WiFiEventBody wifi_event_body;
                    wifi_event_body.error.type = MQTT_ERROR;
                    wifi_event_body.error.body.mqtt_error.retry_count = retry_count + 1;

                    event.type = SK_MQTT_CONNECTION_FAILED;
                    event.body = wifi_event_body;
                    event.sent_at = millis();
                    publishEvent(event);
                }

                disconnect();

                if (!connect())
                {
                    LOGD("Attempting to connect to MQTT");
                    retry_count++;
                    if (retry_count > 2)
                    {
                        LOGI("Retry limit reached...");
                        WiFiEvent event;
                        event.type = SK_MQTT_RETRY_LIMIT_REACHED;
                        publishEvent(event);
                    }
                    continue;
                }
                has_been_connected = true;
                retry_count = 0;
                WiFiEvent reset_error;
                reset_error.type = SK_RESET_ERROR;
                publishEvent(reset_error);
            }

            if (millis() - mqtt_pull > mqtt_pull_interval_ms)
            {
                mqtt_client.loop();
                mqtt_pull = millis();
            }

            if (xQueueReceive(entity_state_to_send_queue_, &entity_state_to_process_, 0) == pdTRUE)
            {

                if (entity_state_to_process_.changed)
                {
                    entity_states_to_send[entity_state_to_process_.app_id] = entity_state_to_process_;
                }
            }

            if (millis() - mqtt_push > mqtt_push_interval_ms)
            {
                // iterate over all items in the map and push all not pushed yet
                for (auto i : entity_states_to_send)
                {
                    if (!entity_states_to_send[i.first].sent)
                    {
                        sprintf(hexbuffer_, "%08lX", micros());
                        cJSON *json = cJSON_CreateObject();
                        cJSON_AddStringToObject(json, "id", hexbuffer_);
                        cJSON_AddStringToObject(json, "type", "state_update");
                        cJSON_AddStringToObject(json, "app_id", entity_states_to_send[i.first].app_id);
                        cJSON_AddRawToObject(json, "state", entity_states_to_send[i.first].state);

                        char *json_string = cJSON_PrintUnformatted(json);

                        unacknowledged_ids.insert(std::make_pair(hexbuffer_, "state_update"));
                        unacknowledged_states.insert(std::make_pair(hexbuffer_, entity_states_to_send[i.first]));
                        LOGD("Publishing state update to MQTT");
                        LOGD("Topic: %s", entity_states_to_send[i.first].topic);
                        LOGD("State: %s", entity_states_to_send[i.first].state);
                        mqtt_client.publish(entity_states_to_send[i.first].topic, entity_states_to_send[i.first].state);

                        cJSON_free(json_string);
                        cJSON_Delete(json);

                        last_mqtt_state_sent = millis();
                        entity_states_to_send[i.first].sent = true;
                    }
                }

                mqtt_push = millis();
            }

            if (millis() - mqtt_init_interval > 10000 && !hass_init_acknowledged)
            {
                LOGD("Attempting to initialize MQTT");
                init();
                mqtt_init_interval = millis();
            }
        }

        mqtt_notifier.loopTick();
        delay(5); // Reduced from 5ms to 1ms for more responsive MQTT handling
    }
}

bool MqttTask::setup(MQTTConfiguration config)
{
    if (is_config_set)
    {
        reset();
    }

    WiFiEvent event;
    event.type = SK_MQTT_SETUP;
    sprintf(event.body.mqtt_connecting.host, "%s", config.host);
    event.body.mqtt_connecting.port = config.port;
    sprintf(event.body.mqtt_connecting.user, "%s", config.user);
    sprintf(event.body.mqtt_connecting.password, "%s", config.password);

    config_ = config;

    wifi_client.setTimeout(10);

    mqtt_client.setClient(wifi_client);
    mqtt_client.setServer(config_.host, config_.port);
    mqtt_client.setBufferSize(2048); // ADD BUFFER SIZE TO CONFIG? NO?
    mqtt_client.setKeepAlive(30);    // Set keepalive to 30 seconds to prevent timeout
    mqtt_client.setCallback([this](char *topic, byte *payload, unsigned int length)
                            { this->callback(topic, payload, length); });

    publishEvent(event);
    is_config_set = true;
    return is_config_set;
}

bool MqttTask::setupAndConnectNewCredentials(MQTTConfiguration config)
{
    if (is_config_set)
    {
        reset();
    }
    LOGD("Attempting to connect to MQTT with new credentials");
    WiFiEvent event;
    event.type = SK_MQTT_TRY_NEW_CREDENTIALS;
    event.body.mqtt_connecting = config;
    publishEvent(event);

    wifi_client.setTimeout(15); // 30s timeout didnt work, threw error (Software caused connection abort)

    mqtt_client.setClient(wifi_client);
    mqtt_client.setServer(config.host, config.port);
    mqtt_client.setBufferSize(SK_MQTT_BUFFER_SIZE); // ADD BUFFER SIZE TO CONFIG? NO?
    mqtt_client.setKeepAlive(30);                   // Set keepalive to 30 seconds to prevent timeout
    mqtt_client.setCallback([this](char *topic, byte *payload, unsigned int length)
                            { this->callback(topic, payload, length); });

    uint32_t timeout_at = millis() + 30000;

    disconnect();
    while (!mqtt_client.connect(config.knob_id, config.user, config.password) && timeout_at > millis())
    {
        delay(10); // DO NOTHING
    }

    if (mqtt_client.connected())
    {
        event.type = SK_MQTT_CONNECTED_NEW_CREDENTIALS;
        publishEvent(event);

        config_ = config;
        is_config_set = true;
        return true;
    }

    event.type = SK_MQTT_TRY_NEW_CREDENTIALS_FAILED;
    publishEvent(event);
    return false;
}

bool MqttTask::reset()
{
    WiFiEvent event;
    event.type = SK_MQTT_RESET;

    retry_count = 0;
    is_config_set = false;
    wifi_client.flush();
    mqtt_client.disconnect();
    publishEvent(event);

    return true;
}

bool MqttTask::connect()
{
    if (config_.host == "")
    {
        LOGD("No host set");
        return false;
    }

    bool mqtt_connected = false;
    mqtt_connected = mqtt_client.connect(config_.knob_id, config_.user, config_.password);
    if (mqtt_connected)
    {
        WiFiEvent event;
        event.type = SK_MQTT_CONNECTED;
        publishEvent(event);
        LOGD("MQTT client connected");
        return true;
    }
    else
    {
        WiFiEvent event;
        event.type = SK_MQTT_CONNECTION_FAILED;
        publishEvent(event);
        LOGD("MQTT connection failed");
    }
    return false;
}

bool MqttTask::disconnect()
{
    wifi_client.flush();
    mqtt_client.disconnect();
    mqtt_client.flush();
    mqtt_client.loop();
    return true;
}

bool MqttTask::init()
{
    sprintf(hexbuffer_, "%08lX", millis());

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "id", hexbuffer_);
    cJSON_AddStringToObject(json, "mac_address", WiFi.macAddress().c_str());

    cJSON *data = cJSON_CreateObject();

    cJSON_AddStringToObject(data, "model", MODEL);
#ifdef RELEASE_VERSION
    cJSON_AddStringToObject(data, "firmware_version", RELEASE_VERSION);
#else
    cJSON_AddStringToObject(data, "firmware_version", "DEV");
#endif
    cJSON_AddStringToObject(data, "manufacturer", "Seedlabs");

    cJSON_AddItemToObject(json, "data", data);

    unacknowledged_ids.insert(std::make_pair(hexbuffer_, "init"));

    char *init_string = cJSON_PrintUnformatted(json);
    mqtt_client.publish("smartknob/init", init_string);

    // Subscribe to standard topics
    mqtt_client.subscribe("smartknob/disco");
    mqtt_client.subscribe(MQTT_LOCK_REQUEST_TOPIC);

    cJSON_free(init_string);
    cJSON_Delete(json);

    mqtt_client.loop();

    WiFiEvent mqtt_connected_event;
    mqtt_connected_event.type = SK_MQTT_INIT;
    publishEvent(mqtt_connected_event);
    return true;
}

void MqttTask::callback(char *topic, byte *payload, unsigned int length)
{
    LOGI("MQTT message received on topic: %s", topic);

    if (strcmp(topic, "smartknob/disco") == 0)
    {
        LOGI("Received message on disco topic: %.*s", length, payload);
        
        // Allocate memory for the JSON string
        char *json_str = (char *)malloc(length + 1);
        if (!json_str)
        {
            LOGW("Failed to allocate memory for disco message JSON");
            return;
        }
        
        // Copy payload to the string and add null terminator
        memcpy(json_str, payload, length);
        json_str[length] = '\0';
        
        // Parse the JSON
        cJSON *json_root = cJSON_Parse(json_str);
        free(json_str);
        
        if (!json_root)
        {
            LOGW("Failed to parse disco message JSON");
            return;
        }
        
        // Create and initialize a DiscoMessage struct
        DiscoMessage disco_msg = {false, false, 0.0f, false, 0, 0, "#000000"};
        
        // Parse rotation parameters
        cJSON *rotation = cJSON_GetObjectItem(json_root, "rotation");
        if (rotation)
        {
            cJSON *enabled = cJSON_GetObjectItem(rotation, "enabled");
            if (enabled && cJSON_IsBool(enabled))
            {
                disco_msg.rotation_enabled = cJSON_IsTrue(enabled);
            }
            
            cJSON *direction = cJSON_GetObjectItem(rotation, "direction");
            if (direction && cJSON_IsBool(direction))
            {
                disco_msg.rotation_direction = cJSON_IsTrue(direction);
            }
            
            cJSON *speed = cJSON_GetObjectItem(rotation, "speed");
            if (speed && cJSON_IsNumber(speed))
            {
                disco_msg.rotation_speed = speed->valuedouble;
            }
        }
        
        // Parse spotlights parameters
        cJSON *spotlights = cJSON_GetObjectItem(json_root, "spotlights");
        if (spotlights)
        {
            cJSON *enabled = cJSON_GetObjectItem(spotlights, "enabled");
            if (enabled && cJSON_IsBool(enabled))
            {
                disco_msg.spotlights_enabled = cJSON_IsTrue(enabled);
            }
            
            cJSON *mode = cJSON_GetObjectItem(spotlights, "mode");
            if (mode && cJSON_IsNumber(mode))
            {
                disco_msg.spotlights_mode = mode->valueint;
            }
            
            cJSON *mode_speed = cJSON_GetObjectItem(spotlights, "mode_speed");
            if (mode_speed && cJSON_IsNumber(mode_speed))
            {
                disco_msg.spotlights_mode_speed = mode_speed->valueint;
            }
            
            cJSON *color = cJSON_GetObjectItem(spotlights, "color");
            if (color && cJSON_IsString(color))
            {
                strncpy(disco_msg.spotlights_color, color->valuestring, sizeof(disco_msg.spotlights_color) - 1);
                disco_msg.spotlights_color[sizeof(disco_msg.spotlights_color) - 1] = '\0'; // Ensure null termination
            }
        }
        
        // Log the parsed data for debugging
        LOGI("Disco message parsed: rotation(enabled=%d, direction=%d, speed=%.4f), spotlights(enabled=%d, mode=%d, mode_speed=%d, color=%s)",
             disco_msg.rotation_enabled, disco_msg.rotation_direction, disco_msg.rotation_speed,
             disco_msg.spotlights_enabled, disco_msg.spotlights_mode, disco_msg.spotlights_mode_speed, disco_msg.spotlights_color);
        
        // Create a WiFiEvent for the disco message
        WiFiEvent event;
        event.type = SK_DISCO_MESSAGE;
        event.body.disco_message = disco_msg;
        
        // Publish event to notify subscribers
        publishEvent(event);
        
        // Clean up
        cJSON_Delete(json_root);
        return;
    }

    if (strcmp(topic, MQTT_LOCK_REQUEST_TOPIC) == 0)
    {
        char *json_str = (char *)malloc(length + 1);
        if (!json_str)
        {
            LOGW("Failed to allocate memory for lock request JSON");
            return;
        }

        memcpy(json_str, payload, length);
        json_str[length] = '\0';

        cJSON *json_root = cJSON_Parse(json_str);
        free(json_str);

        if (!json_root)
        {
            LOGW("Failed to parse lock request JSON");
            return;
        }
        LOGW("Lock request JSON: %s", json_str);

        cJSON *action_json = cJSON_GetObjectItem(json_root, "action");
        cJSON *client_id_json = cJSON_GetObjectItem(json_root, "clientId");

        if (!action_json || !cJSON_IsString(action_json) ||
            !client_id_json || !cJSON_IsString(client_id_json))
        {
            LOGW("Invalid lock request message format");
            cJSON_Delete(json_root);
            return;
        }

        const char *action = action_json->valuestring;
        const char *client_id = client_id_json->valuestring;

        bool success = false;

        if (strcmp(action, "lock") == 0)
        {
            success = requestClientLock(client_id);
        }
        else if (strcmp(action, "unlock") == 0)
        {
            success = releaseClientLock(client_id);
        }

        cJSON_Delete(json_root);
        publishLockResponse(client_id, success);
        return;
    }

    cJSON *json_root = cJSON_Parse((char *)payload);
    if (json_root == NULL)
    {
        LOGW("Failed to parse JSON message");
        return;
    }

    cJSON *type = cJSON_GetObjectItem(json_root, "type");
    if (type == NULL)
    {
        LOGW("Invalid message received - missing type field");
        cJSON_Delete(json_root);
        return;
    }

    char buf_[128];
    sprintf(buf_, "smartknob/%s/from_knob", WiFi.macAddress().c_str());

    if (strcmp(type->valuestring, "sync") == 0)
    {
        cJSON *json_root_ = cJSON_Parse((char *)payload);
        LOGD("sync");

        lock();
        apps = cJSON_GetObjectItem(json_root_, "apps"); //! THIS APPS OBJECT NEEDS TO BE FIXED!!! WAS CAUSING MEMORY LEAK BEFORE WHEN USING json_root instead of json_root_
        if (apps == NULL)
        {
            LOGW("Invalid message received");
            return;
        }
        unlock();

        // DELAY TO MAKE SURE APPS ARE INITIALIZED?
        delay(100);

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "type", "acknowledgement");
        cJSON_AddStringToObject(json, "data", "sync");

        char *acknowledgement_payload = cJSON_PrintUnformatted(json);
        mqtt_client.publish(buf_, acknowledgement_payload);

        cJSON_free(acknowledgement_payload);
        cJSON_Delete(json);

        publishAppSync(apps);
    }

    if (strcmp(type->valuestring, "state_update") == 0)
    {
        LOGD("state_update received");

        cJSON *app_id = cJSON_GetObjectItem(json_root, "app_id");
        cJSON *entity_id = cJSON_GetObjectItem(json_root, "entity_id");
        cJSON *new_state = cJSON_GetObjectItem(json_root, "new_state");

        if (app_id == NULL || entity_id == NULL || new_state == NULL)
        {
            LOGW("Invalid message received");
            return;
        }

        MQTTStateUpdate state_update;
        state_update.all = false;
        sprintf(state_update.app_id, "%s", app_id->valuestring);
        sprintf(state_update.entity_id, "%s", entity_id->valuestring);
        sprintf(state_update.state, "%s", "");

        char *state_string = cJSON_PrintUnformatted(new_state);
        sprintf(state_update.state, "%s", state_string);

        cJSON_free(state_string);

        WiFiEvent event;
        event.type = SK_MQTT_STATE_UPDATE;
        event.body.mqtt_state_update = state_update;

        publishEvent(event);
    }

    if (strcmp(type->valuestring, "acknowledgement") == 0)
    {
        cJSON *acknowledge_id = cJSON_GetObjectItem(json_root, "acknowledge_id");
        cJSON *acknowledge_type = cJSON_GetObjectItem(json_root, "acknowledge_type");

        if (acknowledge_id == NULL || acknowledge_type == NULL)
        {
            LOGW("Invalid message received");
            return;
        }

        if (unacknowledged_ids.find(acknowledge_id->valuestring) != unacknowledged_ids.end())
        {
            unacknowledged_ids.erase(acknowledge_id->valuestring);

            if (strcmp(acknowledge_type->valuestring, "init") == 0)
            {
                hass_init_acknowledged = true;
            }

            else if (strcmp(acknowledge_type->valuestring, "state_update") == 0)
            {
                if (unacknowledged_states.find(acknowledge_id->valuestring) != unacknowledged_states.end())
                {
                    EntityStateUpdate state = unacknowledged_states[acknowledge_id->valuestring];

                    MQTTStateUpdate state_update;
                    state_update.all = true;
                    sprintf(state_update.app_id, "%s", state.app_id);
                    sprintf(state_update.entity_id, "%s", state.entity_id);
                    sprintf(state_update.state, "%s", state.state);

                    WiFiEvent event;
                    event.type = SK_MQTT_STATE_UPDATE;
                    event.body.mqtt_state_update = state_update;

                    publishEvent(event);

                    unacknowledged_states.erase(acknowledge_id->valuestring);
                }
            }
        }
    }

    cJSON_Delete(json_root);
}

cJSON *MqttTask::getApps()
{
    lock();
    return apps;
}

void MqttTask::enqueueEntityStateToSend(EntityStateUpdate state)
{
    xQueueSendToBack(entity_state_to_send_queue_, &state, 0);
}

MqttNotifier *MqttTask::getNotifier()
{
    return &mqtt_notifier;
}

void MqttTask::addAppSyncListener(QueueHandle_t queue)
{
    app_sync_listeners_.push_back(queue);
}

void MqttTask::publishAppSync(const cJSON *state)
{
    for (auto listener : app_sync_listeners_)
    {
        xQueueSend(listener, state, portMAX_DELAY);
    }
}

void MqttTask::lock()
{
    xSemaphoreTake(mutex_app_sync_, portMAX_DELAY);
}
void MqttTask::unlock()
{
    xSemaphoreGive(mutex_app_sync_);
}

void MqttTask::setSharedEventsQueue(QueueHandle_t shared_events_queue)
{
    this->shared_events_queue = shared_events_queue;
}

void MqttTask::publishEvent(WiFiEvent event)
{
    event.sent_at = millis();
    xQueueSendToBack(shared_events_queue, &event, 0);
}

QueueHandle_t MqttTask::getEntityStateReceivedQueue()
{
    // Note: This seems to be a placeholder - no queue is actually created or used yet
    // Would need to be implemented if entity state receiving is needed
    return NULL;
}

bool MqttTask::reconnect()
{
    disconnect();
    return connect();
}

bool MqttTask::requestClientLock(const char *client_id)
{
    if (!client_locked_ || (authorized_client_id_ == client_id))
    {
        client_locked_ = true;
        authorized_client_id_ = client_id;
        lock_timestamp_ = millis();
        LOGI("Client lock granted to %s", client_id);
        return true;
    }

    LOGW("Client lock denied for %s, already locked by %s", client_id, authorized_client_id_.c_str());
    return false;
}

bool MqttTask::releaseClientLock(const char *client_id)
{
    if (client_locked_ && (authorized_client_id_ == client_id))
    {
        client_locked_ = false;
        authorized_client_id_ = "";
        LOGI("Client lock released by %s", client_id);
        return true;
    }

    LOGW("Client lock release denied for %s, not the lock owner", client_id);
    return false;
}

bool MqttTask::isClientAuthorized(const char *client_id)
{
    return !client_locked_ || (authorized_client_id_ == client_id);
}

void MqttTask::checkLockTimeout()
{
    if (client_locked_ && (millis() - lock_timestamp_ > LOCK_TIMEOUT_MS))
    {
        LOGI("Client lock for %s timed out", authorized_client_id_.c_str());
        client_locked_ = false;
        authorized_client_id_ = "";
    }
}

void MqttTask::publishLockResponse(const char *client_id, bool success)
{
    if (!mqtt_client.connected())
    {
        LOGW("Cannot publish lock response, MQTT not connected");
        return;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "clientId", client_id);
    cJSON_AddBoolToObject(json, "success", success);
    cJSON_AddBoolToObject(json, "locked", client_locked_);

    if (client_locked_)
    {
        cJSON_AddStringToObject(json, "lockedBy", authorized_client_id_.c_str());
        // Calculate seconds remaining in the lock
        int expires_in = (lock_timestamp_ + LOCK_TIMEOUT_MS - millis()) / 1000;
        cJSON_AddNumberToObject(json, "expires_in", expires_in);
    }

    char *json_str = cJSON_PrintUnformatted(json);
    mqtt_client.publish(MQTT_LOCK_RESPONSE_TOPIC, json_str);
    LOGD("Published lock response: %s", json_str);

    cJSON_free(json_str);
    cJSON_Delete(json);
}

void MqttTask::publishManagerStatus()
{
    if (!mqtt_client.connected())
    {
        return;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "manager_status");
    cJSON_AddStringToObject(json, "status", "online");

    char *json_str = cJSON_PrintUnformatted(json);
    mqtt_client.publish(MQTT_MANAGER_STATUS_TOPIC, json_str);

    cJSON_free(json_str);
    cJSON_Delete(json);
}

#endif

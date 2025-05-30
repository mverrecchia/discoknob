#pragma once
#include "../app.h"

#include "../../font/roboto_thin_bold_24.h"
#include "../../font/roboto_thin_20.h"

class LightSwitchApp : public App
{
public:
    LightSwitchApp(TFT_eSprite *spr_, char *app_id, char *friendly_name, char *entity_id);
    TFT_eSprite *render();
    EntityStateUpdate updateStateFromKnob(PB_SmartKnobState state);
    void updateStateFromHASS(MQTTStateUpdate mqtt_state_update);
    void updateStateFromSystem(AppState state);

private:
    uint8_t current_position = 0;
    uint8_t last_position = 0;
    uint8_t num_positions = 0;

    // needed for UI
    float sub_position_unit = 0;
    float adjusted_sub_position = 0;
    bool first_run = false;

    char mqtt_topic[100]; // Buffer to store the MQTT topic
};
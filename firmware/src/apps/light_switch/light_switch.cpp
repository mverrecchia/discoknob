#include "light_switch.h"

LightSwitchApp::LightSwitchApp(TFT_eSprite *spr_, char *app_id, char *friendly_name, char *entity_id) : App(spr_)
{
    sprintf(this->app_id, "%s", app_id);
    sprintf(this->friendly_name, "%s", friendly_name);
    sprintf(this->entity_id, "%s", entity_id);

    // Use entity_id to determine which MQTT topic to use
    if (strcmp(entity_id, "couch_switch_entity_id") == 0)
    {
        sprintf(this->mqtt_topic, "athom-9d86a1/switch/command");
    }
    else if (strcmp(entity_id, "neon_switch_entity_id") == 0)
    {
        sprintf(this->mqtt_topic, "athom-9d86ed/switch/command");
    }
    else
    {
        // Default fallback
        sprintf(this->mqtt_topic, "athom-9d86a1/switch/command");
    }

    motor_config = PB_SmartKnobConfig{
        current_position, // position
        0,                // sub_position_unit
        current_position, // position_nonce
        0,                // min_position
        1,                // max_position
        60 * PI / 180,    // position_width_radians
        1,                // detent_strength_unit
        1,                // endstop_strength_unit
        0.55,             // Note the snap point is slightly past the midpoint (0.5); compare to normal detents which use a snap point *past* the next value (i.e. > 1)
        "",               // Change the type of app_id from char to char*
        0,                // detent_positions_count
        {},               // detent_positions
        0,                // snap_point_bias
        27,               // led_hue
    };

    big_icon = light_switch_80;
    small_icon = light_switch_40;
}

EntityStateUpdate LightSwitchApp::updateStateFromKnob(PB_SmartKnobState state)
{
    EntityStateUpdate new_state;
    if (state_sent_from_hass)
    {
        state_sent_from_hass = false;
        return new_state;
    }

    current_position = state.current_position;
    sub_position_unit = state.sub_position_unit;
    // needed to next reload of App
    motor_config.position_nonce = current_position;
    motor_config.position = current_position;

    adjusted_sub_position = sub_position_unit * motor_config.position_width_radians;

    if (state.current_position == motor_config.min_position && sub_position_unit < 0)
    {
        adjusted_sub_position = -logf(1 - sub_position_unit * motor_config.position_width_radians / 5 / PI * 180) * 5 * PI / 180;
    }
    else if (state.current_position == motor_config.max_position && sub_position_unit > 0)
    {
        adjusted_sub_position = logf(1 + sub_position_unit * motor_config.position_width_radians / 5 / PI * 180) * 5 * PI / 180;
    }

    if (last_position != current_position && first_run)
    {
        sprintf(new_state.app_id, "%s", app_id);
        sprintf(new_state.entity_id, "%s", entity_id);
        sprintf(new_state.topic, "%s", mqtt_topic);
        sprintf(new_state.state, "%s", current_position > 0 ? "ON" : "OFF");

        last_position = current_position;
        new_state.changed = true;
        sprintf(new_state.app_slug, "%s", APP_SLUG_LIGHT_SWITCH);
    }

    first_run = true;
    return new_state;
}

void LightSwitchApp::updateStateFromHASS(MQTTStateUpdate mqtt_state_update)
{
    cJSON *new_state = cJSON_Parse(mqtt_state_update.state);
    cJSON *on = cJSON_GetObjectItem(new_state, "on");

    if (on != NULL)
    {
        current_position = on->valueint;
        motor_config.position = current_position;
        motor_config.position_nonce = current_position + 1; // TODO: LOOK INTO THIS WEIRD WORK AROUND (NEEDED FOR LIGHT SWITCH NOT TO TOGGLE STATE OF LIGHT TO OFF IF SET TO ON IN HASS, KINDA)
        state_sent_from_hass = true;
    }

    cJSON_Delete(new_state);
}

void LightSwitchApp::updateStateFromSystem(AppState state) {}

TFT_eSprite *LightSwitchApp::render()
{
    uint16_t DISABLED_COLOR = spr_->color565(71, 71, 71);

    uint32_t off_background = spr_->color565(0, 0, 0);
    uint32_t off_lamp_color = spr_->color565(150, 150, 150);

    uint32_t on_background = spr_->color565(71, 39, 1);
    uint32_t on_lamp_color = spr_->color565(245, 164, 66);

    uint16_t center_h = TFT_WIDTH / 2;
    uint16_t center_v = TFT_WIDTH / 2;

    uint8_t icon_size = 80;

    uint16_t offset_vertical = 0;
    uint16_t screen_radius = TFT_WIDTH / 2;

    float left_bound = PI / 2;
    float right_bound = 0;
    float range_radians = (motor_config.max_position - motor_config.min_position) * motor_config.position_width_radians;

    left_bound = PI / 2 + range_radians / 2;
    right_bound = PI / 2 - range_radians / 2;

    float raw_angle = left_bound - (current_position - motor_config.min_position) * motor_config.position_width_radians;
    float adjusted_angle = raw_angle - adjusted_sub_position;

    if (current_position == 0)
    {
        spr_->fillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, off_background);
        spr_->drawBitmap(center_h - icon_size / 2, center_v - icon_size / 2 - offset_vertical, lamp_regular, icon_size, icon_size, off_lamp_color, off_background);
        spr_->setTextColor(off_lamp_color);
        spr_->setFreeFont(&Roboto_Thin_24);
        spr_->drawString(friendly_name, center_h, center_v + icon_size / 2 + 30 - offset_vertical, 1);
    }
    else
    {
        spr_->fillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, on_background);
        spr_->drawBitmap(center_h - icon_size / 2, center_v - icon_size / 2 - offset_vertical, lamp_solid, icon_size, icon_size, on_lamp_color, on_background);
        spr_->setTextColor(on_lamp_color);
        spr_->setFreeFont(&Roboto_Thin_24);
        spr_->drawString(friendly_name, center_h, center_v + icon_size / 2 + 30 - offset_vertical, 1);
    }

    // set the moving dot color
    uint32_t dot_color = TFT_WHITE;

    if (current_position < 1)
    {
        dot_color = off_lamp_color;
    }
    else
    {
        dot_color = on_lamp_color;
    }

    // draw moving dot
    if (num_positions > 0 && ((current_position == motor_config.min_position && sub_position_unit < 0) || (current_position == motor_config.max_position && sub_position_unit > 0)))
    {

        spr_->fillCircle(TFT_WIDTH / 2 + (screen_radius - 10) * cosf(raw_angle), TFT_HEIGHT / 2 - (screen_radius - 10) * sinf(raw_angle), 5, dot_color);
        if (raw_angle < adjusted_angle)
        {
            for (float r = raw_angle; r <= adjusted_angle; r += 2 * PI / 180)
            {
                spr_->fillCircle(TFT_WIDTH / 2 + (screen_radius - 10) * cosf(r), TFT_HEIGHT / 2 - (screen_radius - 10) * sinf(r), 2, dot_color);
            }
            spr_->fillCircle(TFT_WIDTH / 2 + (screen_radius - 10) * cosf(adjusted_angle), TFT_HEIGHT / 2 - (screen_radius - 10) * sinf(adjusted_angle), 2, dot_color);
        }
        else
        {
            for (float r = raw_angle; r >= adjusted_angle; r -= 2 * PI / 180)
            {
                spr_->fillCircle(TFT_WIDTH / 2 + (screen_radius - 10) * cosf(r), TFT_HEIGHT / 2 - (screen_radius - 10) * sinf(r), 2, dot_color);
            }
            spr_->fillCircle(TFT_WIDTH / 2 + (screen_radius - 10) * cosf(adjusted_angle), TFT_HEIGHT / 2 - (screen_radius - 10) * sinf(adjusted_angle), 2, dot_color);
        }
    }
    else
    {
        spr_->fillCircle(TFT_WIDTH / 2 + (screen_radius - 10) * cosf(adjusted_angle), TFT_HEIGHT / 2 - (screen_radius - 10) * sinf(adjusted_angle), 5, dot_color);
    }

    return this->spr_;
};
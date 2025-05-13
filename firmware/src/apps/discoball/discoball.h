#pragma once

#include "../app.h"
#include <RCSwitch.h>
#include <TFT_eSPI.h>
#include <cJSON.h>
#include <esp_now.h>

#include "../../font/roboto_thin_20.h"
#include "../../font/roboto_light_60.h"

enum DiscoballColor
{
    COLOR_NONE,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_WHITE,
    COLOR_GRADUAL
};

enum DiscoballMode
{
    MODE_OFF,
    MODE_ON,
    MODE_JUMP,
    MODE_GRADUAL,
    MODE_BREATHE,
};

enum DiscoballAppMode
{
    DISCOBALL_APP_MODE_SPEED,
    DISCOBALL_APP_MODE_COLOR,
    DISCOBALL_APP_MODE_MODE_TYPE,
    DISCOBALL_APP_MODE_MODE_SPEED
};

// RF command codes
static const char *CMD_BLUE = "1110010000001001000010010";
static const char *CMD_RED = "1110010000001001000001010";
static const char *CMD_GREEN = "1110010000001001000001110";
static const char *CMD_WHITE = "1110010000001001000010000";
static const char *CMD_ON = "1110010000001001000000110";
static const char *CMD_OFF = "1110010000001001000000010";
static const char *CMD_GRADUAL = "1110010000001001000010110";
static const char *CMD_JUMP = "1110010000001001000011010";
static const char *CMD_BREATHE = "1110010000001001000011110";
static const char *CMD_SPEED_MINUS = "1110010000001001000100000";
static const char *CMD_SPEED_PLUS = "1110010000001001000100100";

class DiscoballApp : public App
{
public:
    DiscoballApp(TFT_eSprite *spr_, char *app_id, char *friendly_name, char *entity_id);
    int8_t navigationNext() override;
    void updateStateFromHASS(MQTTStateUpdate mqtt_state_update) override;
    EntityStateUpdate updateStateFromKnob(PB_SmartKnobState state) override;
    void updateStateFromSystem(AppState state) override;
    TFT_eSprite *render() override;

private:
    void renderBackground();
    uint32_t getRainbowColor(float longitude);
    uint32_t interpolateColors(uint32_t color1, uint32_t color2, float t);
    void sendRfCommand(const char *command);

    DiscoballAppMode current_mode = DISCOBALL_APP_MODE_SPEED;
    DiscoballColor current_color = COLOR_NONE;
    DiscoballColor previous_color = COLOR_NONE;
    int current_speed = 0;
    int last_speed = 0;
    bool state_sent_from_hass = false;
    unsigned long lastUpdateTime = 0;
    const unsigned long debounceDelay = 100;

    TFT_eSprite *backgroundSprite;
    uint8_t peerAddress[6];
    float tiltAngle = PI / 12; // 15 degrees tilt
    int centerX = TFT_WIDTH / 2;
    int centerY = TFT_HEIGHT / 2;

    struct Discoball
    {
        bool on;
        bool direction;
        int speed;
        int mode;
        int mode_speed;
    } discoball = {false, 0, 0, 0, 0};

    RCSwitch rfSwitch;
    char buf_[24];
    float minLongitude = -170;
    float maxLongitude = -10;
    float dotLongitude = 0;    // Current longitude of the dot
    float trailLongitudes[30]; // Trail positions
    int trailLength = 50;      // Number of trail segments
    float trailFadeRate = 0.9; // Controls fade speed of the trail
};

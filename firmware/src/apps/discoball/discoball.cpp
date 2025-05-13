#include <esp_now.h>
#include "discoball.h"

DiscoballApp::DiscoballApp(TFT_eSprite *spr_, char *app_id, char *friendly_name, char *entity_id) : App(spr_)
{
    sprintf(this->app_id, "%s", app_id);
    sprintf(this->friendly_name, "%s", friendly_name);
    sprintf(this->entity_id, "%s", entity_id);

    motor_config = PB_SmartKnobConfig{
        0,    // position (start at 0)
        0,    // sub_position_unit
        0,    // position_nonce
        -100, // min_position (now -100)
        100,  // max_position (now 100)
        PI / 100,
        2,
        1,
        1.1,
        "SKDEMO_Discoball",
        0,
        {},
        0,
        27,
    };

    big_icon = discoball_80;
    small_icon = discoball_40;

    backgroundSprite = new TFT_eSprite(spr_);
    backgroundSprite->createSprite(TFT_WIDTH, TFT_HEIGHT);
    renderBackground();

    uint8_t peerAddress[] = {0x30, 0x30, 0xF9, 0xFB, 0x89, 0xD0};
    memcpy(this->peerAddress, peerAddress, 6);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peerAddress, 6);
    peerInfo.channel = 10;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        LOGE("Failed to add peer");
    }

    // Initialize RF transmitter
    rfSwitch.enableTransmit(PIN_RF_TX);
    rfSwitch.setPulseLength(500);
}

int8_t DiscoballApp::navigationNext()
{
    if (current_mode == DISCOBALL_APP_MODE_COLOR && current_color == COLOR_GRADUAL)
    {
        // If we're in color mode and GRADUAL is selected, allow access to mode type
        current_mode = DISCOBALL_APP_MODE_MODE_TYPE;
    }
    else if (current_mode == DISCOBALL_APP_MODE_MODE_TYPE)
    {
        // After mode type, go to mode speed
        current_mode = DISCOBALL_APP_MODE_MODE_SPEED;
    }
    else
    {
        // Normal navigation between speed and color modes
        current_mode = static_cast<DiscoballAppMode>((current_mode + 1) % 2);
    }

    switch (current_mode)
    {
    case DISCOBALL_APP_MODE_SPEED:
        motor_config = PB_SmartKnobConfig{
            0,
            0,
            0,
            -100,
            100,
            PI / 100,
            2,
            1,
            1.1,
            "SKDEMO_Discoball_Speed",
            0,
            {},
            0,
            27,
        };
        break;

    case DISCOBALL_APP_MODE_COLOR:
        motor_config = PB_SmartKnobConfig{
            0,
            0,
            0,
            0,
            5,
            PI / 8,
            1,
            1,
            1.1,
            "SKDEMO_Discoball_Color",
            0,
            {},
            0,
            27,
        };
        break;

    case DISCOBALL_APP_MODE_MODE_TYPE:
        motor_config = PB_SmartKnobConfig{
            0,
            0,
            0,
            0,
            2,
            PI / 4,
            1,
            1,
            1.1,
            "SKDEMO_Discoball_Mode_Type",
            0,
            {},
            0,
            27,
        };
        break;

    case DISCOBALL_APP_MODE_MODE_SPEED:
        motor_config = PB_SmartKnobConfig{
            1, // Start at position 1
            0,
            0,
            1, // min_position (start at 1)
            5, // max_position (allow all 5 dots)
            PI / 8,
            1,
            1,
            1.1,
            "SKDEMO_Discoball_Mode_Speed",
            0,
            {},
            0,
            27,
        };
        break;
    }

    motor_config.position_nonce = motor_config.position;
    return DONT_NAVIGATE_UPDATE_MOTOR_CONFIG;
}

void DiscoballApp::updateStateFromHASS(MQTTStateUpdate mqtt_state_update)
{
    cJSON *new_state = cJSON_Parse(mqtt_state_update.state);
    if (new_state == NULL)
    {
        LOGW("Failed to parse discoball state JSON");
        return;
    }
    
    cJSON *position = cJSON_GetObjectItem(new_state, "position");
    if (position != NULL)
    {
        current_speed = (20 - position->valueint / 5);
        motor_config.position = current_speed;
        motor_config.position_nonce = current_speed;
    }
    
    cJSON *rotation = cJSON_GetObjectItem(new_state, "rotation");
    cJSON *spotlights = cJSON_GetObjectItem(new_state, "spotlights");
    
    if (rotation != NULL && spotlights != NULL)
    {
        LOGI("Received disco message");
        
        cJSON *enabled = cJSON_GetObjectItem(rotation, "enabled");
        if (enabled != NULL && cJSON_IsBool(enabled))
        {
            bool rotation_enabled = cJSON_IsTrue(enabled);
            if (!rotation_enabled)
            {
                current_speed = 0;
                discoball.speed = 0;
                motor_config.position = 0;
                motor_config.position_nonce = 0;
            }
        }
        
        cJSON *direction = cJSON_GetObjectItem(rotation, "direction");
        if (direction != NULL && cJSON_IsBool(direction))
        {
            discoball.direction = cJSON_IsTrue(direction);
        }
        
        cJSON *speed = cJSON_GetObjectItem(rotation, "speed");
        if (speed != NULL && cJSON_IsNumber(speed))
        {
            float speed_value = speed->valuedouble;
            discoball.speed = speed_value * 100;
            current_speed = discoball.direction ? discoball.speed : -discoball.speed;
            
            motor_config.position = current_speed;
            motor_config.position_nonce = current_speed;
            last_speed = current_speed;
        }
        
        cJSON *spotlights_enabled = cJSON_GetObjectItem(spotlights, "enabled");
        if (spotlights_enabled != NULL && cJSON_IsBool(spotlights_enabled))
        {
            bool enabled = cJSON_IsTrue(spotlights_enabled);
            if (enabled != discoball.on)
            {
                discoball.on = enabled;
                sendRfCommand(enabled ? CMD_ON : CMD_OFF);
            }
        }
        
        cJSON *mode = cJSON_GetObjectItem(spotlights, "mode");
        if (mode != NULL && cJSON_IsNumber(mode))
        {
            int new_mode = mode->valueint;
            if (new_mode != discoball.mode)
            {
                discoball.mode = new_mode;
                switch (new_mode)
                {
                case 0:
                    sendRfCommand(CMD_JUMP);
                    break;
                case 1:
                    sendRfCommand(CMD_GRADUAL);
                    break;
                case 2:
                    sendRfCommand(CMD_BREATHE);
                    break;
                }
            }
        }
        
        cJSON *mode_speed = cJSON_GetObjectItem(spotlights, "mode_speed");
        if (mode_speed != NULL && cJSON_IsNumber(mode_speed))
        {
            int new_mode_speed = mode_speed->valueint;
            if (new_mode_speed != discoball.mode_speed)
            {
                int speed_diff = new_mode_speed - discoball.mode_speed;
                for (int i = 0; i < abs(speed_diff); i++)
                {
                    sendRfCommand(speed_diff > 0 ? CMD_SPEED_PLUS : CMD_SPEED_MINUS);
                    delay(50);
                }
                discoball.mode_speed = new_mode_speed;
            }
        }
        
        cJSON *color = cJSON_GetObjectItem(spotlights, "color");
        if (color != NULL && cJSON_IsString(color))
        {
            const char* color_str = color->valuestring;
            if (strncmp(color_str, "#ff0000", 7) == 0)
            {
                if (current_color != COLOR_RED)
                {
                    current_color = COLOR_RED;
                    sendRfCommand(CMD_RED);
                }
            }
            else if (strncmp(color_str, "#00ff00", 7) == 0)
            {
                if (current_color != COLOR_GREEN)
                {
                    current_color = COLOR_GREEN;
                    sendRfCommand(CMD_GREEN);
                }
            }
            else if (strncmp(color_str, "#0000ff", 7) == 0)
            {
                if (current_color != COLOR_BLUE)
                {
                    current_color = COLOR_BLUE;
                    sendRfCommand(CMD_BLUE);
                }
            }
            else if (strncmp(color_str, "#ffffff", 7) == 0)
            {
                if (current_color != COLOR_WHITE)
                {
                    current_color = COLOR_WHITE;
                    sendRfCommand(CMD_WHITE);
                }
            }
        }
        
        unsigned long currentTime = millis();
        if (currentTime - lastUpdateTime > debounceDelay)
        {
            lastUpdateTime = currentTime;
            LOGI("ESP-NOW: Sending current_speed: %d, direction: %d", discoball.speed, discoball.direction);
            esp_err_t result = esp_now_send(peerAddress, (uint8_t *)&discoball, sizeof(discoball));
        }
    }
    
    cJSON_Delete(new_state);
}

void DiscoballApp::sendRfCommand(const char *command)
{
    if (command != nullptr)
    {
        rfSwitch.send(command);
    }
}

EntityStateUpdate DiscoballApp::updateStateFromKnob(PB_SmartKnobState state)
{
    EntityStateUpdate new_state;

    if (current_mode == DISCOBALL_APP_MODE_SPEED)
    {
        current_speed = state.current_position;
        motor_config.position_nonce = current_speed;
        motor_config.position = current_speed;

        if (last_speed != current_speed)
        {
            // Handle speed and direction based on the current position
            if (current_speed <= 3 && current_speed >= -3)
            {
                discoball.speed = 0;
            }
            else
            {
                discoball.speed = abs(current_speed);
            }
            discoball.direction = current_speed < 0 ? false : true;
            LOGI("ESP-NOW: Sending current_speed: %d, direction: %d", discoball.speed, discoball.direction);

            unsigned long currentTime = millis();
            if (currentTime - lastUpdateTime > debounceDelay)
            {
                lastUpdateTime = currentTime;
                esp_err_t result = esp_now_send(peerAddress, (uint8_t *)&discoball, sizeof(discoball));
            }

            last_speed = current_speed;
        }
    }
    else if (current_mode == DISCOBALL_APP_MODE_COLOR)
    {
        DiscoballColor newColor = static_cast<DiscoballColor>(state.current_position);

        if (newColor != current_color)
        {
            previous_color = current_color;
            current_color = newColor;
            motor_config.position_nonce = state.current_position;
            motor_config.position = state.current_position;

            if (current_color == COLOR_NONE && discoball.on)
            {
                sendRfCommand(CMD_OFF);
                discoball.on = false;
            }
            else if (current_color != COLOR_NONE && !discoball.on)
            {
                discoball.on = true;
                sendRfCommand(CMD_ON);
            }
            switch (current_color)
            {
            case COLOR_RED:
                sendRfCommand(CMD_RED);
                break;
            case COLOR_GREEN:
                sendRfCommand(CMD_GREEN);
                break;
            case COLOR_BLUE:
                sendRfCommand(CMD_BLUE);
                break;
            case COLOR_WHITE:
                sendRfCommand(CMD_WHITE);
                break;
            case COLOR_GRADUAL:
                sendRfCommand(CMD_GRADUAL);
                break;
            case COLOR_NONE:
                // nothing
                break;
            }
        }
    }
    else if (current_mode == DISCOBALL_APP_MODE_MODE_TYPE)
    {
        int newMode = state.current_position;
        if (newMode != discoball.mode)
        {
            discoball.mode = newMode;
            switch (newMode)
            {
            case 0:
                sendRfCommand(CMD_JUMP);
                break;
            case 1:
                sendRfCommand(CMD_GRADUAL);
                break;
            case 2:
                sendRfCommand(CMD_BREATHE);
                break;
            }
        }
    }
    else if (current_mode == DISCOBALL_APP_MODE_MODE_SPEED)
    {
        int newSpeed = state.current_position;
        if (newSpeed != discoball.mode_speed)
        {
            if (newSpeed > discoball.mode_speed)
            {
                sendRfCommand(CMD_SPEED_PLUS);
            }
            else if (newSpeed < discoball.mode_speed)
            {
                sendRfCommand(CMD_SPEED_MINUS);
            }
            discoball.mode_speed = newSpeed;
        }
    }

    return new_state;
}

void DiscoballApp::updateStateFromSystem(AppState state) {}

TFT_eSprite *DiscoballApp::render()
{
    backgroundSprite->pushToSprite(spr_, 0, 0);
    if (current_mode == DISCOBALL_APP_MODE_SPEED)
    {
        int radius = 120;
        float arrowLength = map(abs(current_speed), 0, 100, 0, 180);
        int arrowTrailThickness = 6;
        int arrowHeadBaseWidth = 30;
        int arrowHeadTipLength = 30;
        int numSegments = 30;

        float startLongitude, endLongitude;
        if (current_speed >= 0)
        {
            startLongitude = radians(-180);
            endLongitude = radians(-180 + arrowLength);
        }
        else
        {
            startLongitude = radians(0);
            endLongitude = radians(0 - arrowLength);
        }

        for (int i = 0; i < numSegments; i++)
        {
            float t1 = (float)i / numSegments;
            float t2 = (float)(i + 1) / numSegments;

            float phi1 = startLongitude + t1 * (endLongitude - startLongitude);
            float phi2 = startLongitude + t2 * (endLongitude - startLongitude);

            // Convert spherical to Cartesian coordinates for each segment
            float x1 = radius * cos(0) * cos(phi1);
            float y1 = radius * cos(0) * sin(phi1);
            float z1 = radius * sin(0);

            float x2 = radius * cos(0) * cos(phi2);
            float y2 = radius * cos(0) * sin(phi2);
            float z2 = radius * sin(0);

            // Apply tilt transformation
            float y1_tilt = y1 * cos(tiltAngle) - z1 * sin(tiltAngle);
            float z1_tilt = y1 * sin(tiltAngle) + z1 * cos(tiltAngle);

            float y2_tilt = y2 * cos(tiltAngle) - z2 * sin(tiltAngle);
            float z2_tilt = y2 * sin(tiltAngle) + z2 * cos(tiltAngle);

            int screenX1 = centerX + x1;
            int screenY1 = centerY - z1_tilt;

            int screenX2 = centerX + x2;
            int screenY2 = centerY - z2_tilt;

            // Draw the thicker trail in screen space
            for (int offset = -arrowTrailThickness / 2; offset <= arrowTrailThickness / 2; offset++)
            {
                spr_->drawLine(screenX1, screenY1 + offset, screenX2, screenY2 + offset, TFT_WHITE);
            }
        }

        // Draw the arrowhead at the end of the trail
        float phiHead = endLongitude;
        float xTip = radius * cos(0) * cos(phiHead);
        float yTip = radius * cos(0) * sin(phiHead);
        float zTip = radius * sin(0);

        // Arrowhead base points projected onto the sphere
        float baseOffset = radians(arrowHeadBaseWidth / (float)radius);
        float xLeft = radius * cos(0) * cos(phiHead - baseOffset);
        float yLeft = radius * cos(0) * sin(phiHead - baseOffset);
        float zLeft = radius * sin(0);

        float xRight = radius * cos(0) * cos(phiHead + baseOffset);
        float yRight = radius * cos(0) * sin(phiHead + baseOffset);
        float zRight = radius * sin(0);

        // Extend the tip forward for a more pronounced arrowhead
        float xForwardTip = radius * cos(0) * cos(phiHead + radians(arrowHeadTipLength / (float)radius));
        float yForwardTip = radius * cos(0) * sin(phiHead + radians(arrowHeadTipLength / (float)radius));
        float zForwardTip = radius * sin(0);

        // Apply tilt transformation for arrowhead points
        float yTip_tilt = yTip * cos(tiltAngle) - zTip * sin(tiltAngle);
        float zTip_tilt = yTip * sin(tiltAngle) + zTip * cos(tiltAngle);

        float yLeft_tilt = yLeft * cos(tiltAngle) - zLeft * sin(tiltAngle);
        float zLeft_tilt = yLeft * sin(tiltAngle) + zLeft * cos(tiltAngle);

        float yRight_tilt = yRight * cos(tiltAngle) - zRight * sin(tiltAngle);
        float zRight_tilt = yRight * sin(tiltAngle) + zRight * cos(tiltAngle);

        float yForwardTip_tilt = yForwardTip * cos(tiltAngle) - zForwardTip * sin(tiltAngle);
        float zForwardTip_tilt = yForwardTip * sin(tiltAngle) + zForwardTip * cos(tiltAngle);

        int screenXTip = centerX + xTip;
        int screenYTip = centerY - zTip_tilt;

        int screenXLeft = centerX + xLeft;
        int screenYLeft = centerY - zLeft_tilt;

        int screenXRight = centerX + xRight;
        int screenYRight = centerY - zRight_tilt;

        int screenXForwardTip = centerX + xForwardTip;
        int screenYForwardTip = centerY - zForwardTip_tilt;

        // Draw the arrowhead as two filled triangles for a pronounced shape
        spr_->fillTriangle(screenXTip, screenYTip, screenXLeft, screenYLeft, screenXRight, screenYRight, TFT_WHITE);
        spr_->fillTriangle(screenXTip, screenYTip, screenXForwardTip, screenYForwardTip, screenXRight, screenYRight, TFT_WHITE);
    }
    else if (current_mode == DISCOBALL_APP_MODE_COLOR)
    {
        renderBackground();
        if (current_color != COLOR_NONE)
        {
            const uint32_t starColor = spr_->color565(255, 255, 255); // Bright white
            const float radius = 120;                                 // Match the sphere radius

            // Helper function to project a point onto the sphere
            auto projectPoint = [this, radius](float lat, float lon)
            {
                // Convert lat/lon to radians
                float theta = radians(lat);
                float phi = radians(lon);

                // Convert to Cartesian coordinates
                float x = radius * cos(theta) * cos(phi);
                float y = radius * cos(theta) * sin(phi);
                float z = radius * sin(theta);

                // Apply tilt transformation
                float y_tilt = y * cos(tiltAngle) - z * sin(tiltAngle);
                float z_tilt = y * sin(tiltAngle) + z * cos(tiltAngle);

                // Project to screen coordinates
                struct Point
                {
                    int x, y;
                };
                Point p = {
                    centerX + (int)x,
                    centerY - (int)z_tilt};
                return p;
            };

            // Helper function to draw a star at given lat/lon
            auto drawProjectedStar = [this, starColor, projectPoint](float centerLat, float centerLon, float size)
            {
                const int numPoints = 8; // 4 main points + 4 diagonal points
                float pointAngles[numPoints];
                float pointSizes[numPoints];

                // Define the star points in terms of offsets from center
                for (int i = 0; i < numPoints; i++)
                {
                    pointAngles[i] = i * (2 * PI / numPoints);
                    pointSizes[i] = (i % 2 == 0) ? size : size * 0.7; // Alternate between long and short points
                }

                // Draw the star rays
                for (int i = 0; i < numPoints; i++)
                {
                    float pointLat = centerLat + pointSizes[i] * cos(pointAngles[i]);
                    float pointLon = centerLon + pointSizes[i] * sin(pointAngles[i]);

                    auto center = projectPoint(centerLat, centerLon);
                    auto point = projectPoint(pointLat, pointLon);

                    spr_->drawLine(center.x, center.y, point.x, point.y, starColor);
                }

                // Draw center point
                auto center = projectPoint(centerLat, centerLon);
                spr_->fillCircle(center.x, center.y, 2, starColor);
            };

            // Draw cluster of three stars in upper right quadrant of sphere
            drawProjectedStar(40, -30, 5); // Main star
            drawProjectedStar(20, -30, 4); // Secondary star
            drawProjectedStar(30, -50, 4); // Third star
        }
    }
    else if (current_mode == DISCOBALL_APP_MODE_MODE_TYPE)
    {
        renderBackground();
        // Draw mode type text
        const char *modeTypes[] = {"JUMP", "GRADUAL", "BREATHE"};
        int currentMode = discoball.mode;
        if (currentMode >= 0 && currentMode < 3)
        {
            spr_->setTextColor(TFT_WHITE);
            spr_->setFreeFont(&Roboto_Thin_24);
            spr_->drawString(modeTypes[currentMode], centerX, centerY - 30, 1);
        }

        // Draw mode type dots
        int dotRadius = 5;
        int dotSpacing = 30;
        int startX = centerX - (dotSpacing * 1); // Center 3 dots

        for (int i = 0; i < 3; i++)
        {
            int x = startX + (i * dotSpacing);
            int y = centerY + 50; // Position dots below center
            if (i == currentMode)
            {
                spr_->fillCircle(x, y, dotRadius, TFT_WHITE);
            }
            else
            {
                spr_->drawCircle(x, y, dotRadius, TFT_WHITE);
            }
        }
    }
    else if (current_mode == DISCOBALL_APP_MODE_MODE_SPEED)
    {
        renderBackground();
        // Draw SPEED text
        spr_->setTextColor(TFT_WHITE);
        spr_->setFreeFont(&Roboto_Thin_24);
        spr_->drawString("SPEED", centerX, centerY - 30, 1);

        // Draw speed dots
        int currentSpeed = discoball.mode_speed;
        int dotRadius = 5;
        int dotSpacing = 30;
        int startX = centerX - (dotSpacing * 2);

        for (int i = 0; i < 5; i++)
        {
            int x = startX + (i * dotSpacing);
            int y = centerY + 50; // Position dots below center
            if (i < currentSpeed)
            {
                spr_->fillCircle(x, y, dotRadius, TFT_WHITE);
            }
            else
            {
                spr_->drawCircle(x, y, dotRadius, TFT_WHITE);
            }
        }
    }
    return this->spr_;
};

void DiscoballApp::renderBackground()
{
    backgroundSprite->fillSprite(TFT_BLACK); // Clear the sprite
    uint32_t baseColor;

    float lightX = 0.0, lightY = 2.0, lightZ = 3.0; // Light pointed more towards the z-axis

    float mag = sqrt(lightX * lightX + lightY * lightY + lightZ * lightZ);
    lightX /= mag;
    lightY /= mag;
    lightZ /= mag;

    int32_t radius = 120;
    int tileSize = 10;

    for (int lat = -90; lat <= 90; lat += tileSize)
    {
        if (abs(lat) > 80)
        {
            continue;
        }
        for (int lon = -180; lon < 180; lon += tileSize)
        {
            if (current_color == COLOR_GRADUAL)
            {
                baseColor = getRainbowColor(lon);
            }
            else
            {
                switch (current_color)
                {
                case COLOR_RED:
                    baseColor = spr_->color565(255, 100, 100);
                    break;
                case COLOR_GREEN:
                    baseColor = spr_->color565(100, 255, 100);
                    break;
                case COLOR_BLUE:
                    baseColor = spr_->color565(100, 100, 255);
                    break;
                case COLOR_WHITE:
                    baseColor = spr_->color565(255, 255, 255);
                    break;
                case COLOR_NONE:
                default:
                    baseColor = spr_->color565(150, 150, 150);
                    break;
                }
            }
            // Convert latitude and longitude to Cartesian coordinates
            float theta1 = radians(lat);            // Latitude (start of tile)
            float theta2 = radians(lat + tileSize); // Latitude (end of tile)
            float phi1 = radians(lon);              // Longitude (start of tile)
            float phi2 = radians(lon + tileSize);   // Longitude (end of tile)

            // Calculate Cartesian coordinates before tilt
            float x1 = radius * cos(theta1) * cos(phi1);
            float y1 = radius * cos(theta1) * sin(phi1);
            float z1 = radius * sin(theta1);

            float x2 = radius * cos(theta1) * cos(phi2);
            float y2 = radius * cos(theta1) * sin(phi2);
            float z2 = radius * sin(theta1);

            float x3 = radius * cos(theta2) * cos(phi1);
            float y3 = radius * cos(theta2) * sin(phi1);
            float z3 = radius * sin(theta2);

            float x4 = radius * cos(theta2) * cos(phi2);
            float y4 = radius * cos(theta2) * sin(phi2);
            float z4 = radius * sin(theta2);

            // Apply axial tilt (rotation around the x-axis)
            float y1_tilt = y1 * cos(tiltAngle) - z1 * sin(tiltAngle);
            float z1_tilt = y1 * sin(tiltAngle) + z1 * cos(tiltAngle);

            float y2_tilt = y2 * cos(tiltAngle) - z2 * sin(tiltAngle);
            float z2_tilt = y2 * sin(tiltAngle) + z2 * cos(tiltAngle);

            float y3_tilt = y3 * cos(tiltAngle) - z3 * sin(tiltAngle);
            float z3_tilt = y3 * sin(tiltAngle) + z3 * cos(tiltAngle);

            float y4_tilt = y4 * cos(tiltAngle) - z4 * sin(tiltAngle);
            float z4_tilt = y4 * sin(tiltAngle) + z4 * cos(tiltAngle);

            // Project the tilted coordinates onto 2D screen space
            int screenX1 = centerX + x1;
            int screenY1 = centerY - z1_tilt;

            int screenX2 = centerX + x2;
            int screenY2 = centerY - z2_tilt;

            int screenX3 = centerX + x3;
            int screenY3 = centerY - z3_tilt;

            int screenX4 = centerX + x4;
            int screenY4 = centerY - z4_tilt;

            // Calculate tile center for shading
            float tileCenterX = (x1 + x2 + x3 + x4) / 4.0;
            float tileCenterY = (y1_tilt + y2_tilt + y3_tilt + y4_tilt) / 4.0;
            float tileCenterZ = (z1_tilt + z2_tilt + z3_tilt + z4_tilt) / 4.0;

            // Extract base color components
            uint8_t base_r = (baseColor >> 11) & 0x1F;
            uint8_t base_g = (baseColor >> 5) & 0x3F;
            uint8_t base_b = baseColor & 0x1F;

            float brightness = (tileCenterX * lightX + tileCenterY * lightY + tileCenterZ * lightZ) / radius;
            brightness = constrain(0.7 + brightness, 0.0, 1.0);

            // Apply brightness to base color to get final tile color
            uint8_t r = (base_r * brightness);
            uint8_t g = (base_g * brightness);
            uint8_t b = (base_b * brightness);
            uint16_t color = (r << 11) | (g << 5) | b;
            // Draw the tile as a quadrilateral
            backgroundSprite->fillTriangle(screenX1, screenY1, screenX2, screenY2, screenX3, screenY3, color);
            backgroundSprite->fillTriangle(screenX3, screenY3, screenX2, screenY2, screenX4, screenY4, color);

            // Draw borders selectively
            // if (brightness > 0.1)

            uint16_t borderColor = TFT_BLACK; // Border color (e.g., black)
            backgroundSprite->drawLine(screenX1, screenY1, screenX2, screenY2, borderColor);
            backgroundSprite->drawLine(screenX2, screenY2, screenX4, screenY4, borderColor);
            backgroundSprite->drawLine(screenX4, screenY4, screenX3, screenY3, borderColor);
            backgroundSprite->drawLine(screenX3, screenY3, screenX1, screenY1, borderColor);
        }
    }
    // Add a 10-pixel thick annulus around the perimeter
    int ringThickness = 6;

    // Screen dimensions
    int centerX = TFT_WIDTH / 2;
    int centerY = TFT_HEIGHT / 2;

    // Outer radius (to the edge of the screen)
    int outerRadius = min(centerX, centerY); // Smaller of width/2 or height/2

    // Inner radius
    int innerRadius = outerRadius - ringThickness;

    // Draw the annulus by filling the space between the inner and outer radii
    for (int r = innerRadius; r <= outerRadius; r++)
    {
        backgroundSprite->drawCircle(centerX, centerY, r, TFT_BLACK);
    }
}

uint32_t DiscoballApp::getRainbowColor(float longitude)
{
    // Define our color segments
    struct ColorSegment
    {
        float startLon;
        float endLon;
        uint32_t color;
    };

    const ColorSegment segments[] = {
        {-180, -150, spr_->color565(255, 0, 0)},   // Red
        {-150, -130, spr_->color565(255, 127, 0)}, // Orange
        {-130, -110, spr_->color565(255, 255, 0)}, // Yellow
        {-110, -90, spr_->color565(0, 255, 0)},    // Green
        {-90, -70, spr_->color565(0, 0, 255)},     // Blue
        {-70, -50, spr_->color565(75, 0, 130)},    // Indigo
        {-50, -30, spr_->color565(148, 0, 211)},   // Violet
        {-30, 0, spr_->color565(255, 192, 203)}    // Pink
    };

    // Find which segment we're in
    for (int i = 0; i < 8; i++)
    {
        if (longitude >= segments[i].startLon && longitude < segments[i].endLon)
        {
            // Calculate position within this segment
            float t = (longitude - segments[i].startLon) /
                      (segments[i].endLon - segments[i].startLon);

            // If this is the last segment, wrap to first segment
            uint32_t nextColor = (i == 7) ? segments[0].color : segments[i + 1].color;

            // Interpolate between this color and next
            return interpolateColors(segments[i].color, nextColor, t);
        }
    }

    return segments[0].color; // Default to red if somehow out of range
}

uint32_t DiscoballApp::interpolateColors(uint32_t color1, uint32_t color2, float t)
{
    // Extract RGB components
    uint8_t r1 = (color1 >> 11) & 0x1F;
    uint8_t g1 = (color1 >> 5) & 0x3F;
    uint8_t b1 = color1 & 0x1F;

    uint8_t r2 = (color2 >> 11) & 0x1F;
    uint8_t g2 = (color2 >> 5) & 0x3F;
    uint8_t b2 = color2 & 0x1F;

    // Interpolate
    uint8_t r = r1 + (r2 - r1) * t;
    uint8_t g = g1 + (g2 - g1) * t;
    uint8_t b = b1 + (b2 - b1) * t;

    return (r << 11) | (g << 5) | b;
}
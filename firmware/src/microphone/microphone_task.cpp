#include "microphone_task.h"
#include "semaphore_guard.h"
#include "util.h"

// Define the frequency bin arrays
const uint16_t MicrophoneTask::LOW_BINS[NUM_LOW_BINS] = {0}; // ~30-90Hz
const uint16_t MicrophoneTask::MID_BINS[NUM_MID_BINS] = {0};
const uint16_t MicrophoneTask::HIGH_BINS[NUM_HIGH_BINS] = {0};

MicrophoneTask::MicrophoneTask(const uint8_t task_core) : Task{"Mic", 1024 * 4, 1, task_core} // Higher priority (2) than Sensors (0)
{
    mutex_ = xSemaphoreCreateMutex();
    assert(mutex_ != NULL);

    microphone_state_queue_ = xQueueCreate(5, sizeof(MicrophoneState));
    assert(microphone_state_queue_ != NULL);
}

MicrophoneTask::~MicrophoneTask()
{
    vQueueDelete(microphone_state_queue_);
    vSemaphoreDelete(mutex_);
}

void MicrophoneTask::run()
{
    LOGI("Microphone task started with I2S_NUM_1");

    // Initialize microphone state
    microphone_state.clap_detected = false;
    microphone_state.double_clap_detected = false;
    microphone_state.last_clap_time = 0;
    microphone_state.clap_count = 0;

    // Initialize FFT data
    microphone_state.fft_low_band = 0.0f;
    microphone_state.fft_mid_band = 0.0f;
    microphone_state.fft_high_band = 0.0f;

    // Initialize timing
    last_fft_process_ms = millis();
    last_microphone_check_ms = millis();

    // Try initializing the microphone with error handling
    bool mic_initialized = false;

    // Configure I2S for the INMP441 microphone
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL3,
        .dma_buf_count = DMA_BUF_COUNT,
        .dma_buf_len = DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0};

    // Configure I2S pins
    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_MIC_SCK,
        .ws_io_num = PIN_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_MIC_SD};

    // Install and configure I2S driver
    LOGI("About to install I2S_NUM_0 driver");
    esp_err_t result = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (result != ESP_OK)
    {
        LOGE("Failed to install I2S driver: %d", result);
    }
    else
    {
        LOGI("I2S driver installed successfully");

        // LOGI("About to set I2S pins");
        // result = i2s_set_pin(I2S_PORT, &pin_config);
        // if (result != ESP_OK)
        // {
        //     LOGE("Failed to set I2S pins: %d", result);
        //     i2s_driver_uninstall(I2S_PORT);
        // }
        // else
        // {
        //     mic_initialized = true;
        //     LOGI("I2S driver fully initialized successfully");
        // }
    }

    // Main loop
    while (1)
    {
        if (mic_initialized)
        {
            LOGI("I2S_NUM_0 driver is active");
        }
        else
        {
            // LOGI("Running without I2S initialized");
        }

        // Publish the state every second
        publishState(microphone_state);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// void MicrophoneTask::run() //simulated, also works
// {
//     LOGI("Microphone task started in simulation mode");

//     // Initialize microphone state
//     microphone_state.clap_detected = false;
//     microphone_state.double_clap_detected = false;
//     microphone_state.last_clap_time = 0;
//     microphone_state.clap_count = 0;

//     // Initialize FFT data
//     microphone_state.fft_low_band = 0.0f;
//     microphone_state.fft_mid_band = 0.0f;
//     microphone_state.fft_high_band = 0.0f;

//     // Initialize timing
//     last_fft_process_ms = millis();
//     last_microphone_check_ms = millis();

//     // Initialize FFT arrays without actually using I2S
//     for (size_t i = 0; i < FFT_SAMPLES; i++)
//     {
//         m_realComponent[i] = 0.0f;
//         m_imagComponent[i] = 0.0f;
//         m_rawSamples[i] = 0;
//     }

//     // Initialize magnitudes arrays
//     for (size_t i = 0; i < NUM_TOTAL_BINS; i++)
//     {
//         m_magnitudes[i] = 0.0f;
//         m_peaks[i] = 0.0f;
//     }

//     // Generate some simulated data for bands
//     bool increasing = true;
//     float level = 0.0f;

//     // Main loop
//     while (1)
//     {
//         // Simulate microphone data
//         if (increasing)
//         {
//             level += 0.01f;
//             if (level > 0.5f)
//                 increasing = false;
//         }
//         else
//         {
//             level -= 0.01f;
//             if (level < 0.01f)
//                 increasing = true;
//         }

//         // Set simulated band levels
//         microphone_state.fft_low_band = level;
//         microphone_state.fft_mid_band = level * 0.7f;
//         microphone_state.fft_high_band = level * 0.3f;

//         // Occasionally simulate a clap (every 10 seconds)
//         unsigned long now = millis();
//         if (now % 10000 < 100 && now - last_clap_end_time > 2000)
//         {
//             microphone_state.clap_detected = true;
//             last_clap_end_time = now;
//             LOGD("Simulated clap detected!");

//             // Simulate double clap occasionally
//             if (microphone_state.clap_count > 0 && now - microphone_state.last_clap_time < CLAP_TIMEOUT_MS)
//             {
//                 microphone_state.clap_count++;
//                 LOGD("Simulated consecutive clap! Count: %d", microphone_state.clap_count);

//                 if (microphone_state.clap_count >= 2)
//                 {
//                     microphone_state.double_clap_detected = true;

//                     // Generate an event for the double clap
//                     WiFiEvent event;
//                     event.type = SK_DOUBLE_CLAP_DETECTED;
//                     event.body.clap_count = 2;
//                     publishEvent(event);

//                     // Reset counter after detecting pattern
//                     microphone_state.clap_count = 0;
//                     microphone_state.double_clap_detected = false;
//                 }
//             }
//             else
//             {
//                 microphone_state.clap_count = 1;
//             }

//             microphone_state.last_clap_time = now;
//         }
//         else
//         {
//             microphone_state.clap_detected = false;
//         }

//         // Log data periodically (every second)
//         static unsigned long last_log_time = 0;
//         if (millis() - last_log_time > 1000)
//         {
//             LOGD("Simulated FFT Bands - Low: %.2f, Mid: %.2f, High: %.2f",
//                  microphone_state.fft_low_band,
//                  microphone_state.fft_mid_band,
//                  microphone_state.fft_high_band);
//             last_log_time = millis();
//         }

//         // Publish state every 100ms
//         publishState(microphone_state);
//         vTaskDelay(pdMS_TO_TICKS(100));
//     }
// }

// void MicrophoneTask::run() // this works
// {
//     LOGI("Microphone task started");

//     // Initialize microphone state
//     microphone_state.clap_detected = false;
//     microphone_state.double_clap_detected = false;
//     microphone_state.last_clap_time = 0;
//     microphone_state.clap_count = 0;

//     // Initialize FFT data
//     microphone_state.fft_low_band = 0.0f;
//     microphone_state.fft_mid_band = 0.0f;
//     microphone_state.fft_high_band = 0.0f;

//     // Initialize timing
//     last_fft_process_ms = millis();
//     last_microphone_check_ms = millis();

//     // Do nothing but publish empty/zero state periodically
//     while (1)
//     {
//         // Publish the blank state every second
//         publishState(microphone_state);
//         vTaskDelay(pdMS_TO_TICKS(1000));
//     }
// }
// void MicrophoneTask::run()
// {
//     // Initialize the microphone
//     // initMicrophone();
//     LOGI("INMP441 microphone initialized in separate task");

//     // Initialize microphone state
//     microphone_state.clap_detected = false;
//     microphone_state.double_clap_detected = false;
//     microphone_state.last_clap_time = 0;
//     microphone_state.clap_count = 0;

//     // Initialize FFT data
//     microphone_state.fft_low_band = 0.0f;
//     microphone_state.fft_mid_band = 0.0f;
//     microphone_state.fft_high_band = 0.0f;

//     // Initialize timing
//     last_fft_process_ms = millis();
//     last_microphone_check_ms = millis();

//     // Main task loop
//     const uint8_t microphone_polling_rate_hz = 40; // Higher rate for detecting claps

//     // while (1)
//     // {
//     //     // Process microphone data on regular intervals
//     //     if (millis() - last_microphone_check_ms > 1000 / microphone_polling_rate_hz)
//     //     {
//     //         processMicrophoneData();
//     //         last_microphone_check_ms = millis();
//     //     }

//     //     // Check if double clap was detected and publish event
//     //     if (microphone_state.double_clap_detected)
//     //     {
//     //         LOGI("Double clap detected in microphone task!");

//     //         // Generate an event for the double clap
//     //         WiFiEvent event;
//     //         event.type = SK_DOUBLE_CLAP_DETECTED;
//     //         event.body.clap_count = 2;
//     //         publishEvent(event);

//     //         // Reset the flag after publishing event
//     //         microphone_state.double_clap_detected = false;
//     //     }

//     //     delay(5); // Short delay to prevent task from consuming too much CPU
//     // }
// }

void MicrophoneTask::initMicrophone()
{
    // Configure I2S for the INMP441 microphone
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // INMP441 is mono
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = DMA_BUF_COUNT,
        .dma_buf_len = DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0};

    // Configure I2S pins
    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_MIC_SCK,
        .ws_io_num = PIN_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_MIC_SD};

    // Install and configure I2S driver
    esp_err_t result = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (result != ESP_OK)
    {
        LOGE("Failed to install I2S driver: %d", result);
        return;
    }

    result = i2s_set_pin(I2S_PORT, &pin_config);
    if (result != ESP_OK)
    {
        LOGE("Failed to set I2S pins: %d", result);
        return;
    }

    // Initialize FFT arrays
    for (size_t i = 0; i < FFT_SAMPLES; i++)
    {
        m_realComponent[i] = 0.0f;
        m_imagComponent[i] = 0.0f;
        m_rawSamples[i] = 0;
    }

    // Initialize magnitudes arrays
    for (size_t i = 0; i < NUM_TOTAL_BINS; i++)
    {
        m_magnitudes[i] = 0.0f;
        m_peaks[i] = 0.0f;
    }

    LOGI("I2S driver and FFT analyzer initialized successfully");
}

void MicrophoneTask::processMicrophoneData()
{
    // Buffer to store audio samples
    int32_t samples[DMA_BUF_LEN];
    size_t bytes_read = 0;

    // Read samples from I2S
    esp_err_t result = i2s_read(I2S_PORT, samples, sizeof(samples), &bytes_read, 0);
    if (result != ESP_OK)
    {
        // Handle error
        return;
    }

    // Process samples to detect claps
    size_t samples_read = bytes_read / sizeof(int32_t);
    if (detectClap(samples, samples_read))
    {
        processClap();
    }

    // Add samples to our FFT buffer
    for (size_t i = 0; i < samples_read && i < FFT_SAMPLES; i++)
    {
        // Shift existing samples
        if (i + samples_read < FFT_SAMPLES)
        {
            m_rawSamples[i] = m_rawSamples[i + samples_read];
        }
        else
        {
            // Add new samples at the end
            m_rawSamples[i] = samples[i - (FFT_SAMPLES - samples_read)];
        }
    }

    // Process FFT periodically
    if (millis() - last_fft_process_ms > FFT_PERIOD_MS)
    {
        processAudioFFT();
        last_fft_process_ms = millis();
    }
}

void MicrophoneTask::processAudioFFT()
{
    // Get magnitudes from FFT
    getMagnitudes(m_magnitudes);

    // Calculate band averages
    float low_band_sum = 0;
    float mid_band_sum = 0;
    float high_band_sum = 0;

    // Process low frequency band
    for (size_t i = 0; i < NUM_LOW_BINS; i++)
    {
        low_band_sum += m_magnitudes[i];
    }

    // Process mid frequency band
    for (size_t i = 0; i < NUM_MID_BINS; i++)
    {
        mid_band_sum += m_magnitudes[NUM_LOW_BINS + i];
    }

    // Process high frequency band
    for (size_t i = 0; i < NUM_HIGH_BINS; i++)
    {
        high_band_sum += m_magnitudes[NUM_LOW_BINS + NUM_MID_BINS + i];
    }

    // Calculate averages
    microphone_state.fft_low_band = NUM_LOW_BINS > 0 ? low_band_sum / NUM_LOW_BINS : 0;
    microphone_state.fft_mid_band = NUM_MID_BINS > 0 ? mid_band_sum / NUM_MID_BINS : 0;
    microphone_state.fft_high_band = NUM_HIGH_BINS > 0 ? high_band_sum / NUM_HIGH_BINS : 0;

    // Process peaks for visualization or other features
    getMaxMagnitudes(m_peaks);

    // Log data periodically (every second) for debugging
    static unsigned long last_log_time = 0;
    if (millis() - last_log_time > 1000)
    {
        LOGD("FFT Bands - Low: %.2f, Mid: %.2f, High: %.2f",
             microphone_state.fft_low_band,
             microphone_state.fft_mid_band,
             microphone_state.fft_high_band);
        last_log_time = millis();
    }

    // Publish updated state with FFT data
    publishState(microphone_state);
}

void MicrophoneTask::convertSamplesToReal(int32_t *samples, size_t count)
{
    for (size_t idx = 0; idx < count && idx < FFT_SAMPLES; idx++)
    {
        // Convert and normalize audio samples
        m_realComponent[idx] = static_cast<float>(samples[idx]) * NORMALIZE_24BIT;
        m_imagComponent[idx] = 0.0f;

        // Apply Hamming window function to reduce spectral leakage
        float multiplier = 0.5f * (1.0f - cos(2.0f * M_PI * idx / (FFT_SAMPLES - 1)));
        m_realComponent[idx] *= multiplier;
    }
}

void MicrophoneTask::getMagnitudes(float *outMagnitudes)
{
    // Convert raw samples to real/imaginary components for FFT
    convertSamplesToReal(m_rawSamples, FFT_SAMPLES);

    // Perform FFT processing
    m_FFT.compute(m_realComponent, m_imagComponent, FFT_SAMPLES, FFT_FORWARD);
    m_FFT.complexToMagnitude(m_realComponent, m_imagComponent, FFT_SAMPLES);

    // Extract specific frequency bins
    size_t lowIdx = 0;
    size_t midIdx = NUM_LOW_BINS;
    size_t highIdx = NUM_LOW_BINS + NUM_MID_BINS;

    for (size_t idx = 0; idx < NUM_LOW_BINS; idx++)
    {
        outMagnitudes[idx] = m_realComponent[LOW_BINS[idx]];
    }
    for (size_t idx = 0; idx < NUM_MID_BINS; idx++)
    {
        outMagnitudes[midIdx + idx] = m_realComponent[MID_BINS[idx]];
    }
    for (size_t idx = 0; idx < NUM_HIGH_BINS; idx++)
    {
        outMagnitudes[highIdx + idx] = m_realComponent[HIGH_BINS[idx]];
    }
}

void MicrophoneTask::getMaxMagnitudes(float *outPeaks)
{
    // This tracks peak values over time with decay
    for (size_t i = 0; i < NUM_TOTAL_BINS; i++)
    {
        if (m_magnitudes[i] > outPeaks[i])
        {
            outPeaks[i] = m_magnitudes[i];
        }
        else
        {
            // Gradual decay of peaks
            outPeaks[i] *= 0.95f;
        }
    }
}

bool MicrophoneTask::detectClap(int32_t *samples, size_t samples_len)
{
    // Look for high amplitude peak followed by silence
    bool high_amplitude_detected = false;
    int32_t peak_amplitude = 0;

    // Find peak amplitude in the buffer
    for (size_t i = 0; i < samples_len; i++)
    {
        // Convert from 32-bit sample to absolute amplitude
        int32_t amplitude = abs(samples[i] >> 16); // Shift to get 16-bit value

        if (amplitude > peak_amplitude)
        {
            peak_amplitude = amplitude;
        }

        // Check if amplitude exceeds threshold
        if (amplitude > CLAP_THRESHOLD)
        {
            high_amplitude_detected = true;
        }
    }

    // Only detect a clap if we've waited for the cooldown period since the last clap
    if (high_amplitude_detected && millis() - last_clap_end_time > CLAP_COOLDOWN_MS)
    {
        microphone_state.clap_detected = true;
        last_clap_end_time = millis();
        LOGD("Clap detected! Peak amplitude: %d", peak_amplitude);
        return true;
    }

    return false;
}

void MicrophoneTask::processClap()
{
    unsigned long current_time = millis();

    // Process consecutive claps
    if (microphone_state.clap_count > 0)
    {
        // Check if this clap is within the timeout window from the previous clap
        if (current_time - microphone_state.last_clap_time < CLAP_TIMEOUT_MS)
        {
            microphone_state.clap_count++;
            LOGD("Consecutive clap detected! Count: %d", microphone_state.clap_count);

            // Detect double clap pattern
            if (microphone_state.clap_count == 2)
            {
                microphone_state.double_clap_detected = true;

                // Reset counter after detecting pattern
                microphone_state.clap_count = 0;
            }
        }
        else
        {
            // Too much time has passed, reset the counter
            microphone_state.clap_count = 1;
        }
    }
    else
    {
        // First clap
        microphone_state.clap_count = 1;
    }

    // Update last clap time
    microphone_state.last_clap_time = current_time;

    // Publish the updated state
    publishState(microphone_state);
}

void MicrophoneTask::addStateListener(QueueHandle_t queue)
{
    state_listeners_.push_back(queue);
}

void MicrophoneTask::publishState(const MicrophoneState &state)
{
    for (auto listener : state_listeners_)
    {
        xQueueSend(listener, &state, portMAX_DELAY);
    }
}

void MicrophoneTask::setSharedEventsQueue(QueueHandle_t shared_events_queue)
{
    this->shared_events_queue = shared_events_queue;
}

void MicrophoneTask::publishEvent(WiFiEvent event)
{
    event.sent_at = millis();
    xQueueSendToBack(shared_events_queue, &event, 0);
}
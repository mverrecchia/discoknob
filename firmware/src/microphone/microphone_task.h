#pragma once

#include "logger.h"
#include "task.h"
#include "app_config.h"
#include "driver/i2s.h"
#include <arduinoFFT.h>
#include <vector>

class MicrophoneTask : public Task<MicrophoneTask>
{
    friend class Task<MicrophoneTask>;

public:
    MicrophoneTask(const uint8_t task_core);
    ~MicrophoneTask();

    // Add state listeners to receive updates
    void addStateListener(QueueHandle_t queue);

    // Share the events queue
    void setSharedEventsQueue(QueueHandle_t shared_events_queue);
    void publishEvent(WiFiEvent event);

protected:
    void run();

private:
    // I2S microphone configuration
    static const i2s_port_t I2S_PORT = I2S_NUM_0;
    static constexpr size_t SAMPLE_RATE = 48000;
    static constexpr size_t SAMPLE_BITS = 32;
    static constexpr size_t DMA_BUF_LEN = 1024;
    static constexpr size_t DMA_BUF_COUNT = 8;

    // FFT configuration
    static constexpr size_t FFT_SAMPLES = 1024;
    static constexpr size_t FFT_PERIOD_MS = 100; // Process FFT every 100ms
    static constexpr float NORMALIZE_24BIT = 1.0f / 8388608.0f;

    // Frequency bin definitions
    static const uint8_t NUM_LOW_BINS = 5;
    static const uint8_t NUM_MID_BINS = 5;
    static const uint8_t NUM_HIGH_BINS = 5;
    static const uint8_t NUM_TOTAL_BINS = NUM_LOW_BINS + NUM_MID_BINS + NUM_HIGH_BINS;

    // Arrays storing which FFT bins to use
    static const uint16_t LOW_BINS[NUM_LOW_BINS];
    static const uint16_t MID_BINS[NUM_MID_BINS];
    static const uint16_t HIGH_BINS[NUM_HIGH_BINS];

    // Clap detection parameters
    static const int CLAP_THRESHOLD = 30000; // Amplitude threshold for clap detection
    static const int CLAP_TIMEOUT_MS = 1000; // Max time between claps in a sequence
    static const int CLAP_COOLDOWN_MS = 300; // Minimum time between clap detections

    // Microphone and audio processing methods
    void initMicrophone();
    void processMicrophoneData();
    bool detectClap(int32_t *samples, size_t samples_len);
    void processClap();
    void processAudioFFT();
    void convertSamplesToReal(int32_t *samples, size_t count);
    void getMagnitudes(float *outMagnitudes);
    void getMaxMagnitudes(float *outPeaks);

    // Microphone state
    MicrophoneState microphone_state = {};
    QueueHandle_t microphone_state_queue_;

    // FFT components
    ArduinoFFT<float> m_FFT;
    float m_realComponent[FFT_SAMPLES];
    float m_imagComponent[FFT_SAMPLES];
    int32_t m_rawSamples[FFT_SAMPLES];

    // Timing variables
    unsigned long last_microphone_check_ms = 0;
    unsigned long last_fft_process_ms = 0;
    unsigned long last_clap_end_time = 0;

    // Audio analysis results
    float m_magnitudes[NUM_TOTAL_BINS];
    float m_peaks[NUM_TOTAL_BINS];

    // Listeners and synchronization
    std::vector<QueueHandle_t> state_listeners_;
    QueueHandle_t shared_events_queue;
    SemaphoreHandle_t mutex_;

    // Publish state to listeners
    void publishState(const MicrophoneState &state);
};
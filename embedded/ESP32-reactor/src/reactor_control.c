#include "reactor_control.h"

#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_log.h"

static const char *TAG = "reactor_ctrl";

#define REACTOR_LED_GPIO   GPIO_NUM_2


// GPIO36 on ESP32 = ADC1_CHANNEL_0
#define LM35_ADC_CHANNEL       ADC1_CHANNEL_0
#define LM35_ADC_WIDTH         ADC_WIDTH_BIT_12       // 0..4095
#define LM35_ADC_ATTEN         ADC_ATTEN_DB_11        // full-scale ~3.3V

// These match your Arduino sketch
#define LM35_ADC_REF_V         3.3f
#define LM35_ADC_RESOLUTION    4095.0f

// Calibration factor from your Arduino code: tempC = tempC_raw * (18.0 / 6.4)
#define LM35_CALIBRATION_FACTOR   (18.0f / 6.4f)

// Global reactor config
static float g_temp_warning_threshold  = TEMP_WARNING_DEFAULT;
static float g_temp_critical_threshold = TEMP_CRITICAL_DEFAULT;

// Simulated “reactor power” (0–100%)
static uint8_t         g_reactor_power  = 50;
static reactor_state_t g_reactor_state  = REACTOR_STATE_NORMAL;

static void reactor_led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << REACTOR_LED_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

static void reactor_led_update(reactor_state_t state)
{
    switch (state) {
        case REACTOR_STATE_NORMAL:
            gpio_set_level(REACTOR_LED_GPIO, 0);
            break;

        case REACTOR_STATE_WARNING: {
            static bool level = false;
            level = !level;
            gpio_set_level(REACTOR_LED_GPIO, level ? 1 : 0);
            break;
        }

        case REACTOR_STATE_SCRAM:
            gpio_set_level(REACTOR_LED_GPIO, 1);
            break;

        default:
            gpio_set_level(REACTOR_LED_GPIO, 0);
            break;
    }
}

static void lm35_init(void)
{
    // Legacy ADC1 API (ESP-IDF 4.x style). For ESP32 this is fine.
    adc1_config_width(LM35_ADC_WIDTH);
    adc1_config_channel_atten(LM35_ADC_CHANNEL, LM35_ADC_ATTEN);

    ESP_LOGI(TAG, "LM35 ADC initialised on ADC1_CH0 (GPIO36)");
}

static bool lm35_read_temperature(float *out_temp_c)
{
    if (!out_temp_c) {
        return false;
    }

    // Raw 12-bit read (0..4095)
    int raw = adc1_get_raw(LM35_ADC_CHANNEL);
    if (raw < 0) {
        ESP_LOGE(TAG, "adc1_get_raw failed (raw=%d)", raw);
        return false;
    }

    float voltage   = ((float)raw * LM35_ADC_REF_V) / LM35_ADC_RESOLUTION;
    float tempC_raw = voltage * 100.0f;                  // 10 mV/°C -> 100 * V

    // Apply the same calibration factor as your Arduino sketch
    float tempC     = tempC_raw * LM35_CALIBRATION_FACTOR;

    *out_temp_c = tempC;
    return true;
}

static void reactor_update_state(float temp_c, float accel_mag)
{
    bool major_quake = accel_mag > 2.0f;
    bool minor_quake = accel_mag > 0.8f;

    switch (g_reactor_state) {
        case REACTOR_STATE_NORMAL:
            if (temp_c >= g_temp_critical_threshold || major_quake) {
                g_reactor_state = REACTOR_STATE_SCRAM;
                g_reactor_power = 0;
                ESP_LOGW(TAG, "NORMAL -> SCRAM (temp=%.1f, accel=%.2f)",
                         temp_c, accel_mag);
            } else if (temp_c >= g_temp_warning_threshold || minor_quake) {
                g_reactor_state = REACTOR_STATE_WARNING;
                ESP_LOGW(TAG, "NORMAL -> WARNING (temp=%.1f, accel=%.2f)",
                         temp_c, accel_mag);
            }
            break;

        case REACTOR_STATE_WARNING:
            if (temp_c >= g_temp_critical_threshold || major_quake) {
                g_reactor_state = REACTOR_STATE_SCRAM;
                g_reactor_power = 0;
                ESP_LOGW(TAG, "WARNING -> SCRAM (temp=%.1f, accel=%.2f)",
                         temp_c, accel_mag);
            } else if (temp_c < (g_temp_warning_threshold - 2.0f)) {
                g_reactor_state = REACTOR_STATE_NORMAL;
                ESP_LOGI(TAG, "WARNING -> NORMAL (temp=%.1f, accel=%.2f)",
                         temp_c, accel_mag);
            }
            break;

        case REACTOR_STATE_SCRAM:
            // Stay in SCRAM until explicitly reset
            g_reactor_power = 0;
            break;

        default:
            g_reactor_state = REACTOR_STATE_NORMAL;
            break;
    }
}

// Handle incoming commands from comms task
void reactor_control_handle_command(const reactor_command_t *cmd)
{
    if (!cmd) {
        return;
    }

    switch (cmd->type) {
        case CMD_SCRAM:
            ESP_LOGW(TAG, "CMD_SCRAM received");
            g_reactor_state = REACTOR_STATE_SCRAM;
            g_reactor_power = 0;
            break;

        case CMD_RESET_NORMAL:
            ESP_LOGI(TAG, "CMD_RESET_NORMAL received");
            g_reactor_state = REACTOR_STATE_NORMAL;
            g_reactor_power = 50;
            break;

        case CMD_SET_POWER: {
            int32_t power = cmd->value;
            if (power < 0)   power = 0;
            if (power > 100) power = 100;
            g_reactor_power = (uint8_t)power;
            ESP_LOGI(TAG, "CMD_SET_POWER -> %u%%", (unsigned)g_reactor_power);
            break;
        }

        case CMD_NONE:
        default:
            break;
    }
}

void reactor_control_init(void)
{
    reactor_led_init();
    lm35_init();

    g_temp_warning_threshold  = TEMP_WARNING_DEFAULT;
    g_temp_critical_threshold = TEMP_CRITICAL_DEFAULT;
    g_reactor_state = REACTOR_STATE_NORMAL;
    g_reactor_power = 50;
}

void reactor_control_step(uint32_t sample_id,
                          reactor_telemetry_t *out_telemetry)
{
    float temp_c    = 0.0f;
    float accel_mag = 0.2f;

    // read temp from lm35
    bool ok = lm35_read_temperature(&temp_c);
    if (!ok) {
        // Sensor failure -> fail safe
        ESP_LOGE(TAG, "LM35 read failed, forcing SCRAM");
        g_reactor_state = REACTOR_STATE_SCRAM;
        g_reactor_power = 0;
    }

    // Use real temp_c to drive state machine
    reactor_update_state(temp_c, accel_mag);
    reactor_led_update(g_reactor_state);

    if (out_telemetry) {
        out_telemetry->sample_id     = sample_id;
        out_telemetry->temperature_c = temp_c;
        out_telemetry->accel_mag     = accel_mag;
        out_telemetry->state         = g_reactor_state;
        out_telemetry->power_percent = g_reactor_power;
    }
}

reactor_state_t reactor_control_get_state(void)
{
    return g_reactor_state;
}

uint8_t reactor_control_get_power(void)
{
    return g_reactor_power;
}

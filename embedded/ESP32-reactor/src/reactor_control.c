#include "reactor_control.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "reactor_ctrl";

#define REACTOR_LED_GPIO   GPIO_NUM_2

// Global reactor config
static float g_temp_warning_threshold  = TEMP_WARNING_DEFAULT;
static float g_temp_critical_threshold = TEMP_CRITICAL_DEFAULT;

// Simulated “reactor power” (0–100%)
static uint8_t        g_reactor_power = 50;
static reactor_state_t g_reactor_state = REACTOR_STATE_NORMAL;

// LED helpers

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

// Fake sensor readings (for now)

static float fake_temperature_reading(uint32_t sample_id)
{
    // Simple sawtooth between 40°C and 90°C to exercise thresholds
    return 40.0f + ((sample_id % 500U) * 0.1f);
}

static float fake_accel_magnitude(uint32_t sample_id)
{
    // Mostly small, occasionally spike to simulate a “quake”
    if ((sample_id % 200U) == 0U) {
        return 3.0f; // big spike
    }
    return 0.2f; // quiet
}

// State machine + command handling

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

// Public API

void reactor_control_init(void)
{
    reactor_led_init();
    g_temp_warning_threshold  = TEMP_WARNING_DEFAULT;
    g_temp_critical_threshold = TEMP_CRITICAL_DEFAULT;
    g_reactor_state = REACTOR_STATE_NORMAL;
    g_reactor_power = 50;
}

void reactor_control_step(uint32_t sample_id,
                          reactor_telemetry_t *out_telemetry)
{
    float temp_c    = fake_temperature_reading(sample_id);
    float accel_mag = fake_accel_magnitude(sample_id);

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

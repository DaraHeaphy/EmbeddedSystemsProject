#include "reactor_control.h"

#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_log.h"

static const char *TAG = "control";

#define LED_GPIO           GPIO_NUM_2
#define LM35_ADC_CHANNEL   ADC1_CHANNEL_0
#define LM35_ADC_WIDTH     ADC_WIDTH_BIT_12
#define LM35_ADC_ATTEN     ADC_ATTEN_DB_11
#define ADC_REF_V          3.3f
#define ADC_MAX            4095.0f
#define LM35_CAL_FACTOR    (18.0f / 6.4f)

static float s_temp_warning = TEMP_WARNING;
static float s_temp_critical = TEMP_CRITICAL;
static uint8_t s_power = 50;
static reactor_state_t s_state = REACTOR_STATE_NORMAL;

static void led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
}

static void led_update(reactor_state_t state)
{
    static bool blink = false;

    switch (state) {
    case REACTOR_STATE_NORMAL:
        gpio_set_level(LED_GPIO, 0);
        break;
    case REACTOR_STATE_WARNING:
        blink = !blink;
        gpio_set_level(LED_GPIO, blink);
        break;
    case REACTOR_STATE_SCRAM:
        gpio_set_level(LED_GPIO, 1);
        break;
    }
}

static void lm35_init(void)
{
    adc1_config_width(LM35_ADC_WIDTH);
    adc1_config_channel_atten(LM35_ADC_CHANNEL, LM35_ADC_ATTEN);
    ESP_LOGI(TAG, "lm35 ready on adc1 ch0");
}

static bool lm35_read(float *temp)
{
    int raw = adc1_get_raw(LM35_ADC_CHANNEL);
    if (raw < 0) {
        return false;
    }

    float voltage = (raw * ADC_REF_V) / ADC_MAX;
    float temp_raw = voltage * 100.0f;
    *temp = temp_raw * LM35_CAL_FACTOR;
    return true;
}

static void update_state(float temp, float accel)
{
    bool major_quake = accel > 2.0f;
    bool minor_quake = accel > 0.8f;

    switch (s_state) {
    case REACTOR_STATE_NORMAL:
        if (temp >= s_temp_critical || major_quake) {
            s_state = REACTOR_STATE_SCRAM;
            s_power = 0;
            ESP_LOGW(TAG, "NORMAL -> SCRAM (temp=%.1f accel=%.2f)", temp, accel);
        } else if (temp >= s_temp_warning || minor_quake) {
            s_state = REACTOR_STATE_WARNING;
            ESP_LOGW(TAG, "NORMAL -> WARNING (temp=%.1f accel=%.2f)", temp, accel);
        }
        break;

    case REACTOR_STATE_WARNING:
        if (temp >= s_temp_critical || major_quake) {
            s_state = REACTOR_STATE_SCRAM;
            s_power = 0;
            ESP_LOGW(TAG, "WARNING -> SCRAM (temp=%.1f accel=%.2f)", temp, accel);
        } else if (temp < (s_temp_warning - 2.0f)) {
            s_state = REACTOR_STATE_NORMAL;
            ESP_LOGI(TAG, "WARNING -> NORMAL (temp=%.1f)", temp);
        }
        break;

    case REACTOR_STATE_SCRAM:
        // stays in scram until reset command
        s_power = 0;
        break;
    }
}

void reactor_control_handle_command(const reactor_command_t *cmd)
{
    if (!cmd) return;

    switch (cmd->type) {
    case CMD_SCRAM:
        ESP_LOGW(TAG, "cmd: SCRAM");
        s_state = REACTOR_STATE_SCRAM;
        s_power = 0;
        break;

    case CMD_RESET_NORMAL:
        ESP_LOGI(TAG, "cmd: RESET_NORMAL");
        s_state = REACTOR_STATE_NORMAL;
        s_power = 50;
        break;

    case CMD_SET_POWER: {
        int32_t p = cmd->value;
        if (p < 0) p = 0;
        if (p > 100) p = 100;
        s_power = (uint8_t)p;
        ESP_LOGI(TAG, "cmd: SET_POWER %u%%", s_power);
        break;
    }

    default:
        break;
    }
}

void reactor_control_init(void)
{
    led_init();
    lm35_init();
    s_state = REACTOR_STATE_NORMAL;
    s_power = 50;
}

void reactor_control_step(uint32_t sample_id, reactor_telemetry_t *out)
{
    float temp = 0.0f;
    float accel = 0.2f;  // placeholder, no accelerometer connected

    if (!lm35_read(&temp)) {
        ESP_LOGE(TAG, "lm35 read failed, forcing scram");
        s_state = REACTOR_STATE_SCRAM;
        s_power = 0;
    }

    update_state(temp, accel);
    led_update(s_state);

    if (out) {
        out->sample_id = sample_id;
        out->temperature_c = temp;
        out->accel_mag = accel;
        out->state = s_state;
        out->power_percent = s_power;
    }
}

reactor_state_t reactor_control_get_state(void)
{
    return s_state;
}

uint8_t reactor_control_get_power(void)
{
    return s_power;
}

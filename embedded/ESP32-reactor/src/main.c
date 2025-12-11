// embedded/ESP32-reactor/src/main.c

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "esp_log.h"

// ===== Logging tag =====
static const char *TAG = "reactor";

// ===== Reactor configuration (defaults) =====
#define TEMP_WARNING_DEFAULT   60.0f
#define TEMP_CRITICAL_DEFAULT  80.0f

// ===== Task configuration =====
#define CONTROL_TASK_PERIOD_MS     100     // 10 Hz
#define CONTROL_TASK_STACK_SIZE    4096
#define CONTROL_TASK_PRIORITY      5

#define COMMS_TASK_STACK_SIZE      4096
#define COMMS_TASK_PRIORITY        3

// ===== LED GPIO (FireBeetle onboard LED is on GPIO2 / D9) =====
#define REACTOR_LED_GPIO           GPIO_NUM_2

// ===== Reactor state machine =====
typedef enum {
    REACTOR_STATE_NORMAL = 0,
    REACTOR_STATE_WARNING,
    REACTOR_STATE_SCRAM
} reactor_state_t;

// ===== Command types (from CommsTask -> ControlTask) =====
typedef enum {
    CMD_NONE = 0,
    CMD_SCRAM,
    CMD_RESET_NORMAL,
    CMD_SET_POWER,       // value = 0..100
} reactor_command_type_t;

typedef struct {
    reactor_command_type_t type;
    int32_t value;       // used for CMD_SET_POWER
} reactor_command_t;

// ===== Telemetry struct (from ControlTask -> CommsTask) =====
typedef struct {
    uint32_t       sample_id;
    float          temperature_c;
    float          accel_mag;      // for now fake/simulated
    reactor_state_t state;
    uint8_t        power_percent;
} reactor_telemetry_t;

// ===== Queues =====
static QueueHandle_t s_telemetry_queue = NULL;
static QueueHandle_t s_command_queue   = NULL;

// ===== Global reactor “config” (for now constants, later set by commands) =====
static float g_temp_warning_threshold  = TEMP_WARNING_DEFAULT;
static float g_temp_critical_threshold = TEMP_CRITICAL_DEFAULT;

// Simulated “reactor power” (0–100%)
static uint8_t g_reactor_power = 50;

// Current state
static reactor_state_t g_reactor_state = REACTOR_STATE_NORMAL;

// ===== LED helpers =====
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

// For now: simple mapping of state -> LED behaviour
static void reactor_led_update(reactor_state_t state)
{
    switch (state) {
        case REACTOR_STATE_NORMAL:
            // LED off in NORMAL
            gpio_set_level(REACTOR_LED_GPIO, 0);
            break;
        case REACTOR_STATE_WARNING:
            // Blink slowly to indicate WARNING
            // (For now: just toggle)
        {
            static bool level = false;
            level = !level;
            gpio_set_level(REACTOR_LED_GPIO, level ? 1 : 0);
            break;
        }
        case REACTOR_STATE_SCRAM:
            // Solid ON in SCRAM
            gpio_set_level(REACTOR_LED_GPIO, 1);
            break;
        default:
            gpio_set_level(REACTOR_LED_GPIO, 0);
            break;
    }
}

// ===== Fake sensor readings for step 1 =====
// Later you will replace these with LM35 ADC + MPU6050 I2C reads.

static float fake_temperature_reading(uint32_t sample_id)
{
    // Simple sawtooth between 40°C and 90°C to exercise thresholds
    // 40 + (sample_id % 500) * 0.1 -> cycles every 50s at 10Hz
    float temp = 40.0f + ((sample_id % 500U) * 0.1f);
    return temp;
}

static float fake_accel_magnitude(uint32_t sample_id)
{
    // Mostly small, occasionally spike to simulate a “quake”
    if ((sample_id % 200U) == 0U) {
        return 3.0f; // big spike
    }
    return 0.2f; // quiet
}

// ===== Simple reactor state machine =====
static void reactor_handle_command(const reactor_command_t *cmd)
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
            // Only allow reset if temperature is below warning threshold
            // (for fake sensors we can't know here easily, but later you will
            // use the last temperature reading; for now we just reset).
            g_reactor_state = REACTOR_STATE_NORMAL;
            g_reactor_power = 50;
            break;

        case CMD_SET_POWER: {
            int32_t power = cmd->value;   // make a local copy we can modify
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

static void reactor_update_state(float temp_c, float accel_mag)
{
    bool major_quake = accel_mag > 2.0f;
    bool minor_quake = accel_mag > 0.8f;

    switch (g_reactor_state) {
        case REACTOR_STATE_NORMAL:
            if (temp_c >= g_temp_critical_threshold || major_quake) {
                g_reactor_state = REACTOR_STATE_SCRAM;
                g_reactor_power = 0;
                ESP_LOGW(TAG, "NORMAL -> SCRAM (temp=%.1f, accel=%.2f)", temp_c, accel_mag);
            } else if (temp_c >= g_temp_warning_threshold || minor_quake) {
                g_reactor_state = REACTOR_STATE_WARNING;
                ESP_LOGW(TAG, "NORMAL -> WARNING (temp=%.1f, accel=%.2f)", temp_c, accel_mag);
            }
            break;

        case REACTOR_STATE_WARNING:
            if (temp_c >= g_temp_critical_threshold || major_quake) {
                g_reactor_state = REACTOR_STATE_SCRAM;
                g_reactor_power = 0;
                ESP_LOGW(TAG, "WARNING -> SCRAM (temp=%.1f, accel=%.2f)", temp_c, accel_mag);
            } else if (temp_c < (g_temp_warning_threshold - 2.0f)) {
                // small hysteresis
                g_reactor_state = REACTOR_STATE_NORMAL;
                ESP_LOGI(TAG, "WARNING -> NORMAL (temp=%.1f, accel=%.2f)", temp_c, accel_mag);
            }
            break;

        case REACTOR_STATE_SCRAM:
            // Remain in SCRAM until a RESET command arrives (handled elsewhere)
            g_reactor_power = 0;
            break;

        default:
            g_reactor_state = REACTOR_STATE_NORMAL;
            break;
    }
}

// ===== Control Task (high priority, periodic) =====
static void control_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Control task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    uint32_t sample_id = 0;

    for (;;) {
        // 1) Process any pending commands without blocking
        reactor_command_t cmd;
        while (xQueueReceive(s_command_queue, &cmd, 0) == pdTRUE) {
            reactor_handle_command(&cmd);
        }

        // 2) Read sensors (fake for now)
        float temp_c   = fake_temperature_reading(sample_id);
        float accel_mag = fake_accel_magnitude(sample_id);

        // 3) Update state machine
        reactor_update_state(temp_c, accel_mag);

        // 4) Update LED outputs
        reactor_led_update(g_reactor_state);

        // 5) Build telemetry and enqueue (non-blocking)
        reactor_telemetry_t t = {
            .sample_id      = sample_id,
            .temperature_c  = temp_c,
            .accel_mag      = accel_mag,
            .state          = g_reactor_state,
            .power_percent  = g_reactor_power,
        };

        if (xQueueSend(s_telemetry_queue, &t, 0) != pdTRUE) {
            // Queue full: drop the sample (later you can track a drop counter)
            ESP_LOGW(TAG, "Telemetry queue full, dropping sample %u", (unsigned)sample_id);
        }

        sample_id++;

        // 6) Wait until next period
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS));
    }
}

// ===== Comms Task (lower priority) =====
// For step 1: just logs telemetry. Later this will:
//  - Format telemetry into your proprietary serial protocol
//  - Handle incoming bytes, parse commands, enqueue to s_command_queue
static void comms_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Comms task started");

    reactor_telemetry_t t;

    for (;;) {
        // Block until a telemetry item is available
        if (xQueueReceive(s_telemetry_queue, &t, portMAX_DELAY) == pdTRUE) {
            // For now, print it out
            ESP_LOGI(TAG,
                     "TELEM sample=%u temp=%.1fC accel=%.2f state=%d power=%u%%",
                     (unsigned)t.sample_id,
                     t.temperature_c,
                     t.accel_mag,
                     (int)t.state,
                     (unsigned)t.power_percent);
        }
    }
}

// ===== app_main =====
void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 reactor starting up");

    // Init LED GPIO
    reactor_led_init();

    // Create queues
    s_telemetry_queue = xQueueCreate(32, sizeof(reactor_telemetry_t));
    s_command_queue   = xQueueCreate(8, sizeof(reactor_command_t));

    if (s_telemetry_queue == NULL || s_command_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queues");
        // Without queues, there's no point continuing
        return;
    }

    // Create control task (higher priority)
    BaseType_t ok;
    ok = xTaskCreate(
        control_task,
        "ControlTask",
        CONTROL_TASK_STACK_SIZE,
        NULL,
        CONTROL_TASK_PRIORITY,
        NULL
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ControlTask");
        return;
    }

    // Create comms task (lower priority)
    ok = xTaskCreate(
        comms_task,
        "CommsTask",
        COMMS_TASK_STACK_SIZE,
        NULL,
        COMMS_TASK_PRIORITY,
        NULL
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CommsTask");
        return;
    }

    ESP_LOGI(TAG, "Tasks created, system running");
}

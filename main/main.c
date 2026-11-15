#include "esp_rom_sys.h"
#include "esp_timer.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ble_config.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// 1 = USB diagnostics, BLE and power hold disabled; 0 = normal firmware.
#define DIAGNOSTIC_MODE 0

#define LED_A GPIO_NUM_4
#define LED_B GPIO_NUM_5

#define ADC_CH ADC_CHANNEL_0
#define ADC_ATTEN_LOW ADC_ATTEN_DB_0
#define ADC_ATTEN_HIGH ADC_ATTEN_DB_11

#define SAMPLE_COUNT 64
#define SAMPLE_PERIOD_MS 137
#define STATE_DELAY_MS 500

// 1: GPIO4 = зелёный, GPIO5 = красный.
// 0: наоборот.
#define GREEN_A 0

// Калибровка головки + OPA333.
// Сопротивление 0, 10, 20 ... 200 МОм.
// Напряжение на выходе ОУ, мВ.
static const int cal_mv[21] = {
    2380, 1256, 857, 661, 535,
    455, 394, 350, 314, 288,
    268, 249, 233, 218, 206,
    195, 186, 178, 170, 163, 157
};

// Единственный порог, МОм.
static int threshold = 180;

static void thresholds_load(void)
{
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READONLY, &h) != ESP_OK)
        return;

    int32_t saved = 180;
    if (nvs_get_i32(h, "threshold", &saved) == ESP_OK &&
        saved >= 0 && saved <= 200 && saved % 10 == 0)
        threshold = saved;

    nvs_close(h);
}

int32_t ble_threshold_get(void)
{
    return threshold;
}

esp_err_t ble_threshold_save(int32_t value)
{

    if (value < 0 || value > 200 || value % 10 != 0)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open("settings", NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;

    err = nvs_set_i32(h, "threshold", value);
    if (err == ESP_OK)
        err = nvs_commit(h);

    nvs_close(h);

    if (err == ESP_OK)
        threshold = value;

    return err;
}


// Запрос выключения питания от BLE.
static volatile bool ble_poweroff_requested = false;

static volatile int64_t ble_poweroff_at_us = 0;

void ble_request_poweroff(void)
{
    if (!ble_poweroff_requested) {
        ble_poweroff_at_us = esp_timer_get_time() + 1000000LL;
        ble_poweroff_requested = true;
    }
}

typedef enum {
    GREEN_STEADY = 0,
    GREEN_BLINK,
    RED_GREEN_BLINK,
    RED_BLINK,
    RED_STEADY
} led_state_t;

static volatile int32_t latest_adc_mv = -1;
static int32_t zero_mv = -1;

static void zero_load(void)
{
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READONLY, &h) != ESP_OK)
        return;

    int32_t saved = -1;
    if (nvs_get_i32(h, "zero_mv", &saved) == ESP_OK &&
        saved >= 0 && saved <= 3300)
        zero_mv = saved;

    nvs_close(h);
}

static SemaphoreHandle_t adc_mutex;
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle_0;
static adc_cali_handle_t cali_handle_11;
static bool calibration_enabled = false;
static bool adc_high_range = false;

static esp_err_t adc_set_atten(adc_atten_t atten);

// Сохранение текущего нуля в NVS.
esp_err_t ble_zero_save(void)
{
    if (xSemaphoreTake(adc_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    bool previous_range = adc_high_range;
    esp_err_t err = previous_range ? ESP_OK : adc_set_atten(ADC_ATTEN_HIGH);
    int mv = -1;

    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(10));
        int raw = 0;
        err = adc_oneshot_read(adc_handle, ADC_CH, &raw);
        if (err == ESP_OK)
            err = adc_cali_raw_to_voltage(cali_handle_11, raw, &mv);
    }

    esp_err_t restore = previous_range ? ESP_OK : adc_set_atten(ADC_ATTEN_LOW);
    xSemaphoreGive(adc_mutex);

    if (err != ESP_OK) return err;
    if (restore != ESP_OK) return restore;
    if (mv < 0 || mv > 3300) return ESP_ERR_INVALID_STATE;

    nvs_handle_t h;
    err = nvs_open("settings", NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;

    err = nvs_set_i32(h, "zero_mv", mv);

    if (err == ESP_OK)
        err = nvs_commit(h);

    nvs_close(h);
    if (err == ESP_OK)
        zero_mv = mv;
    return err;
}



static void leds(bool green, bool red)
{
#if GREEN_A
    gpio_set_level(LED_A, green ? 1 : 0);
    gpio_set_level(LED_B, red ? 1 : 0);
#else
    gpio_set_level(LED_A, red ? 1 : 0);
    gpio_set_level(LED_B, green ? 1 : 0);
#endif
}

static void show_leds(led_state_t state, int phase)
{
    switch (state) {
        case GREEN_STEADY:
            leds(true, false);
            break;

        case GREEN_BLINK:
            leds(phase == 0, false);
            break;

        case RED_GREEN_BLINK:
            leds(phase == 0, phase == 1);
            break;

        case RED_BLINK:
            leds(false, phase == 0);
            break;

        case RED_STEADY:
        default:
            leds(false, true);
            break;
    }
}

// Независимое управление пограничным миганием.
static volatile bool boundary_blink_active = false;

static void boundary_blink_task(void *arg)
{
    bool red = true;
    while (1) {
        if (boundary_blink_active) {
            leds(!red, red);
            red = !red;
            vTaskDelay(pdMS_TO_TICKS(200));
        } else {
            red = true;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}


// Активная пищалка GPIO7: три коротких сигнала.
static volatile bool buzzer_enabled = false;

static void buzzer_task(void *arg)
{
    const int durations[] = {30, 30, 30, 30, 30, 100};
    while (1) {
        for (int i = 0; i < 6; i++) {
            gpio_set_level(GPIO_NUM_7,
                           buzzer_enabled && (i % 2 == 0));
            vTaskDelay(pdMS_TO_TICKS(durations[i]));
        }
    }
}

static void adc_init(void)
{
    adc_mutex = xSemaphoreCreateMutex();
    configASSERT(adc_mutex != NULL);

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_LOW,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc_handle, ADC_CH, &chan_cfg));

    adc_cali_curve_fitting_config_t cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CH,
        .atten = ADC_ATTEN_LOW,
        .bitwidth = ADC_BITWIDTH_12,
    };

    esp_err_t err0 = adc_cali_create_scheme_curve_fitting(
        &cfg, &cali_handle_0);

    cfg.atten = ADC_ATTEN_HIGH;
    esp_err_t err11 = adc_cali_create_scheme_curve_fitting(
        &cfg, &cali_handle_11);

    calibration_enabled = (err0 == ESP_OK && err11 == ESP_OK);

    printf("ADC calibration: 0dB=%s, 11dB=%s\n",
           esp_err_to_name(err0), esp_err_to_name(err11));

    ESP_ERROR_CHECK(err0);
    ESP_ERROR_CHECK(err11);
}

static esp_err_t adc_set_atten(adc_atten_t atten)
{
    adc_oneshot_chan_cfg_t cfg = {
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    return adc_oneshot_config_channel(adc_handle, ADC_CH, &cfg);
}

static float zero_corrected_mv(float mv)
{
    if (zero_mv <= 0)
        return mv;

    return mv * ((float)cal_mv[0] / (float)zero_mv);
}

static float resistance_mohm(float mv)
{
    if (mv >= cal_mv[0]) return 0.0f;
    if (mv <= cal_mv[20]) return 200.0f;

    for (int i = 0; i < 20; i++) {
        if (mv <= cal_mv[i] &&
            mv >= cal_mv[i + 1]) {

            float fraction =
                (cal_mv[i] - mv) /
                (float)(cal_mv[i] - cal_mv[i + 1]);

            return 10.0f * (i + fraction);
        }
    }

    return 0.0f;
}

static led_state_t select_state(float r)
{
    if (r < threshold) return RED_STEADY;
    if (r < threshold + 10) return RED_GREEN_BLINK;
    return GREEN_STEADY;
}




void app_main(void)
{


    gpio_config_t cfg = {
        .pin_bit_mask =
            (1ULL << LED_A) | (1ULL << LED_B),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));
    leds(false, false);

    gpio_reset_pin(GPIO_NUM_7);
    ESP_ERROR_CHECK(gpio_pulldown_en(GPIO_NUM_7));
    gpio_set_level(GPIO_NUM_7, 0);
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_7, GPIO_MODE_OUTPUT));

    BaseType_t buzzer_ok = xTaskCreate(
        buzzer_task, "buzzer", 2048, NULL, 5, NULL);
    configASSERT(buzzer_ok == pdPASS);

    BaseType_t blink_ok = xTaskCreate(
        boundary_blink_task, "boundary_blink",
        2048, NULL, 5, NULL);
    configASSERT(blink_ok == pdPASS);

    ESP_ERROR_CHECK(nvs_flash_init());
    thresholds_load();

    printf("ACTIVE THRESHOLD = %d Mohm\n", threshold);
    zero_load();

    adc_init();

    // GPIO6 пока не удерживает питание.
    gpio_reset_pin(GPIO_NUM_6);
    gpio_set_level(GPIO_NUM_6, 0);
    gpio_set_direction(GPIO_NUM_6, GPIO_MODE_OUTPUT);

    int64_t power_start_us = esp_timer_get_time();
    bool power_hold_enabled = false;
    int64_t ble_start_us = 0;


    led_state_t current = GREEN_STEADY;
    int log_counter = 0;
    int64_t decision_started_us = 0;
    float history[3] = {0};
    int history_count = 0;
    bool confirmed = false;

    while (1) {
        if (power_hold_enabled && ble_start_us != 0 &&
            esp_timer_get_time() - ble_start_us >= 120000000LL) {
            ble_request_poweroff();
        }

        if (ble_poweroff_requested &&
            esp_timer_get_time() >= ble_poweroff_at_us) {
            gpio_set_level(GPIO_NUM_6, 0);
            printf("POWER OFF REQUESTED\\n");
            while (1)
                vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // Через 5 секунд включаем самоподхват.
        if (!DIAGNOSTIC_MODE && !power_hold_enabled &&
            esp_timer_get_time() - power_start_us >= 8000000LL) {
            gpio_set_level(GPIO_NUM_6, 1);
            power_hold_enabled = true;
            ble_start_us = esp_timer_get_time();
            printf("POWER HOLD ENABLED\\n");
            ESP_ERROR_CHECK(ble_config_start());
            printf("BLE CONFIG STARTED\\n");
        }

        if (power_hold_enabled) {
            int64_t t = (esp_timer_get_time() - ble_start_us) / 1000 % 1423;
            leds(false, t < 70 || (t >= 140 && t < 210) ||
                        (t >= 280 && t < 350) ||
                        (t >= 420 && t < 490));
        }

        xSemaphoreTake(adc_mutex, portMAX_DELAY);
        int64_t raw_sum = 0;

        for (int i = 0; i < SAMPLE_COUNT; i++) {
            int raw_sample = 0;

            ESP_ERROR_CHECK(
                adc_oneshot_read(
                    adc_handle, ADC_CH, &raw_sample
                )
            );

            raw_sum += raw_sample;
            esp_rom_delay_us(1000);
        }

        float raw =
            raw_sum / (float)SAMPLE_COUNT;

        bool high_range = adc_high_range;

        if (!high_range && raw >= 3900.0f) {
            ESP_ERROR_CHECK(adc_set_atten(ADC_ATTEN_HIGH));
            adc_high_range = true;
            high_range = true;
            printf("ADC SWITCH: 0 -> 11 dB\\n");
            vTaskDelay(pdMS_TO_TICKS(10));

            raw_sum = 0;
            for (int i = 0; i < SAMPLE_COUNT; i++) {
                int sample = 0;
                ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CH, &sample));
                raw_sum += sample;
                esp_rom_delay_us(1000);
            }
            raw = raw_sum / (float)SAMPLE_COUNT;
        }

        if (high_range && calibration_enabled) {
            int high_mv = 0;
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(
                cali_handle_11, (int)(raw + 0.5f), &high_mv));

            if (high_mv < 700) {
                ESP_ERROR_CHECK(adc_set_atten(ADC_ATTEN_LOW));
                adc_high_range = false;
                high_range = false;
                printf("ADC SWITCH: 11 -> 0 dB\\n");
                vTaskDelay(pdMS_TO_TICKS(10));

                raw_sum = 0;
                for (int i = 0; i < SAMPLE_COUNT; i++) {
                    int sample = 0;
                    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CH, &sample));
                    raw_sum += sample;
                    esp_rom_delay_us(1000);
                }
                raw = raw_sum / (float)SAMPLE_COUNT;
            }
        }

        float mv = 0.0f;

        if (calibration_enabled) {
            int voltage_mv = 0;

            ESP_ERROR_CHECK(
                adc_cali_raw_to_voltage(
                    high_range ? cali_handle_11 : cali_handle_0,
                    (int)(raw + 0.5f),
                    &voltage_mv
                )
            );

            mv = (float)voltage_mv;
        } else {
            // Только диагностическая оценка.
            // Не использовать для точных порогов.
            mv = raw * 3300.0f / 4095.0f;
        }

        xSemaphoreGive(adc_mutex);
        printf("CAL,%.1f,%.1f,%.1f,%d\n",
               raw, mv, resistance_mohm(mv),
               high_range ? 11 : 0);
        latest_adc_mv = (int32_t)(mv + 0.5f);
        if (power_hold_enabled) {
            buzzer_enabled = false;
            gpio_set_level(GPIO_NUM_7, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        float r = resistance_mohm(zero_corrected_mv(mv));
        int64_t now_us = esp_timer_get_time();

        if (mv < 100.0f) {
            boundary_blink_active = false;
            leds(false, false);
            decision_started_us = 0;
            history_count = 0;
            confirmed = false;
        } else {
            if (decision_started_us == 0) {
                decision_started_us = now_us;
                history_count = 0;
                confirmed = false;
            }

            int elapsed_ms =
                (int)((now_us - decision_started_us) / 1000);

            history[history_count % 3] = mv;
            history_count++;

            float min_mv = mv;
            float max_mv = mv;

            int n = history_count < 3 ? history_count : 3;
            for (int i = 0; i < n; i++) {
                if (history[i] < min_mv) min_mv = history[i];
                if (history[i] > max_mv) max_mv = history[i];
            }

            float spread = max_mv - min_mv;
            float boundary_mv = (float)cal_mv[threshold / 10];
            if (zero_mv > 0)
                boundary_mv *= (float)zero_mv / (float)cal_mv[0];

            // Теневой прогноз, без влияния на светодиод.
            if (!confirmed && elapsed_ms >= 600 && n == 3) {
                int pred = min_mv > boundary_mv + 30.0f ? 4 :
                           max_mv < boundary_mv - 30.0f ? 0 : -1;
                printf("PRED t=%d mv=%.1f spread=%.1f color=%d\n",
                       elapsed_ms, mv, spread, pred);
            }

            bool stable = n == 3 && spread <= 3.0f;
            bool green_safe = max_mv < boundary_mv - 2.0f;
            bool red_safe = min_mv > boundary_mv + 2.0f;

            bool far_green = max_mv < boundary_mv - 30.0f;
            bool far_red = min_mv > boundary_mv + 30.0f;
            bool early_stable = n == 3 && spread <= 10.0f;

            if (!confirmed && elapsed_ms >= 800 &&
                early_stable && (far_green || far_red)) {
                current = far_green ? GREEN_STEADY : RED_STEADY;
                confirmed = true;
            }

            if (!confirmed && elapsed_ms >= 2000 &&
                stable && (green_safe || red_safe)) {
                current = green_safe ? GREEN_STEADY : RED_STEADY;
                confirmed = true;
            }

            // Через 3 секунды завершаем ожидание.
            // Неопределённость обозначаем чередованием цветов.
            if (!confirmed && elapsed_ms >= 3000) {
                confirmed = true;
                current = green_safe && stable ? GREEN_STEADY :
                          red_safe && stable ? RED_STEADY :
                          GREEN_STEADY;
                if (!(stable && (green_safe || red_safe)))
                    current = -1;
            }

            printf("DIAG2 t=%d spread=%.1f boundary=%.1f "
                   "stable=%d conf=%d color=%d\\n",
                   elapsed_ms, spread, boundary_mv,
                   stable ? 1 : 0, confirmed ? 1 : 0,
                   confirmed ? (int)current : -1);

            if (confirmed && (int)current == -1) {
                boundary_blink_active = true;
            } else {
                boundary_blink_active = false;
                if (confirmed) {
                    show_leds(current, 0);
                } else {
                    leds(true, false);
                    vTaskDelay(pdMS_TO_TICKS(15));
                    leds(false, false);
                }
            }
        }

        buzzer_enabled = !power_hold_enabled && confirmed &&
                         r >= 0.0f && r <= 10.0f &&
                         mv >= 100.0f;

        if (++log_counter >= 10) {
            log_counter = 0;

            printf(
                "ADC=%.1f U=%.1f mV "
                "R=%.1f Mohm LED=%d CAL=%d\n",
                raw, mv, r,
                (int)current,
                calibration_enabled ? 1 : 0
            );
        }

        vTaskDelay(
            pdMS_TO_TICKS(SAMPLE_PERIOD_MS)
        );
    }
}

#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

#include <stdint.h>
#include "esp_err.h"

// Функции основной программы.
int32_t ble_threshold_get(void);
esp_err_t ble_threshold_save(int32_t value);
esp_err_t ble_zero_save(void);
void ble_request_poweroff(void);

// Запуск BLE-сервиса настройки порогов.
esp_err_t ble_config_start(void);

#endif

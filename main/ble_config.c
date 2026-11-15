#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ble_config.h"
#include "esp_err.h"

#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static uint8_t own_addr_type;

static int thresholds_access(uint16_t conn_handle,
                             uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR)
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

    int32_t value = ble_threshold_get();

    return os_mbuf_append(ctxt->om, &value, sizeof(value)) == 0
        ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int thresholds_write_access(uint16_t conn_handle,
                                   uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt,
                                   void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return BLE_ATT_ERR_READ_NOT_PERMITTED;

    if (OS_MBUF_PKTLEN(ctxt->om) != sizeof(int32_t))
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    int32_t value = 0;

    if (os_mbuf_copydata(ctxt->om, 0, sizeof(value), &value) != 0)
        return BLE_ATT_ERR_UNLIKELY;

    if (ble_threshold_save(value) != ESP_OK)
        return BLE_ATT_ERR_UNLIKELY;

    ble_request_poweroff();
    return 0;
}

// BLE-команда сохранения нуля.
static int zero_write_access(uint16_t conn_handle,
                             uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return BLE_ATT_ERR_READ_NOT_PERMITTED;

    if (OS_MBUF_PKTLEN(ctxt->om) != 1)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    uint8_t command = 0;
    if (os_mbuf_copydata(ctxt->om, 0, 1, &command) != 0 ||
        command != 1)
        return BLE_ATT_ERR_UNLIKELY;

    if (ble_zero_save() != ESP_OK)
        return BLE_ATT_ERR_UNLIKELY;

    return 0;
}

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xFFF0),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF1),
                .access_cb = thresholds_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF2),
                .access_cb = thresholds_write_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF3),
                .access_cb = zero_write_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {0}
        },
    },
    {0}
};

static void start_advertising(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISCONNECT:
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;
    default:
        break;
    }

    return 0;
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"E6-16";
    fields.name_len = strlen("E6-16");
    fields.name_is_complete = 1;

    if (ble_gap_adv_set_fields(&fields) != 0)
        return;

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(own_addr_type, NULL,
                      BLE_HS_FOREVER, &params,
                      gap_event, NULL);
}

static void on_sync(void)
{
    if (ble_hs_id_infer_auto(0, &own_addr_type) == 0)
        start_advertising();
}

static void ble_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_config_start(void)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK)
        return err;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_hs_cfg.sync_cb = on_sync;

    int rc = ble_gatts_count_cfg(services);
    if (rc != 0)
        return ESP_FAIL;

    rc = ble_gatts_add_svcs(services);
    if (rc != 0)
        return ESP_FAIL;

    nimble_port_freertos_init(ble_task);
    return ESP_OK;
}

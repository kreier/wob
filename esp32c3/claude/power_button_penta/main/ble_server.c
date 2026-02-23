/**
 * ble_server.c
 *
 * Advertises a custom BLE service:
 *   Service UUID  : 0x00FF
 *   Characteristic: 0xFF01  (WRITE | WRITE_NO_RSP)
 *   User descriptor: "Power button Penta"
 *
 * Any write to the characteristic triggers usb_hid_send_wake_key().
 *
 * The device advertises as "Penta Power Btn" and keeps BLE advertising alive
 * after connection so other clients can still discover it.
 */

#include "ble_server.h"
#include "usb_hid.h"

#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "BLE_PWR";

/* ── UUIDs ───────────────────────────────────────────────────────────────── */
#define WAKE_SERVICE_UUID       0x00FF
#define WAKE_CHAR_UUID          0xFF01

/* ── GAP advertising payload ─────────────────────────────────────────────── */
#define DEVICE_NAME             "Penta Power Btn"

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,   /* 20 ms – snappy discovery */
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = true,
    .min_interval        = 0x0006,
    .max_interval        = 0x0010,
    .appearance          = 0x00,
    .manufacturer_len    = 0,
    .p_manufacturer_data = NULL,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = 0,
    .p_service_uuid      = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

/* ── GATT attribute table ────────────────────────────────────────────────── */
#define GATTS_PROFILE_IDX   0
#define GATTS_APP_ID        0

enum {
    IDX_SVC,
    IDX_CHAR_WAKE,
    IDX_CHAR_WAKE_VAL,
    IDX_CHAR_WAKE_DESC,   /* User Description */
    IDX_TABLE_SIZE,
};

static uint16_t handle_table[IDX_TABLE_SIZE];

static const uint16_t primary_service_uuid     = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t char_decl_uuid           = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t char_user_desc_uuid      = ESP_GATT_UUID_CHAR_DESCRIPTION;

static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE |
                                       ESP_GATT_CHAR_PROP_BIT_WRITE_NR;

static const uint16_t wake_service_uuid  = WAKE_SERVICE_UUID;
static const uint16_t wake_char_uuid     = WAKE_CHAR_UUID;
static const uint8_t  wake_char_value[]  = {0x00};

static const char user_desc[] = "Power button Penta";

static const esp_gatts_attr_db_t gatt_db[IDX_TABLE_SIZE] = {

    /* Service declaration */
    [IDX_SVC] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid,
          ESP_GATT_PERM_READ,
          sizeof(wake_service_uuid), sizeof(wake_service_uuid),
          (uint8_t *)&wake_service_uuid }
    },

    /* Characteristic declaration */
    [IDX_CHAR_WAKE] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_16, (uint8_t *)&char_decl_uuid,
          ESP_GATT_PERM_READ,
          sizeof(char_prop_write), sizeof(char_prop_write),
          (uint8_t *)&char_prop_write }
    },

    /* Characteristic value */
    [IDX_CHAR_WAKE_VAL] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_16, (uint8_t *)&wake_char_uuid,
          ESP_GATT_PERM_WRITE,
          sizeof(wake_char_value), sizeof(wake_char_value),
          (uint8_t *)wake_char_value }
    },

    /* User Description descriptor */
    [IDX_CHAR_WAKE_DESC] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_16, (uint8_t *)&char_user_desc_uuid,
          ESP_GATT_PERM_READ,
          sizeof(user_desc) - 1, sizeof(user_desc) - 1,
          (uint8_t *)user_desc }
    },
};

/* ── GATTS event handler ─────────────────────────────────────────────────── */
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {

    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "GATTS registered, app_id=%d", param->reg.app_id);
        esp_ble_gap_set_device_name(DEVICE_NAME);
        esp_ble_gap_config_adv_data(&adv_data);
        esp_ble_gatts_create_attr_tab(gatt_db, gatts_if,
                                      IDX_TABLE_SIZE, 0);
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status == ESP_GATT_OK &&
            param->add_attr_tab.num_handle == IDX_TABLE_SIZE) {
            memcpy(handle_table, param->add_attr_tab.handles,
                   sizeof(handle_table));
            esp_ble_gatts_start_service(handle_table[IDX_SVC]);
            ESP_LOGI(TAG, "Attribute table created, service started");
        } else {
            ESP_LOGE(TAG, "Failed to create attribute table, status=%d",
                     param->add_attr_tab.status);
        }
        break;

    case ESP_GATTS_WRITE_EVT:
        /* Any write to our characteristic triggers the wake key */
        if (param->write.handle == handle_table[IDX_CHAR_WAKE_VAL]) {
            ESP_LOGI(TAG, "Wake write received – sending HID key");
            usb_hid_send_wake_key();
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "Client connected, conn_id=%d", param->connect.conn_id);
        /* Keep advertising so other clients can still find/connect */
        esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "Client disconnected, restarting advertising");
        esp_ble_gap_start_advertising(&adv_params);
        break;

    default:
        break;
    }
}

/* ── GAP event handler ───────────────────────────────────────────────────── */
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Advertising start failed");
        } else {
            ESP_LOGI(TAG, "Advertising started");
        }
        break;
    default:
        break;
    }
}

/* ── Public init ─────────────────────────────────────────────────────────── */
void ble_server_init(void)
{
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(GATTS_APP_ID));
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(128));

    ESP_LOGI(TAG, "BLE GATT server initialised");
}

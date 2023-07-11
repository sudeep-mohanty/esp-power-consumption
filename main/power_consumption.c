/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Copyright <2023> <sudp.mohanty@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby
 * granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * ESP Power Consumption Example
 *
 *
 * This example is designed to experiment with variour power saving features of Espressif SoCs.
 * To connect to a Wi-Fi AP the SSID and PASSWORD must be set in the example configuration menu.
 * To use Wi-Fi 6 Target Wake Time (TWT) feature, the Wi-Fi AP must be IEEE 802.11ax (Wi-Fi 6)
 * capable and support TWT.
 */

#include <netdb.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wifi_he.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_sleep.h"

/*******************************************************
 *                Constants
 *******************************************************/
static const char *TAG = "esp-pwr-con";

/*******************************************************
 *                Structures
 *******************************************************/

/*******************************************************
 *                Variable Definitions
 *******************************************************/
#define DEFAULT_SSID CONFIG_EXAMPLE_WIFI_SSID
#define DEFAULT_PWD CONFIG_EXAMPLE_WIFI_PASSWORD

static bool sta_connected = false;

#if CONFIG_EXAMPLE_ITWT_TRIGGER_ENABLE
bool trigger_enabled = true;
#else
bool trigger_enabled = false;
#endif

#if CONFIG_EXAMPLE_ITWT_ANNOUNCED
bool flow_type_announced = true;
#else
bool flow_type_announced = false;
#endif

esp_netif_t *netif_sta = NULL;
const int CONNECTED_BIT = BIT0;
const int DISCONNECTED_BIT = BIT1;
EventGroupHandle_t wifi_event_group;

/*******************************************************
 *                Function Declarations
 *******************************************************/

/*******************************************************
 *                Function Definitions
 *******************************************************/

static void print_config(void)
{
    ESP_LOGI(TAG, "*****************************************************");
    ESP_LOGI(TAG, "\t\t\tEXAMPLE CONFIGURATION");
    ESP_LOGI(TAG, "Sleep Mode Configuration:");
#if CONFIG_EXAMPLE_LIGHT_SLEEP
    ESP_LOGI(TAG, "\tLight Sleep : enabled");
#else
    ESP_LOGI(TAG, "\tLight Sleep : disabled");
#endif
#if CONFIG_EXAMPLE_DEEP_SLEEP
    ESP_LOGI(TAG, "\tDeep Sleep : enabled");
#else
    ESP_LOGI(TAG, "\tDeep Sleep : disabled");
#endif
#if CONFIG_EXAMPLE_MODEM_SLEEP
    ESP_LOGI(TAG, "\tMODEM Sleep : enabled");
    #if EXAMPLE_MODEM_SLEEP_NONE
        ESP_LOGI(TAG, "\t\tMODEM Sleep Type: None");
    #elif EXAMPLE_MODEM_SLEEP_MIN
        ESP_LOGI(TAG, "\t\tMODEM Sleep Type: MIN");
    #else 
        ESP_LOGI(TAG, "\t\tMODEM Sleep Type: MAX");
    #endif
#else
    ESP_LOGI(TAG, "\tMODEM Sleep : disabled");
#endif
#if CONFIG_EXAMPLE_PM_ENABLE
    ESP_LOGI(TAG, "Power Management : enabled");
    ESP_LOGI(TAG, "\tMax frequency for DFS %d MHz", CONFIG_EXAMPLE_MAX_CPU_FREQ_MHZ);
    ESP_LOGI(TAG, "\tMin frequency for DFS %d MHz", CONFIG_EXAMPLE_MIN_CPU_FREQ_MHZ);
    #if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        ESP_LOGI(TAG, "\tFreeRTOS Tickless Idle : enabled");
    #else
        ESP_LOGI(TAG, "\tFreeRTOS Tickless Idle : disabled");
    #endif
#else
    ESP_LOGI(TAG, "Power Management : disabled");
#endif
#if CONFIG_EXAMPLE_TWT_ENABLE
    ESP_LOGI(TAG, "TWT : enabled");
    ESP_LOGI(TAG, "\tTWT Wake Interval Mantissa : %d", CONFIG_EXAMPLE_TWT_WAKE_INTERVAL_MANTISSA);
    ESP_LOGI(TAG, "\tTWT Wake Interval Exponent : %d", CONFIG_EXAMPLE_TWT_WAKE_INTERVAL_EXPONENT);
    ESP_LOGI(TAG, "\tTWT Minumum Wake Duration : %d", CONFIG_EXAMPLE_TWT_MIN_WAKE_DURATION);
#else
    ESP_LOGI(TAG, "TWT : disabled");
#endif

    ESP_LOGI(TAG, "*****************************************************");

    /* Delay before begininning */
    vTaskDelay(1000);
}

static void got_ip_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    xEventGroupClearBits(wifi_event_group, DISCONNECTED_BIT);
    xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);

#if CONFIG_EXAMPLE_TWT_ENABLE
    /* setup a trigger-based announce individual TWT agreement. */
    wifi_phy_mode_t phymode;
    wifi_config_t sta_cfg = { 0, };
    esp_wifi_get_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_sta_get_negotiated_phymode(&phymode);
    if (phymode == WIFI_PHY_MODE_HE20) {
        esp_err_t err = ESP_OK;
        int flow_id = 0;
        err = esp_wifi_sta_itwt_setup(TWT_REQUEST, trigger_enabled, flow_type_announced ? 0 : 1,
                                      CONFIG_EXAMPLE_TWT_MIN_WAKE_DURATION, CONFIG_EXAMPLE_TWT_WAKE_INTERVAL_EXPONENT,
                                      CONFIG_EXAMPLE_TWT_WAKE_INTERVAL_MANTISSA, &flow_id);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "itwt setup failed, err:0x%x", err);
        }
    } else {
        ESP_LOGE(TAG, "Must be in 11ax mode to support itwt");
    }
#endif /* CONFIG_EXAMPLE_TWT_ENABLE */
}

static void start_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "sta connect to %s", DEFAULT_SSID);
    esp_wifi_connect();
}

static void connect_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "sta connected to %s", DEFAULT_SSID);
    sta_connected = true;
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "sta disconnect, reconnect...");
    xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    sta_connected = false;
    esp_wifi_connect();
}

#if CONFIG_EXAMPLE_TWT_ENABLE
static const char *itwt_probe_status_to_str(wifi_itwt_probe_status_t status)
{
    switch (status) {
    case ITWT_PROBE_FAIL:                 return "itwt probe fail";
    case ITWT_PROBE_SUCCESS:              return "itwt probe success";
    case ITWT_PROBE_TIMEOUT:              return "itwt probe timeout";
    case ITWT_PROBE_STA_DISCONNECTED:     return "Sta disconnected";
    default:                              return "Unknown status";
    }
}

static void itwt_setup_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    wifi_event_sta_itwt_setup_t *setup = (wifi_event_sta_itwt_setup_t *) event_data;
    if (setup->setup_cmd == TWT_ACCEPT) {
        /* TWT Wake Interval = TWT Wake Interval Mantissa * (2 ^ TWT Wake Interval Exponent) */
        ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_SETUP>flow_id:%d, %s, %s, wake_dura:%d, wake_invl_e:%d, wake_invl_m:%d", setup->flow_id,
                setup->trigger ? "trigger-enabled" : "non-trigger-enabled", setup->flow_type ? "unannounced" : "announced",
                setup->min_wake_dura, setup->wake_invl_expn, setup->wake_invl_mant);
        ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_SETUP>wake duration:%d us, service period:%d us", setup->min_wake_dura << 8, setup->wake_invl_mant << setup->wake_invl_expn);
    } else {
        ESP_LOGE(TAG, "<WIFI_EVENT_ITWT_SETUP>unexpected setup command:%d", setup->setup_cmd);
    }
}

static void itwt_teardown_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data)
{
    wifi_event_sta_itwt_teardown_t *teardown = (wifi_event_sta_itwt_teardown_t *) event_data;
    ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_TEARDOWN>flow_id %d%s", teardown->flow_id, (teardown->flow_id == 8) ? "(all twt)" : "");
}

static void itwt_suspend_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    wifi_event_sta_itwt_suspend_t *suspend = (wifi_event_sta_itwt_suspend_t *) event_data;
    ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_SUSPEND>status:%d, flow_id_bitmap:0x%x, actual_suspend_time_ms:[%lu %lu %lu %lu %lu %lu %lu %lu]",
             suspend->status, suspend->flow_id_bitmap,
             suspend->actual_suspend_time_ms[0], suspend->actual_suspend_time_ms[1], suspend->actual_suspend_time_ms[2], suspend->actual_suspend_time_ms[3],
             suspend->actual_suspend_time_ms[4], suspend->actual_suspend_time_ms[5], suspend->actual_suspend_time_ms[6], suspend->actual_suspend_time_ms[7]);
}

static void itwt_probe_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    wifi_event_sta_itwt_probe_t *probe = (wifi_event_sta_itwt_probe_t *) event_data;
    ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_PROBE>status:%s, reason:0x%x", itwt_probe_status_to_str(probe->status), probe->reason);
}
#endif /* CONFIG_EXAMPLE_TWT_ENABLE */

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    netif_sta = esp_netif_create_default_wifi_sta();
    assert(netif_sta);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_STA_START,
                    &start_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_STA_CONNECTED,
                    &connect_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_STA_DISCONNECTED,
                    &disconnect_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &got_ip_handler,
                    NULL,
                    NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = DEFAULT_SSID,
            .password = DEFAULT_PWD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX);
}

#if CONFIG_EXAMPLE_TWT_ENABLE
static void itwt_init()
{
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_ITWT_SETUP,
                    &itwt_setup_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_ITWT_TEARDOWN,
                    &itwt_teardown_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_ITWT_SUSPEND,
                    &itwt_suspend_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_ITWT_PROBE,
                    &itwt_probe_handler,
                    NULL,
                    NULL));
}
#endif /* CONFIG_EXAMPLE_TWT_ENABLE */

void pwr_con(void *arg)
{
    /* Print example configuration */
    print_config();

    /* Initialize Wi-Fi configuration and register event handlers */
    wifi_init();

    /* Configure MODEM sleep if enabled */
#if CONFIG_EXAMPLE_MODEM_SLEEP_MIN
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
#elif CONFIG_EXAMPLE_MODEM_SLEEP_MAX
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
#elif CONFIG_EXAMPLE_MODEM_SLEEP_NONE
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
#endif /* CONFIG_EXAMPLE_WIFI_PS_MIN */

    /* PM Config: DFS + Auto Lightsleep (Tickless Idle) */
#if CONFIG_EXAMPLE_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_EXAMPLE_MAX_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_EXAMPLE_MIN_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true
#endif /* CONFIG_FREERTOS_USE_TICKLESS_IDLE */ 
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
#endif /* CONFIG_EXAMPLE_PM_ENABLE */

    /* TWT */
#if CONFIG_EXAMPLE_TWT_ENABLE
    itwt_init();
#endif /* CONFIG_EXAMPLE_TWT_ENABLE */

    /* Start Wi-Fi */
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Light Sleep */
#if CONFIG_EXAMPLE_LIGHT_SLEEP
    while (1) {
        /* Wait until sta is connected */
        while (sta_connected == false) {
            vTaskDelay(1);
        }

        /* Enter Light Sleep */
        ESP_LOGI(TAG, "Entering Light Sleep Mode");
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(CONFIG_EXAMPLE_SLEEP_TIME_US));
        esp_light_sleep_start();
        if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
            ESP_LOGI(TAG, "Wakeup from timer");
        } else {
            ESP_LOGW(TAG, "Unknown wakeup cause %d\n", esp_sleep_get_wakeup_cause());
        }

        esp_wifi_connect();
    }
#endif /* CONFIG_EXAMPLE_LIGHT_SLEEP */

    /* Deep Sleep */
#if CONFIG_EXAMPLE_DEEP_SLEEP
    /* Wait until sta is connected */
    while (sta_connected != true) {
        vTaskDelay(1);
    }

    /* Enter Deep Sleep */
    ESP_LOGI(TAG, "Entering Deep Sleep Mode");
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(CONFIG_EXAMPLE_SLEEP_TIME_US));
    esp_deep_sleep_start();
#endif /* CONFIG_EXAMPLE_DEEP_SLEEP */

    while (1) {
        /* Keep the task running */
        vTaskDelay(10000);
    }
}

void app_main(void)
{
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    /* Create the power consumption experimentation task */
   BaseType_t err = xTaskCreate(pwr_con, "esp-pwr-con", 4096, NULL, 5, NULL);
   if (err != pdTRUE) {
       ESP_LOGE(TAG, "Could not create task");
       abort();
   }
}

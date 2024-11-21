#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <inttypes.h>

static const char *TAG = "WebSocketApp";

#define WIFI_SSID      "SSID"
#define WIFI_PASSWORD  "PASSWORD"
// #define WEBSOCKET_URI  "ws://CLOUD_URL:PORT" // cloud server
#define WEBSOCKET_URI  "ws://LOCAL_MACHINE_IP:PORT" // Nginx proxy

/* FreeRTOS synchronization primitives */
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

/* WebSocket client handle */
static esp_websocket_client_handle_t client = NULL;

#define DATA_PACKET_SIZE 1024   // Size of the data packet to send in bytes
#define SEND_INTERVAL_MS 0001   // Interval between packets in milliseconds

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                               &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, 
                                    int32_t event_id, void *event_data)
{
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket disconnected");
            break;
        case WEBSOCKET_EVENT_DATA:
            // ESP_LOGI(TAG, "Received WebSocket data");
            break;
        default:
            ESP_LOGW(TAG, "Unhandled WebSocket event ID: %" PRIi32, event_id);
            break;
    }
}

static void websocket_init(void)
{
    esp_websocket_client_config_t websocket_cfg = {
        .uri = WEBSOCKET_URI,
    };

    client = esp_websocket_client_init(&websocket_cfg);
    ESP_ERROR_CHECK(esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, 
                                                  websocket_event_handler, NULL));
    ESP_ERROR_CHECK(esp_websocket_client_start(client));
}

static void websocket_send_task(void *arg)
{
    uint8_t *data = malloc(DATA_PACKET_SIZE);
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate memory for data packet");
        vTaskDelete(NULL);
    }

    // Fill the data packet with `0xff`
    memset(data, 0xff, DATA_PACKET_SIZE);

    while (true) {
        if (esp_websocket_client_is_connected(client)) {
            unsigned long start_t = esp_timer_get_time();
            int err = esp_websocket_client_send_bin(client, (const char *)data, DATA_PACKET_SIZE, portMAX_DELAY);
            unsigned long delta_t = esp_timer_get_time() - start_t;

            if (err != -1) {
                ESP_LOGI(TAG, "Sent data of size %d bytes in %.3f ms", DATA_PACKET_SIZE, (delta_t / 1000.0));
            } else {
                ESP_LOGE(TAG, "Failed to send data");
            }
        } else {
            ESP_LOGW(TAG, "WebSocket not connected");
        }

        vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }

    free(data);
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi and wait for connection
    wifi_init();
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Connected to WiFi");

    // Initialize WebSocket
    websocket_init();

    // Start WebSocket send task
    BaseType_t ret_val = xTaskCreate(websocket_send_task, "WebSocket Send Task", 4096, NULL, 5, NULL);
    assert(ret_val == pdPASS);
}

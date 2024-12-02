/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "driver/gpio.h"

static const char *TAG = "mqtt_example";

#define BLINK_GPIO 8

// MQTT topics (based on the Python code)
static const char *discovery_prefix = "homeassistant";
static char device_id[7];
static char object_id_switch[32], object_id_number[32], object_id_text[32];
static char config_topic_switch[64], config_topic_number[64], config_topic_text[64];
static char command_topic_switch[64], command_topic_number[64], command_topic_text[64];
static char state_topic_switch[64], state_topic_number[64], state_topic_text[64];

// Device state (same as in Python code)
struct device_state {
    bool switch_state;
    int number_value;
    char text[64];
} device_state;

// Forward declarations
static void publish_config(esp_mqtt_client_handle_t client);

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    //int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        esp_mqtt_client_subscribe(client, command_topic_switch, 0);
        esp_mqtt_client_subscribe(client, command_topic_number, 0);
        esp_mqtt_client_subscribe(client, command_topic_text, 0);
        publish_config(client);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        //msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        //ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        char payload[64];
        snprintf(payload, sizeof(payload), "%.*s", event->data_len, event->data);
        device_state.switch_state = strcmp(payload, "ON") == 0;
        printf("DEVICE_STATE=%i\n", device_state.switch_state);

        char topic_buffer[256]; // Allocate a buffer large enough to hold the topic
        snprintf(topic_buffer, event->topic_len + 1, "%.*s", event->topic_len, event->topic);
        
        if (strcmp(topic_buffer, command_topic_switch) == 0) {
            //device_state.switch_state = strcmp(payload, "ON") == 0;
            esp_mqtt_client_publish(client, state_topic_switch, device_state.switch_state ? "ON" : "OFF", 0, 0, true);
            ESP_LOGI(TAG, "Switch state updated to: %s", device_state.switch_state ? "ON" : "OFF");
        } else if (strcmp(topic_buffer, command_topic_number) == 0) {
            device_state.number_value = atoi(payload);
            esp_mqtt_client_publish(client, state_topic_number, payload, 0, 0, true);
            ESP_LOGI(TAG, "Number state updated to: %d", device_state.number_value);
        } else if (strcmp(topic_buffer, command_topic_text) == 0) {
            strncpy(device_state.text, payload, sizeof(device_state.text) - 1);
            esp_mqtt_client_publish(client, state_topic_text, device_state.text, 0, 0, true);
            ESP_LOGI(TAG, "Text state updated to: %s", device_state.text);
        }
        
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

// Function to publish configuration topics
static void publish_config(esp_mqtt_client_handle_t client) {
    char config_payload[1024];

    // Switch configuration
    snprintf(config_payload, sizeof(config_payload),
         "{\"name\": \"Simulated Switch\", \"command_topic\": \"%s\", \"state_topic\": \"%s\", \"unique_id\": \"%s\", \"device\": {\"identifiers\": [\"%s\"], \"name\": \"Device 0.9\", \"model\": \"ETERNAL\", \"manufacturer\": \"Ongaro Labs\"}, \"platform\": \"mqtt\", \"schema\": \"basic\"}",
         command_topic_switch, state_topic_switch, object_id_switch, device_id);
    esp_mqtt_client_publish(client, config_topic_switch, config_payload, 0, 0, true);

    // Number configuration
    snprintf(config_payload, sizeof(config_payload),
         "{\"name\": \"Simulated Number\", \"command_topic\": \"%s\", \"state_topic\": \"%s\", \"min\": 0, \"max\": 100, \"unique_id\": \"%s\", \"device\": {\"identifiers\": [\"%s\"], \"name\": \"Device 0.9\", \"model\": \"ETERNAL\", \"manufacturer\": \"Ongaro Labs\"}, \"platform\": \"mqtt\"}",
         command_topic_number, state_topic_number, object_id_number, device_id);
    esp_mqtt_client_publish(client, config_topic_number, config_payload, 0, 0, true);

    // Text configuration (for datetime)
    snprintf(config_payload, sizeof(config_payload),
         "{\"name\": \"Simulated Datetime Text\", \"command_topic\": \"%s\", \"state_topic\": \"%s\", \"unique_id\": \"%s\", \"device\": {\"identifiers\": [\"%s\"], \"name\": \"Device 0.9\", \"model\": \"ETERNAL\", \"manufacturer\": \"Ongaro Labs\"}, \"platform\": \"mqtt\"}",
         command_topic_text, state_topic_text, object_id_text, device_id);
    esp_mqtt_client_publish(client, config_topic_text, config_payload, 0, 0, true);

    ESP_LOGI(TAG, "Published configuration topics");
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
        .credentials.username = CONFIG_MQTT_USERNAME,
        .credentials.authentication.password = CONFIG_MQTT_PASSWORD, 
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}
static void set_led(void)
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(BLINK_GPIO, device_state.switch_state);
}

void led_control(void *pvParameters) {
    /* Configure the peripheral according to the LED type */
    configure_led();
    while (1) {
        set_led();
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}


void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    // Random device ID generation like in Python
    for (int i = 0; i < 6; i++) {
        device_id[i] = "abcdefghijklmnopqrstuvwxyz0123456789"[esp_random() % 36];
    }
    device_id[6] = '\0';

    // Set MQTT topics based on device_id
    snprintf(object_id_switch, sizeof(object_id_switch), "%s_switch", device_id);
    snprintf(object_id_number, sizeof(object_id_number), "%s_number", device_id);
    snprintf(object_id_text, sizeof(object_id_text), "%s_text", device_id);

    snprintf(config_topic_switch, sizeof(config_topic_switch), "%s/switch/%s/config", discovery_prefix, object_id_switch);
    snprintf(config_topic_number, sizeof(config_topic_number), "%s/number/%s/config", discovery_prefix, object_id_number);
    snprintf(config_topic_text, sizeof(config_topic_text), "%s/text/%s/config", discovery_prefix, object_id_text);

    snprintf(command_topic_switch, sizeof(command_topic_switch), "%s/switch/%s/set", discovery_prefix, object_id_switch);
    snprintf(command_topic_number, sizeof(command_topic_number), "%s/number/%s/set", discovery_prefix, object_id_number);
    snprintf(command_topic_text, sizeof(command_topic_text), "%s/text/%s/set", discovery_prefix, object_id_text);

    snprintf(state_topic_switch, sizeof(state_topic_switch), "%s/switch/%s/state", discovery_prefix, object_id_switch);
    snprintf(state_topic_number, sizeof(state_topic_number), "%s/number/%s/state", discovery_prefix, object_id_number);
    snprintf(state_topic_text, sizeof(state_topic_text), "%s/text/%s/state", discovery_prefix, object_id_text);

    mqtt_app_start();

    // Create a FreeRTOS task
    ESP_LOGI(TAG, "Started led_control");
    xTaskCreate(&led_control, "led_control", 2048, NULL, 5, NULL);
}

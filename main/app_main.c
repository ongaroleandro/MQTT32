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

#include <cJSON.h>

static const char *TAG = "mqtt_example";

#define BLINK_GPIO 8

// MQTT topics (based on the Python code)
static const char *discovery_prefix = "homeassistant";
static char device_id[7];
static char *config_topic = "homeassistant/light/6xalj9_light/config";
static char *command_topic = "homeassistant/light/6xalj9_light/set";
static char *state_topic = "homeassistant/light/6xalj9_light/state";

struct LightState {
    bool is_on;          // ON/OFF state
    uint16_t r;          // Red color component (0-4095)
    uint16_t g;          // Green color component (0-4095)
    uint16_t b;          // Blue color component (0-4095)
    uint16_t w;          // White color component (0-4095)
    uint16_t brightness; // Overall brightness (0-4095)
} stLightState;

// Forward declarations
static void publish_config(esp_mqtt_client_handle_t client);
static bool parse_mqtt_message(const char *payload, struct LightState *state);

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
        esp_mqtt_client_subscribe(client, command_topic, 0);
        publish_config(client);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        parse_mqtt_message(event->data,&stLightState);
        printf("DEVICE_STATE=%i\n", stLightState.is_on);
        esp_mqtt_client_publish(client, state_topic, event->data, event->data_len, 0, true);
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

char *create_config(void)
{
	char *string = NULL;
	cJSON *supported_color_modes = NULL;
	cJSON *supported_color_modes_string = NULL;
	
	cJSON *identifier = NULL;
	cJSON *identifier_string = NULL;
	
	cJSON *config = cJSON_CreateObject();
	
	if (cJSON_AddStringToObject(config, "name", "REGEBELEEGHT") == NULL)
	{
		goto end;
	}
	
	cJSON_AddStringToObject(config, "command_topic", "homeassistant/light/6xalj9_light/set");
	cJSON_AddStringToObject(config, "state_topic", "homeassistant/light/6xalj9_light/state");
	cJSON_AddStringToObject(config, "unique_id", "6xalj9_light");
	cJSON_AddStringToObject(config, "platform", "mqtt");
	
	//create device JSON
	cJSON *device = cJSON_CreateObject();
	identifier = cJSON_AddArrayToObject(device, "ids");
	identifier_string = cJSON_CreateString("6xalj9"); //could also use CJSON_PUBLIC(cJSON *) cJSON_CreateStringArray(const char *const *strings, int count); if more than 1 color
	cJSON_AddItemToArray(identifier, identifier_string);
	cJSON_AddStringToObject(device, "name", "OngaroLight");
	cJSON_AddStringToObject(device, "mf", "Ongaro");
	cJSON_AddStringToObject(device, "mdl", "blingbling");
	cJSON_AddStringToObject(device, "sw", "alpha");
	cJSON_AddNumberToObject(device, "sn", 124589);
	
	// add device JSON to the config JSON
	cJSON_AddItemToObject(config, "device", device);
	
	cJSON_AddStringToObject(config, "schema", "json");
	cJSON_AddTrueToObject(config, "brightness");
	cJSON_AddNumberToObject(config, "brightness_scale", 4095);
	supported_color_modes = cJSON_AddArrayToObject(config, "supported_color_modes");
	supported_color_modes_string = cJSON_CreateString("rgbw"); //could also use CJSON_PUBLIC(cJSON *) cJSON_CreateStringArray(const char *const *strings, int count); if more than 1 color
	cJSON_AddItemToArray(supported_color_modes, supported_color_modes_string);	
	string = cJSON_Print(config);
	printf("%s \n", string);
	
	
end:
	cJSON_Delete(config);
	return string;
}

bool parse_mqtt_message(const char *payload, struct LightState *state) {
    cJSON *root = cJSON_Parse(payload);
    if (root == NULL) {
        return false;
    }

    // Always check and set state
    cJSON *state_json = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (cJSON_IsString(state_json) && (state_json->valuestring != NULL)) {
        state->is_on = (strcmp(state_json->valuestring, "ON") == 0);
    }

    // Color parsing - only update if color is present
    cJSON *color_json = cJSON_GetObjectItemCaseSensitive(root, "color");
    if (cJSON_IsObject(color_json)) {
        cJSON *r = cJSON_GetObjectItemCaseSensitive(color_json, "r");
        cJSON *g = cJSON_GetObjectItemCaseSensitive(color_json, "g");
        cJSON *b = cJSON_GetObjectItemCaseSensitive(color_json, "b");
        cJSON *w = cJSON_GetObjectItemCaseSensitive(color_json, "w");

        state->r = cJSON_IsNumber(r) ? r->valueint : state->r;
        state->g = cJSON_IsNumber(g) ? g->valueint : state->g;
        state->b = cJSON_IsNumber(b) ? b->valueint : state->b;
        state->w = cJSON_IsNumber(w) ? w->valueint : state->w;
    }

    // Brightness parsing
    cJSON *brightness_json = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    state->brightness = cJSON_IsNumber(brightness_json) ? 
                        brightness_json->valueint : state->brightness;

    cJSON_Delete(root);
    return true;
}

// Function to publish configuration topics
static void publish_config(esp_mqtt_client_handle_t client) {
    char *my_config = create_config();
    esp_mqtt_client_publish(client, config_topic, my_config, 0, 0, true);
    ESP_LOGI(TAG, "Published configuration topics");
    free(my_config);
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
    gpio_set_level(BLINK_GPIO, stLightState.is_on);
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
    mqtt_app_start();

    // Create a FreeRTOS task
    ESP_LOGI(TAG, "Started led_control");
    xTaskCreate(&led_control, "led_control", 2048, NULL, 5, NULL);
}

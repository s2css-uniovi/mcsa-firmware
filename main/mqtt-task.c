/**
 * @brief File with the WiFi functions.
 * 
 * Most of the file is mostly copied from the expressif example:
 * 
 * esp-idf/examples/wifi/getting_started/station/main/station_example_main.c
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_check.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "secrets.h"
#include "mqtt-task.h"


/* The event group allows multiple bits for each event, but we only care one event:
 * - we are connected to the AP with an IP */
#define WIFI_CONNECTED_BIT BIT0


/************************************************************
 * MQTT topics
 ***********************************************************/
#define MQTT_TOPIC_BASE                  "pump-monitor"
#define MQTT_TOPIC_CMD_RUN               MQTT_TOPIC_BASE "/cmd/run"           // Topic to start sampling: 1: start, 0: stop (retained)
#define MQTT_TOPIC_CMD_SET_INPUT         MQTT_TOPIC_BASE "/cmd/input"         // Topic to select input: 1-4 (retained)
#define MQTT_TOPIC_CMD_SET_AVG_COUNT     MQTT_TOPIC_BASE "/cmd/avg-count"     // Topic to select the number of samples to average: 1-5 (retained)
#define MQTT_TOPIC_CMD_SET_SAMPLE_COUNT  MQTT_TOPIC_BASE "/cmd/sample-count"  // Topic to select the number of samples to read: 1-20000 (retained)
#define MQTT_TOPIC_CMD_SET_SAMPLING_FREQ MQTT_TOPIC_BASE "/cmd/sampling-freq" // Topic to select the sampling frequency: 1-40000 Hz (retained)

#define MQTT_TOPIC_DATA_SAMPLES          MQTT_TOPIC_BASE "/input%d/data"      // Topic to publish samples of 16 bits
#define MQTT_TOPIC_RUNNING_STATE         MQTT_TOPIC_BASE "/state"             // Topic to publish sampling state: 1: running, 0: stopped (retained)
#define MQTT_TOPIC_CONNECTION_STATE      MQTT_TOPIC_BASE "/connected"         // Topic to publish connection state: 1: connected, 0: disconnected (retained)


static const __attribute__((unused)) char *TAG = "MQTT";

// FreeRTOS event group to signal when the WiFi is connected
static EventGroupHandle_t s_wifi_event_group;

// Args of the task
static mqtt_task_args_t * s_args;
// The MQTT client handle
static esp_mqtt_client_handle_t s_client;
// The last will message
static const __attribute__((unused)) char * last_will_msg = "0";

/**
 * @brief Subscribes to MQTT topics
 */
static esp_err_t mqtt_subscribe_topics()
{
    const int qos = 2;
    ESP_RETURN_ON_FALSE(esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_CMD_RUN, qos) != -1,
                        ESP_ERR_INVALID_STATE, TAG, "Cannot subscribe to " MQTT_TOPIC_CMD_RUN);
    ESP_RETURN_ON_FALSE(esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_CMD_SET_INPUT, qos) != -1,
                        ESP_ERR_INVALID_STATE, TAG, "Cannot subscribe to " MQTT_TOPIC_CMD_SET_INPUT);
    ESP_RETURN_ON_FALSE(esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_CMD_SET_AVG_COUNT, qos) != -1,
                        ESP_ERR_INVALID_STATE, TAG, "Cannot subscribe to " MQTT_TOPIC_CMD_SET_AVG_COUNT);
    ESP_RETURN_ON_FALSE(esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_CMD_SET_SAMPLE_COUNT, qos) != -1,
                        ESP_ERR_INVALID_STATE, TAG, "Cannot subscribe to " MQTT_TOPIC_CMD_SET_SAMPLE_COUNT);
    ESP_RETURN_ON_FALSE(esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_CMD_SET_SAMPLING_FREQ, qos) != -1,
                        ESP_ERR_INVALID_STATE, TAG, "Cannot subscribe to " MQTT_TOPIC_CMD_SET_SAMPLING_FREQ);
    
    return ESP_OK;
}


/**
 * @brief Sends the connection/sampling state to the MQTT broker
 * 
 * @param topic The state topic
 * @param state The current state (1: running/connected, 0: stopped/disconnected)
 * @return ESP_OK in case of success, error code otherwise.
 */
static esp_err_t mqtt_publish_state(const char * topic, uint8_t state)
{
    ESP_RETURN_ON_FALSE(topic, ESP_ERR_INVALID_ARG, TAG, "null pointer");
    ESP_RETURN_ON_FALSE(state <= 9, ESP_ERR_INVALID_ARG, TAG, "invalid state");
    
    char state_str = '0' + state;
    ESP_RETURN_ON_FALSE(esp_mqtt_client_publish(s_client, topic, &state_str, 1, 0, 1) != -1, // Retained
                        ESP_ERR_INVALID_STATE, TAG, "Cannot publish state");
    return ESP_OK;
}


/**
 * @brief Event handler for the WiFi driver.
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "connect to the AP fail. Retry to connect");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


/*
 * @brief Event handler registered to receive MQTT events.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    const uint8_t DATA_MAX_LENGTH = 5;
    char buffer[DATA_MAX_LENGTH + 1];

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            mqtt_subscribe_topics(); // If failed, wait for reconnect
            mqtt_publish_state(MQTT_TOPIC_CONNECTION_STATE, 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);            

            if ((strncmp(event->topic, MQTT_TOPIC_CMD_RUN, event->topic_len) == 0) && (event->data_len == 1)) {
                uint8_t state = event->data[0] == '1';
                if (s_args->run(state) == ESP_OK) {
                    mqtt_publish_state(MQTT_TOPIC_RUNNING_STATE, state);
                }
            }
            else if ((strncmp(event->topic, MQTT_TOPIC_CMD_SET_INPUT, event->topic_len) == 0) && (event->data_len == 1)) {
                s_args->set_input(*(event->data) - '0');
            }
            else if ((strncmp(event->topic, MQTT_TOPIC_CMD_SET_AVG_COUNT, event->topic_len) == 0) && (event->data_len == 1)) {
                s_args->set_avg_count(*(event->data) - '0');
            }
            else if ((strncmp(event->topic, MQTT_TOPIC_CMD_SET_SAMPLE_COUNT, event->topic_len) == 0) && (event->data_len <= DATA_MAX_LENGTH)) {
                s_args->set_sample_count(atoi(strncpy(buffer, event->data, event->data_len)));
            }
            else if ((strncmp(event->topic, MQTT_TOPIC_CMD_SET_SAMPLING_FREQ, event->topic_len) == 0) && (event->data_len <= DATA_MAX_LENGTH)) {                
                s_args->set_sampling_freq(atoi(strncpy(buffer, event->data, event->data_len)));                
            }
        break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGI(TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                        strerror(event->error_handle->esp_transport_sock_errno));
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            } else {
                ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
            }
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}


/**
 * @brief Starts the WiFi STA.
 */
esp_err_t wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Cannot initialize esp_netif");

    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Cannot create default event loop");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Cannot initialize esp_wifi");

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            &instance_any_id), TAG, "Cannot register WIFI handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            &instance_got_ip), TAG, "Cannot register IP handler");

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. In case your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	        //.threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Cannot set WIFI STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Cannot set WIFI config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Cannot start esp_wifi");

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by wifi_event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to ap SSID: %s", WIFI_SSID);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    return ESP_ERR_INVALID_ARG;
}


static esp_err_t mqtt_start(esp_mqtt_client_config_t * config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "invalid argument: null pointer");
   
    // Set last will to inform about disconnection
    config->session.last_will.topic = MQTT_TOPIC_CONNECTION_STATE;
    config->session.last_will.msg = last_will_msg;
    config->session.last_will.qos = 0;  // MQTT QoS. Exactly one
    config->session.last_will.retain = 0;

    s_client = esp_mqtt_client_init(config);
    ESP_RETURN_ON_FALSE(s_client, ESP_ERR_INVALID_STATE, TAG, "Cannot intialize mqtt client");

    // The last argument may be used to pass data to the event handler
    ESP_RETURN_ON_ERROR(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL), TAG, "Cannot register mqtt handler");
    return esp_mqtt_client_start(s_client);
}


esp_err_t mqtt_publish_binary_samples(uint8_t input, uint8_t * data, size_t length)
{    
    ESP_RETURN_ON_FALSE(data, ESP_ERR_INVALID_ARG, TAG, "invalid argument: null pointer");

    char buffer[sizeof(MQTT_TOPIC_DATA_SAMPLES) + 3];
    sprintf(buffer, MQTT_TOPIC_DATA_SAMPLES, input);
    ESP_RETURN_ON_FALSE(esp_mqtt_client_publish(s_client, buffer, (const char*)data, length, 0, 0) != -1,
                        ESP_ERR_INVALID_STATE, TAG, "Cannot publish samples");
    return ESP_OK;
}


void mqtt_task(void * args)
{
    s_args = (mqtt_task_args_t *)args;

    assert (args && s_args->run && s_args->set_input && s_args->set_avg_count &&
            s_args->set_sample_count && s_args->set_sampling_freq );    

    // Start WiFi
    ESP_ERROR_CHECK(wifi_init_sta());

    // Start MQTT client
    ESP_ERROR_CHECK(mqtt_start(&(s_args->config)));

    // Publish initial state: stopped
    mqtt_publish_state(MQTT_TOPIC_RUNNING_STATE, 0);

    vTaskDelete(NULL);
}
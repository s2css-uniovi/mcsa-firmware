/**
 * @file main.c
 * @author Juan C. Granda
 * @date 23 Jun 2023
 * @brief Firmware for monitoring current.
 * @version 0.1 
 * 
 * 
 * @attention Make sure to configure sdkconfig with the following options to pin MQTT
 * task to PRO_CPU (core 0):
 * - CONFIG_MQTT_TASK_CORE_SELECTION_ENABLED=y
 * - CONFIG_MQTT_USE_CORE_0=y
 * @attention Make sure the following options are not set in sdkconfig:
 * - CONFIG_ESP_INT_WDT_CHECK_CPU1
 * - CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
 * - CONFIG_INT_WDT_CHECK_CPU1
 * @attention Make sure the following options are set in sdkconfig:
 * - CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
 * - CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "adc.h"
#include "buffer.h"
#include "sampling_task.h"
#include "sending_task.h"
#include "mqtt-task.h"
#include "secrets.h"


/************************************************************
 * Sampling defaults
 ***********************************************************/
#define DEFAULT_AVG_SAMPLE_COUNT 3
#define DEFAULT_SAMPLING_FREQ    ADC_MAX_SAMPLING_FREQ
#define DEFAULT_SAMPLE_COUNT     MAX_BUFFER_SIZE
#define DEFAULT_INPUT            3

static const __attribute__((unused)) char *TAG = "main";


/**
 * @brief The buffer of samples to use.
 * 
 * Because of memory limitations, only one buffer is used.
 */
static buffer_handle_t s_buffer = NULL;
static TaskHandle_t    s_xSamplingTaskHandle = NULL;
static TaskHandle_t    s_xSendingTaskHandle = NULL;
static TaskHandle_t    s_xMqttTaskHandle = NULL;
static uint8_t         s_average_sample_count = DEFAULT_AVG_SAMPLE_COUNT;
static uint32_t        s_sampling_freq_hz = DEFAULT_SAMPLING_FREQ;
static size_t          s_sample_count = DEFAULT_SAMPLE_COUNT;
static uint8_t         s_input = DEFAULT_INPUT;
static volatile bool   s_running = false;

/************************************************************
 * Task configurations
 * 
 * For simplicity, the configurations of the tasks are shared
 * between the main task and the worker tasks.
 ***********************************************************/
static sampling_task_args_t s_sampling_task_args;
static sending_task_args_t  s_sending_task_args;
static mqtt_task_args_t     s_mqtt_task_args;



static esp_err_t start_sampling()
{
    // Send notification to the sampling task
    ESP_RETURN_ON_FALSE(xTaskNotifyGive(s_xSamplingTaskHandle) == pdPASS, ESP_ERR_INVALID_STATE, TAG, "Cannot start sampling");
    return ESP_OK;    
}


/************************************************************
 * Worker task callbacks
 * 
 * These functions run in the worker tasks.
 ***********************************************************/
/**
 * @brief Starts/stops running the sampling+publishing.
 * 
 * This function runs in the MQTT task.
 */
esp_err_t run(bool start)
{
    ESP_RETURN_ON_FALSE(s_running != start, ESP_ERR_INVALID_STATE, TAG, "Already running or already stopped");
    s_running = start; // WARNING. Only this tasks modifies s_running

    if (s_running) {
        start_sampling();
    }
    return ESP_OK;
}

/**
 * @brief Sets the current ADC input.
 * 
 * This function runs in the MQTT task.
 */
esp_err_t set_input(uint8_t input)
{
    ESP_RETURN_ON_FALSE(!s_running, ESP_ERR_INVALID_STATE, TAG, "Cannot change input when running");
    esp_err_t ret = adc_set_input(input);
    if (ret == ESP_OK) {
        s_input = input;
    }
    return ret;
}

/**
 * @brief Sets the number of samples used for averaging.
 * 
 * This function runs in the MQTT task.
 */
esp_err_t set_avg_count(uint8_t count)
{
    ESP_RETURN_ON_FALSE(!s_running, ESP_ERR_INVALID_STATE, TAG, "Cannot change average count when running");
    ESP_RETURN_ON_FALSE((count > 0) && (count <= ADC_MAX_AVERAGING_COUNT), ESP_ERR_INVALID_ARG, TAG, "Invalid average count");
    
    s_sampling_task_args.averaging_count = count;
    return ESP_OK;
}

/**
 * @brief Sets the number of samples to read from the ADC.
 * 
 * This function runs in the MQTT task.
 */
esp_err_t set_sample_count(uint16_t count)
{
    ESP_RETURN_ON_FALSE(!s_running, ESP_ERR_INVALID_STATE, TAG, "Cannot change average count when running");
    ESP_RETURN_ON_FALSE((count > 0) && (count <= MAX_BUFFER_SIZE), ESP_ERR_INVALID_ARG, TAG, "Invalid sample count");

    ESP_RETURN_ON_ERROR(lock_buffer(s_buffer), TAG, "Cannot lock buffer");
    esp_err_t ret = resize_buffer(s_buffer, count);
    unlock_buffer(s_buffer);

    return ret;
}

/**
 * @brief Sets the sampling rate of the ADC.
 * 
 * This function runs in the MQTT task.
 */
esp_err_t set_sampling_freq(uint16_t freq)
{
    ESP_RETURN_ON_FALSE(!s_running, ESP_ERR_INVALID_STATE, TAG, "Cannot change average count when running"); 
    ESP_RETURN_ON_FALSE((freq > 0) && (freq <= ADC_MAX_SAMPLING_FREQ), ESP_ERR_INVALID_ARG, TAG, "Invalid sampling frequency");

    s_sampling_task_args.sampling_freq_hz = freq;
    return ESP_OK;
}

/**
 * @brief The function to pass buffers to the sampling and sending tasks (sampling and sending tasks).
 * 
 * This function runs in the worker task. It waits for a notification to return a buffer.
 */
static esp_err_t wait_buffer(buffer_handle_t * buffer)
{
    ESP_RETURN_ON_FALSE(buffer, ESP_ERR_INVALID_ARG, TAG, "invalid argument: null pointer");

    // Wait to be notified that the sampling may start.  Note
    // the first parameter is pdTRUE, which has the effect of clearing
    // the task's notification value back to 0, making the notification
    // value act like a binary semaphore.
    while (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 0) {}

    // Lock the buffer
    ESP_RETURN_ON_ERROR(lock_buffer(s_buffer), TAG, "Cannot lock buffer");
    *buffer = s_buffer;
    return ESP_OK;
}

/**
 * @brief The function to receive buffer of samples from the sampling task.
 * 
 * This function runs in the sampling task.
 */
static esp_err_t sampling_ready_buffer(buffer_handle_t buffer) {

    // Unlock buffer
    ESP_RETURN_ON_ERROR(unlock_buffer(s_buffer), TAG, "Cannot unlock buffer");

    // Send notification to the sending task to signal the ready buffer if already running
    if (s_running) {
        ESP_RETURN_ON_FALSE(xTaskNotifyGive(s_xSendingTaskHandle) == pdPASS, ESP_ERR_INVALID_STATE, TAG, "Cannot signal sending task");
    }

    return ESP_OK;
}

/**
 * @brief The function to publish samples.
 * 
 * This function runs in the sending task. The buffer should be locked before calling
 * this function.
 */
static esp_err_t sending_send_data(uint8_t * data, size_t length)
{
    ESP_RETURN_ON_FALSE(data, ESP_ERR_INVALID_ARG, TAG, "invalid argument: null pointer");
    ESP_RETURN_ON_FALSE(length, ESP_ERR_INVALID_ARG, TAG, "invalid argument: length");

    // Publish samples
    return mqtt_publish_binary_samples(s_input, data, length);
}

/**
 * @brief The function to pass buffers to the sending task.
 * 
 * This function runs in the sending task. It waits for a notification from the sampling
 * task to return a buffer.
 */
static esp_err_t sending_release_buffer(buffer_handle_t buffer)
{
    // Unlock buffer
    ESP_RETURN_ON_ERROR(unlock_buffer(s_buffer), TAG, "Cannot unlock buffer");

    // Wait for next sampling cycle
    // TODO: Configure using MQTT (-1 one shot)
    vTaskDelay(1000 * 5 / portTICK_PERIOD_MS);

    if (s_running) {
        return start_sampling();
    }

    return ESP_OK;
}



static esp_err_t init_sampling_task()
{
    // Default sampling configuration
    s_sampling_task_args.sampling_freq_hz = s_sampling_freq_hz;
    s_sampling_task_args.averaging_count  = s_average_sample_count;
    s_sampling_task_args.wait_buffer = wait_buffer;
    s_sampling_task_args.ready_buffer = sampling_ready_buffer;

    // The sampling task is pinned to the APP core to avoid blocking
    // the WiFi core during sampling
    BaseType_t ret = xTaskCreatePinnedToCore(sampling_task, "SamplingTask", 4096, &s_sampling_task_args, configMAX_PRIORITIES - 1, &s_xSamplingTaskHandle, APP_CPU_NUM);
    ESP_RETURN_ON_FALSE(ret == pdPASS, ESP_ERR_INVALID_STATE, TAG, "Cannot create sampling task");

    return ESP_OK;
}

static esp_err_t init_sending_task()
{
    // Task configuration
    s_sending_task_args.wait_buffer = wait_buffer;
    s_sending_task_args.send_data = sending_send_data;
    s_sending_task_args.release_buffer = sending_release_buffer;

    // The sending task is pinned to the APP core to avoid blocking the
    // WiFi core while sending a big chunk of data. It runs with lower
    // priority than the sampling task.
    BaseType_t ret = xTaskCreatePinnedToCore(sending_task, "SendingTask", 4096, &s_sending_task_args, tskIDLE_PRIORITY, &s_xSendingTaskHandle, APP_CPU_NUM);
    ESP_RETURN_ON_FALSE(ret == pdPASS, ESP_ERR_INVALID_STATE, TAG, "Cannot create sending task");

    return ESP_OK;
}

static esp_err_t init_mqtt_task()
{
    // Task configuration
    s_mqtt_task_args.config.broker.address.uri = "mqtt://" MQTT_BROKER_IP;
    //.verification.certificate = (const char *)mqtt_eclipseprojects_io_pem_start
    s_mqtt_task_args.config.credentials.username = MQTT_BROKER_USER;
    s_mqtt_task_args.config.credentials.authentication.password = MQTT_BROKER_PASS;
    s_mqtt_task_args.run = run;
    s_mqtt_task_args.set_input = set_input;
    s_mqtt_task_args.set_avg_count = set_avg_count;
    s_mqtt_task_args.set_sample_count = set_sample_count;
    s_mqtt_task_args.set_sampling_freq = set_sampling_freq;

    // The MQTT task is pinned to the PRO core like the WiFi task. It runs
    // with low priority.
    BaseType_t ret = xTaskCreatePinnedToCore(mqtt_task, "MqttTask", 4096, &s_mqtt_task_args, tskIDLE_PRIORITY, &s_xMqttTaskHandle, PRO_CPU_NUM);
    ESP_RETURN_ON_FALSE(ret == pdPASS, ESP_ERR_INVALID_STATE, TAG, "Cannot create mqtt task");

    return ESP_OK;
}




void app_main(void)
{    
    esp_log_level_set("*", ESP_LOG_INFO);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "[APP] Startup...");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    ESP_LOGI(TAG, "[APP] Initializing ADC");
    ESP_ERROR_CHECK(adc_init());

    ESP_LOGI(TAG, "[APP] Setting ADC input %d", s_input);
    ESP_ERROR_CHECK(adc_set_input(s_input));

    ESP_LOGI(TAG, "[APP] Allocating buffer with %ld samples", s_sample_count);
    ESP_ERROR_CHECK(allocate_buffer(s_sample_count, &s_buffer));

    ESP_LOGI(TAG, "[APP] Creating sending task");
    ESP_ERROR_CHECK(init_sending_task());

    ESP_LOGI(TAG, "[APP] Creating sampling task");
    ESP_ERROR_CHECK(init_sampling_task());

    ESP_LOGI(TAG, "[APP] Creating MQTT client task");
    ESP_ERROR_CHECK(init_mqtt_task());
}

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_check.h"
#include "buffer.h"


/**
 * @brief The hiddden definition of the sample buffer.
 * 
 */
struct sample_buffer_t {
	sample_t          samples[MAX_BUFFER_SIZE]; /**< The buffer containing the samples. */
	SemaphoreHandle_t xMutex;                   /**< The mutex to lock the buffer. */
	size_t            size;                     /**< The current size of the buffer in the range [1,MAX_BUFFER_SIZE]. */
};


static const __attribute__((unused)) char *TAG = "BUFFER";


esp_err_t allocate_buffer(size_t size, buffer_handle_t * buffer) {    
    ESP_RETURN_ON_FALSE((size > 0) && (size <= MAX_BUFFER_SIZE), ESP_ERR_INVALID_ARG, TAG, "invalid argument: buffer size");
    ESP_RETURN_ON_FALSE(buffer, ESP_ERR_INVALID_ARG, TAG, "invalid argument: null pointer");
    esp_err_t ret = ESP_OK;
    
    buffer_handle_t buf = malloc(sizeof(sample_buffer_t));
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "Cannot allocate buffer");

    buf->size = size;
    buf->xMutex = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(buf->xMutex != NULL, ESP_ERR_INVALID_STATE, CLEANUP_GOTO, TAG, "Cannot create mutex");

    *buffer = buf;
    return ret;

CLEANUP_GOTO:    
    free(buf);
    return ret;
}


size_t get_buffer_size(buffer_handle_t buffer) {
    assert(buffer);
    return buffer->size;
}


esp_err_t resize_buffer(buffer_handle_t buffer, size_t newSize) {
    ESP_RETURN_ON_FALSE(buffer, ESP_ERR_INVALID_ARG, TAG, "invalid argument: null pointer");
    ESP_RETURN_ON_FALSE((newSize > 0) && (newSize <= MAX_BUFFER_SIZE), ESP_ERR_INVALID_ARG, TAG, "invalid argument: new size");

    buffer->size = newSize;

    return ESP_OK;
}


esp_err_t lock_buffer(buffer_handle_t buffer) {
    ESP_RETURN_ON_FALSE(buffer, ESP_ERR_INVALID_ARG, TAG, "invalid argument: null pointer");

    ESP_RETURN_ON_FALSE(xSemaphoreTake(buffer->xMutex, portMAX_DELAY) == pdTRUE, ESP_ERR_INVALID_STATE, TAG, "Cannot take mutex");

    return ESP_OK;
}


sample_t * get_samples_ptr(buffer_handle_t buffer) {
    assert(buffer);
    return buffer->samples;    
}


esp_err_t unlock_buffer(buffer_handle_t buffer) {
    ESP_RETURN_ON_FALSE(buffer, ESP_ERR_INVALID_ARG, TAG, "invalid argument: null pointer");

    ESP_RETURN_ON_FALSE(xSemaphoreGive(buffer->xMutex) == pdTRUE, ESP_ERR_INVALID_STATE, TAG, "Cannot give mutex");
    return ESP_OK;
}

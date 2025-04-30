#include "assert.h"
#include "esp_log.h"
#include "sending_task.h"

static const __attribute__((unused)) char *TAG = "SENDING";


void sending_task(void *args)
{
    sending_task_args_t * sending_task_args = (sending_task_args_t *)args;
    buffer_handle_t buffer;

    assert (args && sending_task_args->wait_buffer && sending_task_args->send_data && sending_task_args->release_buffer);

    for (;;) {
        
        // Wait for buffer
        ESP_ERROR_CHECK(sending_task_args->wait_buffer(&buffer));
        assert(buffer);

        // Send the samples in the buffer
        ESP_LOGI(TAG, "Start sending...");
        if (sending_task_args->send_data((uint8_t *)get_samples_ptr(buffer), get_buffer_size(buffer) * sizeof(sample_t)) == ESP_OK) {
            ESP_LOGI(TAG, "Data sent. Payload: %d bytes", get_buffer_size(buffer) * sizeof(sample_t));
        }

        // Release the buffer
        ESP_ERROR_CHECK(sending_task_args->release_buffer(buffer));
    }
}
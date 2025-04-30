#include "freertos/FreeRTOS.h"
#include "assert.h"
#include "esp_log.h"
#include "sampling_task.h"
#include "adc.h"

static const __attribute__((unused)) char *TAG = "SAMPLING";


void sampling_task(void *args)
{
    sampling_task_args_t * sampling_task_args = (sampling_task_args_t *)args;
    buffer_handle_t buffer;

    assert (args && sampling_task_args->wait_buffer && sampling_task_args->ready_buffer);

    for (;;) {
        
        // Wait for buffer
        ESP_ERROR_CHECK(sampling_task_args->wait_buffer(&buffer));
        assert(buffer);

        ESP_LOGI(TAG, "Start sampling: %d samples (%d averaging) @ %d Hz", get_buffer_size(buffer), sampling_task_args->averaging_count, sampling_task_args->sampling_freq_hz);                
        ESP_ERROR_CHECK(adc_read_samples(get_samples_ptr(buffer), get_buffer_size(buffer), sampling_task_args->sampling_freq_hz, sampling_task_args->averaging_count));
        ESP_LOGI(TAG, "Sampling finished. Read %d samples", get_buffer_size(buffer));

        // Notify buffer is ready
        ESP_ERROR_CHECK(sampling_task_args->ready_buffer(buffer));
    }
}

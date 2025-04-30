/**
 * @file sampling_task.h
 * @author Juan C. Granda
 * @date 21 Jun 2023
 * @brief File defining the task for sampling the ADC.
 *
 * This file defines the function of the task sampling the ADC. Ideally,
 * it should be pinned to the APP core with high priority to achieve high
 * sampling rates.
 * 
 * The task recieves as arguments a function to wait for buffers to be fill
 * of ADC samples and a function to signal when the buffer is full.
 * 
 * The task waits for a buffer.When a buffer is available, the task reads the
 * ADC, fills the buffer and signals when it is full. Then, the process is
 * repeated.
 */
#ifndef __SAMPLING_TASK_H
#define __SAMPLING_TASK_H

#include <stdint.h>
#include "esp_err.h"
#include "buffer.h"


/**
 * @brief Arguments of the task function.
 */
typedef struct {
    uint32_t sampling_freq_hz;                     /**< Raw sampling frequency. */
    uint8_t averaging_count;                       /**< Number of raw samples to average to get a sample. */
    esp_err_t (*wait_buffer)(buffer_handle_t *);   /**< Function to wait for a buffer. */
    esp_err_t (*ready_buffer)(buffer_handle_t);    /**< Function to signal when a buffer is full. */
} sampling_task_args_t;


/*
 * @brief Task funtion for sampling the ADC.
 *
 * @param args The arguments of the task.
 */
void sampling_task(void * args);


#endif // __SAMPLING_TASK_H
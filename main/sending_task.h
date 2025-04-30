/**
 * @file sending_task.h
 * @author Juan C. Granda
 * @date 22 Jun 2023
 * @brief File defining the task for sending ADC samples using MQTT.
 *
 * This file defines the function of the task publishing the ADC samples.
 * 
 * The task recieves as arguments a function to wait for buffers, a function
 * to send the samples in a buffer and a function to release a buffer once
 * its samples were sent.
 * 
 * The task waits for a buffer. When a buffer is available, the task calls
 * @send_data to send the samples in the buffer and releases the buffer
 * after all samples are sent. Then, the process is repeated.
 */
#ifndef __SENDING_TASK_H
#define __SENDING_TASK_H

#include <stdint.h>
#include "esp_err.h"
#include "buffer.h"


/**
 * @brief Arguments of the task function.
 * 
 */
typedef struct {    
    esp_err_t (*wait_buffer)(buffer_handle_t *);   /**< Function to wait for a buffer. */
    esp_err_t (*send_data)(uint8_t *, size_t);     /**< Function to send data. */
    esp_err_t (*release_buffer)(buffer_handle_t);  /**< Function to release a buffer. */
} sending_task_args_t;


/*
 * @brief Task funtion for sending buffers of samples.
 *
 * @param args The arguments of the task.
 */
void sending_task(void * args);


#endif /* __SAMPLING_TASK_H */
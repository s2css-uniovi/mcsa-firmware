/**
 * @file mqtt.h
 * @author Juan C. Granda
 * @date 21 Jun 2023
 * @brief File containing the functions to connect receive and send MQTT messages.
 */
#ifndef __MQTT_H
#define __MQTT_H

#include "mqtt_client.h"


/**
 * @brief Arguments of the task function.
 */
typedef struct {
    esp_mqtt_client_config_t config;           /**< Configuration of the MQTT client. */
    esp_err_t (*run)(bool);                    /**< Function to start/stop sampling. */
    esp_err_t (*set_input)(uint8_t);           /**< Function to set current input. */
    esp_err_t (*set_avg_count)(uint8_t);       /**< Function to set the number of samples to average. */
    esp_err_t (*set_sample_count)(uint16_t);   /**< Function to set number of samples to read. */
    esp_err_t (*set_sampling_freq)(uint16_t);  /**< Function to set the sampling frequency. */
} mqtt_task_args_t;


/*
 * @brief Task funtion for receiving messages to the MQTT broker.
 *
 * @param args The arguments of the task.
 */
void mqtt_task(void * args);


/**
 * @brief Sends a binary data chunk to the MQTT broker
 * 
 * @param input The input where the samples were read.
 * @param data The binary data to send.
 * @param length The number of bytes to send.
 * @return ESP_OK in case of success, error code otherwise.
 */
esp_err_t mqtt_publish_binary_samples(uint8_t input, uint8_t * data, size_t length);


#endif // __MQTT_H
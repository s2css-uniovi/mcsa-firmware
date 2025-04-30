/**
 * @file adc.h
 * @author Juan C. Granda
 * @date 21 Jun 2023
 * @brief File defining the A/D converter to read analog samples.
 *
 * This file defines functions to initialize and read analog samples from
 * and AD4000 analog-to-digital converter. The output of a 4x1 multiplexer
 * is connected to the input of the ADC, so function @adc_set_input() is
 * provided to change between inputs to sample.
 */
#ifndef __ADC_H
#define __ADC_H

#include <stdint.h>
#include "esp_err.h"
#include "sample.h"


#define ADC_MAX_SAMPLING_FREQ  40000
#define ADC_MAX_AVERAGING_COUNT 5 // Maximum number of raw samples to average


/*
 * @brief Initilizes ADC
 *
 * This function initializes the SPI bus where the ADC is connected.
 *
 * @return ESP_OK in case of success, error code otherwise.
 */
esp_err_t adc_init();


/*
 * @brief Selects the ADC input to sample
 *
 * This function selects one of the four inputs of the ADC to sample. The ADC can only sample
 * one input at a time. Some delay is introduced so the ADC settles when switching inputs.
 *
 * @param input The input to sample in the range [0,3].
 * @return ESP_OK in case of success, error code otherwise.
 */
esp_err_t adc_set_input(uint8_t input);


/**
 * @brief Reads a buffer of samples from ADC.
 * 
 * This functions read consecutive samples from the ADC at a specific sampling frequency. It offers
 * the possibility to average raw samples to improve ADC resolution. Raw samples are sampled at
 * @sampling_freq_hz hertzs, so averaged samples are sampled at @sampling_freq_hz@/@averaging_count hertzs.
 * The SPI bus is locked while reading samples from the ADC.
 * 
 * @param adc_samples The buffer to store the samples.
 * @param length The number of samples to read.
 * @param sampling_freq_hz The raw sampling frequency in the range [0,ADC_MAX_SAMPLING_FREQ] hertzs.
 * @param averaging_count The number of raw samples in the range [1,ADC_MAX_AVERAGING_COUNT] to average to get a sample.
 * @return ESP_OK in case of success, error code otherwise.
 */
esp_err_t adc_read_samples(sample_t * adc_samples, uint16_t length, uint32_t sampling_freq_hz, uint8_t averaging_count);


#endif // #ifndef __ADC_H
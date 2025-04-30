/**
 * @file buffer.h
 * @author Juan C. Granda
 * @date 21 Jun 2023
 * @brief File defining a buffer of samples that can be shared between tasks.
 *
 * This file defines a buffer that can be locked and unlocked. IPC mechanisms
 * are used to make the access to the shared buffer thread-safe.
 * 
 * Firstly, the buffer must be allocated using @allocate_buffer(). Then, the
 * buffer may be resized using @resize_buffer(). The buffer can be locked
 * using @lock_buffer() and released using @unlock_buffer(). The samples stored
 * in the buffer can be read or written using @get_samples_ptr(). Make sure to 
 * lock the buffer before accessing the samples.
 */
#ifndef __BUFFER_H
#define __BUFFER_H

#include "esp_err.h"
#include "sample.h"


/**
 * @brief Maximum size of the sample buffer.
 * 
 */
#define MAX_BUFFER_SIZE   80000 / sizeof(sample_t)

/**
 * @brief Opaque definition of the sample buffer.
 * 
 */
typedef struct sample_buffer_t sample_buffer_t;

/**
 * @brief Data type for the buffer handle.
 */
typedef sample_buffer_t * buffer_handle_t;


/**
 * @brief Buffer allocation.
 * 
 * This function creates a buffer for storing samples.
 * 
 * This function is thread-safe.
 * 
 * @param size The size of the buffer in the range [1,MAX_BUFFER_SIZE] samples.
 * @param buffer A variable where the buffer handle is stored once it is allocated.
 * @return ESP_OK in case of success, error code otherwise.
 */
esp_err_t allocate_buffer(size_t size, buffer_handle_t * buffer);


/**
 * @brief Gets the current size of the buffer in samples.
 * 
 * This function is thread-safe.
 * 
 * @param buffer The handle of the buffer.
 * @return The number of samples in the buffer.
 */
size_t get_buffer_size(buffer_handle_t buffer);


/**
 * @brief Resizes the buffer.
 * 
 * This function changes the size of the buffer.
 * 
 * This function is not thread-safe. The task must lock the buffer first by using @lock_buffer().
 * 
 * @param buffer The handle of the buffer.
 * @param newSize The new size of the buffer in the range [1,MAX_BUFFER_SIZE] samples.
 * @return ESP_OK in case of success, error code otherwise.
 * @see lock_buffer().
 */
esp_err_t resize_buffer(buffer_handle_t buffer, size_t newSize);


/*
 * @brief Locks the sample buffer.
 *
 * This function locks the buffer. When the buffer is unlocked, the
 * function returns immediately. If not, the task is blocked until the buffer is released by
 * another task.
 * 
 * This function is thread-safe.
 *
 * @param buffer The handle of the buffer.
 * @return ESP_OK in case of success, error code otherwise.
 * @see unlock_buffer().
 */
esp_err_t lock_buffer(buffer_handle_t buffer);


/**
 * @brief Gets a pointer to the samples stored in the buffer.
 * 
 * This functions returns a pointer to the samples stored in the buffer. Samples can be
 * read and writter.
 * 
 * This function is not thread-safe. The task must lock the buffer first by using @lock_buffer().
 * 
 * @param buffer The handle to the buffer.
 * @return A pointer to the samples.
 * @see lock_buffer().
 */
sample_t * get_samples_ptr(buffer_handle_t buffer);


/*
 * @brief Releases the sample buffer.
 *
 * This function releases the buffer and returns immediately.
 *
 * This function is thread-safe.
 * 
 * @param buffer The handle of the buffer to release.
 * @return ESP_OK in case of success, error code otherwise.
 * @see lock_buffer().
 */
esp_err_t unlock_buffer(buffer_handle_t buffer);


#endif // __BUFFER_H
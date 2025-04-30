#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_check.h"
#include "adc.h"


//
// ADC configuration
//
#define ADC_SPI_HOST  SPI3_HOST
#define ADC_PIN_CS    5    
#define ADC_PIN_MISO  19
#define ADC_PIN_MOSI  23
#define ADC_SPI_SCLK  18
#define ADC_READ_CMD  0x54     // Command to read the ADC register (WEN R/W 0 1 0 1 0 0)
#define ADC_WRITE_CMD 0x14     // Command to write the ADC register (WEN R/W 0 1 0 1 0 0)
#define ADC_CALIBRATION_OFFSET 537  // LSB offset of the ADC

//
// Multiplexer configuration
//
#define MULT_PIN_A1   17
#define MULT_PIN_A0   16
#define MULT_MAX_INPUT 3
#define MULT_DEFAULT_INPUT MULT_MAX_INPUT


static const __attribute__((unused)) char *TAG = "ADC";
spi_device_handle_t adc_handle = NULL;


static esp_err_t spi_init() {
    ESP_RETURN_ON_FALSE(adc_handle == NULL, ESP_ERR_INVALID_STATE, TAG, "ADC already initialized");

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = -1,
        .miso_io_num = ADC_PIN_MISO,
        .sclk_io_num = ADC_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,          // Maximum transfer size (0 = default size)
    };
    
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_MASTER_FREQ_20M,   // SPI bus frequency
        .mode = 0,                               // SPI mode (0 = CPOL=0, CPHA=0)
        .spics_io_num = ADC_PIN_CS,              // Chip select pin
        .queue_size = 1,                         // Size of the SPI transactions queue
    };

    // CS needs a rising edge to start conversion. Force the edge to start conversion
    ESP_RETURN_ON_ERROR(gpio_set_direction(ADC_PIN_CS, GPIO_MODE_OUTPUT), TAG, "Failed configure CS pin");
    ESP_RETURN_ON_ERROR(gpio_set_level(ADC_PIN_CS, 0), TAG, "Failed to set CS low");
    esp_rom_delay_us(1); // Wait 1 microsecond
    ESP_RETURN_ON_ERROR(gpio_set_level(ADC_PIN_CS, 1), TAG, "Failed to set CS high");

    // The AD4000 ADC needs MOSI to be always high in CS mode, 3-wire without busy indicator. The idle state
    // of MOSI in the ESP32 is low, so MOSI is detached from the SPI bus configuration and set to high
    ESP_RETURN_ON_ERROR(gpio_set_direction(ADC_PIN_MOSI, GPIO_MODE_OUTPUT), TAG, "Failed to configure MOSI pin");
    ESP_RETURN_ON_ERROR(gpio_set_level(ADC_PIN_MOSI, 1), TAG, "Failed to set MOSI high");

    ESP_RETURN_ON_ERROR(spi_bus_initialize(ADC_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "Failed to initialize SPI bus");    
    ESP_RETURN_ON_ERROR(spi_bus_add_device(ADC_SPI_HOST, &dev_cfg, &adc_handle), TAG, "Failed to initialize SPI device");

    return ESP_OK;
}


static esp_err_t multiplexer_set_input(uint8_t input) {
    ESP_RETURN_ON_FALSE(input <= MULT_MAX_INPUT, ESP_ERR_INVALID_ARG, TAG, "invalid argument: multiplexer input");

    ESP_RETURN_ON_ERROR(gpio_set_level(MULT_PIN_A1, (input >> 1) & 1), TAG, "Failed to set A1");
    ESP_RETURN_ON_ERROR(gpio_set_level(MULT_PIN_A0, input & 1), TAG, "Failed to set A0");

    return ESP_OK;
}


static esp_err_t multiplexer_init() {
    ESP_RETURN_ON_ERROR(gpio_set_direction(MULT_PIN_A1, GPIO_MODE_OUTPUT), TAG, "Failed to configure A1 pin");
    ESP_RETURN_ON_ERROR(gpio_set_direction(MULT_PIN_A0, GPIO_MODE_OUTPUT), TAG, "Failed to configure A0 pin");
    
    ESP_RETURN_ON_ERROR(multiplexer_set_input(MULT_DEFAULT_INPUT), TAG, "Failed to set default multiplexer input");

    return ESP_OK;
}


esp_err_t adc_init() {
    ESP_RETURN_ON_ERROR(spi_init(), TAG, "Failed to initialize ADC");
    ESP_RETURN_ON_ERROR(multiplexer_init(), TAG, "Failed to initialize multiplexer");

    return ESP_OK;
}


esp_err_t adc_set_input(uint8_t input) {
    ESP_RETURN_ON_ERROR(multiplexer_set_input(input), TAG, "Failed to set multiplexer input");
    esp_rom_delay_us(200000);  // Delay 200 ms to settle ADC

    return ESP_OK;
}


static esp_err_t IRAM_ATTR adc_single_conversion(sample_t *adc_data) {
    ESP_RETURN_ON_FALSE(adc_data, ESP_ERR_INVALID_ARG, TAG, "invalid argument: null pointer");
    ESP_RETURN_ON_FALSE(adc_handle, ESP_ERR_INVALID_STATE, TAG, "ADC not initialized");

    esp_err_t ret;
    uint8_t buffer[4];
    spi_transaction_t t;

    memset(&t, 0, sizeof(t));
    t.length = 32;                  // Read size: 32 bits
    t.rx_buffer = buffer;

	ret = spi_device_polling_transmit(adc_handle, &t);
	*adc_data =  ((((uint16_t)buffer[0]) << 8) | buffer[1]) - (uint16_t)ADC_CALIBRATION_OFFSET; // TODO: MSB to LSB
	return ret;
}


esp_err_t IRAM_ATTR adc_read_samples(sample_t * adc_samples, uint16_t length, uint32_t sampling_freq_hz, uint8_t averaging_count) {
    ESP_RETURN_ON_FALSE(adc_samples, ESP_ERR_INVALID_ARG, TAG, "invalid argument: null pointer");
    ESP_RETURN_ON_FALSE((sampling_freq_hz > 0) && (sampling_freq_hz <= ADC_MAX_SAMPLING_FREQ), ESP_ERR_INVALID_ARG, TAG, "invalid argument: sampling frequency");
    ESP_RETURN_ON_FALSE((averaging_count > 0) && (averaging_count <= ADC_MAX_AVERAGING_COUNT), ESP_ERR_INVALID_STATE, TAG, "invalid argument: averaging count");
    ESP_RETURN_ON_FALSE(adc_handle, ESP_ERR_INVALID_STATE, TAG, "ADC not initialized");

    const uint32_t delay_us = 1000000 / sampling_freq_hz;
    uint32_t averaging_accum = 0;
    uint8_t  current_avg_count = 0;
    uint16_t sample_count = 0;
    sample_t sample;    
    int64_t next_us, now_us, t1_us, t2_us;
    esp_err_t ret = ESP_OK;

    // Lock the SPI bus
    ESP_RETURN_ON_ERROR(spi_device_acquire_bus(adc_handle, portMAX_DELAY), TAG, "Failed to lock SPI bus");

    // Disable interrupts
    portDISABLE_INTERRUPTS();  // NOTE: Watchdog timer may need to be disabled

    // HACK: Pre-fetch buffer to avoid memory latency
    memset(adc_samples, 0, length * sizeof(sample_t));

    // The first sample should be discarded. With the AD4000 ADC in CS mode 3-wire, samples are sampled
    // in the rising edge of CS and transmitted in the falling edge. Thus, an old sample is read in the
    // first SPI transaction
    adc_single_conversion(&sample);
    esp_rom_delay_us(1);  // Delay 1 microsecond

    t1_us = esp_timer_get_time();
    next_us = t1_us;

    do {
        ret = adc_single_conversion(&sample);
        if (ret != ESP_OK) {
            portENABLE_INTERRUPTS();  // Re-enable interrupts before logging
            ESP_GOTO_ON_ERROR(ret, CLEANUP_GOTO, TAG, "Failed to read ADC sample");
        }
        
        averaging_accum += (uint32_t) sample;
        current_avg_count++;       
        
        if (current_avg_count >= averaging_count) {
            adc_samples[sample_count] = (sample_t)((averaging_accum / current_avg_count) & 0xFFFF);
            sample_count++;
            current_avg_count = 0;
            averaging_accum = 0;
        }
        
        // Delay until next sample
        next_us = next_us + delay_us;
        now_us = esp_timer_get_time();
        if (now_us < next_us) {
            esp_rom_delay_us(next_us - now_us);
        }
        else {
            portENABLE_INTERRUPTS();  // Re-enable interrupts before logging
            ESP_LOGE(TAG, "Sample: %d", sample_count);
            ESP_GOTO_ON_ERROR(ESP_ERR_INVALID_STATE, CLEANUP_GOTO, TAG, "Too high sampling frequency. Real-time sampling failed");
        }
    } while(sample_count < length);

    t2_us = esp_timer_get_time();

CLEANUP_GOTO:
    if (ret == ESP_OK) {
        portENABLE_INTERRUPTS();
    }
    spi_device_release_bus(adc_handle);
    ESP_LOGD(TAG, "Samples read: %d", sample_count);
    ESP_LOGD(TAG, "Time elapsed: %lld us", t2_us - t1_us);    

    return ret;
}
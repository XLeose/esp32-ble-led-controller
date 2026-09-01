#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"
#include "nvs_flash.h"

// NimBLE Bluetooth Stack Headers
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Hardware Pin Definitions
#define LED_GPIO        GPIO_NUM_8   // Addressable RGB LED (WS2812 / NeoPixel) Data Pin
#define BUTTON_GPIO     GPIO_NUM_4   // Physical Push Button Interrupt Pin
#define ESP_INTR_FLAG_DEFAULT 0

// Device Name Placeholder (Customize with your own device name)
#define DEVICE_NAME     "YOUR_BLE_DEVICE_NAME" // e.g. "ESP32_C6_LED"

static const char *TAG = "BLE_LED_CTRL";
const char *device_name = DEVICE_NAME;

static led_strip_handle_t led_strip;
static QueueHandle_t ble_data_queue = NULL;
static QueueHandle_t gpio_evt_queue = NULL;
uint8_t own_addr_type;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_color_t;

// GPIO Interrupt Service Routine (ISR) for Button Press
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

// BLE GATT Characteristic Write Callback
static int ble_write_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    // Process only Write operations
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        // Extract received payload data and length
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0) {
            char temp_buf[32];
            uint16_t copy_len = len < (sizeof(temp_buf) - 1) ? len : (sizeof(temp_buf) - 1);
            os_mbuf_copydata(ctxt->om, 0, copy_len, temp_buf);
            temp_buf[copy_len] = '\0';
            ESP_LOGI(TAG, "Received Command: %s", temp_buf);

            int r_in, g_in, b_in;
            // Parse RGB payload format: rgb(r,g,b)
            if (sscanf(temp_buf, "rgb(%d,%d,%d)", &r_in, &g_in, &b_in) == 3)
            {
                rgb_color_t new_color;
                new_color.r = (r_in > 255) ? 255 : (r_in < 0 ? 0 : (uint8_t)r_in);
                new_color.g = (g_in > 255) ? 255 : (g_in < 0 ? 0 : (uint8_t)g_in);
                new_color.b = (b_in > 255) ? 255 : (b_in < 0 ? 0 : (uint8_t)b_in);
                
                xQueueSend(ble_data_queue, &new_color, 0);
                ESP_LOGI(TAG, "Successfully Set Color -> R:%d G:%d B:%d", new_color.r, new_color.g, new_color.b);
            }
            else
            {
                ESP_LOGW(TAG, "Unknown value entered. Expected format: rgb(r,g,b). Please retry!");
            }
        }
    }
    return 0;
}

// GATT Server Table (Services and Characteristics)
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xABCD), // Custom Primary Service UUID
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = BLE_UUID16_DECLARE(0x1234), // Custom Characteristic UUID
                .flags = BLE_GATT_CHR_F_WRITE,      // Write-Only from BLE Client
                .access_cb = ble_write_callback     // Callback executed on write event
            },
            {
                0, // End of characteristics
            }
        },
    },
    {
        0, // End of services
    },
};

// BLE Advertising Initialization
static void ble_app_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    
    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP; // BLE only (Required for ESP32-C6)
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // Connectable mode
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // Discoverable mode

    // Start BLE Advertising
    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
    ESP_LOGI(TAG, "BLE Advertising Started...");
}

// NimBLE Synchronization Callback (Executed when stack is ready)
static void ble_app_on_sync(void) {
    ble_hs_id_infer_auto(0, &own_addr_type); // Automatically determine address type
    ble_app_advertise();                     // Start advertising
}

// NimBLE Host Main Loop Task
void ble_host_task(void *param) {
    nimble_port_run(); // Run NimBLE event loop (blocking)
    nimble_port_freertos_deinit();
}

// Initialize WS2812 / NeoPixel Addressable LED Strip via RMT
static void configure_led(void)
{
    ESP_LOGI(TAG, "Configuring addressable RGB LED...");
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1, // On-board single RGB pixel
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    
    // Clear LED initially (turn off)
    led_strip_clear(led_strip);
}

// Configure Hardware Button Input on GPIO
static void configure_btn(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_POSEDGE // Interrupt on rising edge
    };

    gpio_config(&io_conf);
}

// FreeRTOS Task to handle LED Color updates from Queue
static void led_control_task(void *arg)
{
    rgb_color_t received_clr;
    for (;;)
    {
        if (xQueueReceive(ble_data_queue, &received_clr, portMAX_DELAY))
        {
            led_strip_set_pixel(led_strip, 0, received_clr.r, received_clr.g, received_clr.b);
            led_strip_refresh(led_strip);
        }
    }
}

// FreeRTOS Task to handle Button press events and turn off LED
static void button_task(void *arg)
{
    uint32_t io_num;
    for (;;)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "Button pressed! Clearing LED.");
            led_strip_clear(led_strip);
        }
    }
}

void app_main(void)
{
    // Initialize NVS Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create FreeRTOS Queues
    ble_data_queue = xQueueCreate(10, sizeof(rgb_color_t));
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // Configure Peripherals
    configure_btn();
    configure_led();

    // Create Tasks
    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
    xTaskCreate(led_control_task, "led_control_task", 4096, NULL, 5, NULL);

    // Initialize NimBLE Stack & Services
    nimble_port_init();
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(BUTTON_GPIO, gpio_isr_handler, (void*) BUTTON_GPIO);

    ble_svc_gap_device_name_set(device_name);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svr_svcs);
    ble_gatts_add_svcs(gatt_svr_svcs);
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(ble_host_task);
}

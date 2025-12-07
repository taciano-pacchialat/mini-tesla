#include "camera_driver.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "Camera";

esp_err_t camera_init(void)
{
    camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,

        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        // XCLK reference still required even if the module has its own oscillator
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        // Procesamiento local necesita frames crudos para convertir a HSV
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size = FRAMESIZE_QVGA,

        .jpeg_quality = 12,
        .fb_count = 2, // doble buffer para reducir tearing
        .grab_mode = CAMERA_GRAB_LATEST,
        .fb_location = CAMERA_FB_IN_PSRAM};

    // Initialize the camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return err;
    }

    // Get camera sensor
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL)
    {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }

    sensor->set_whitebal(sensor, 1); // AWB ON
    sensor->set_awb_gain(sensor, 1);
    sensor->set_wb_mode(sensor, 0); // Auto WB
    sensor->set_brightness(sensor, 2);
    sensor->set_contrast(sensor, 2);
    sensor->set_saturation(sensor, 1);
    sensor->set_hmirror(sensor, 1); // flip horizontal
    sensor->set_vflip(sensor, 1);   // flip vertical

    ESP_LOGI(TAG, "Camera initialized successfully");
    return ESP_OK;
}

camera_fb_t *camera_capture(void)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        return NULL;
    }
    return fb;
}

void camera_fb_return(camera_fb_t *fb)
{
    if (fb)
    {
        esp_camera_fb_return(fb);
    }
}

esp_err_t camera_deinit(void)
{
    return esp_camera_deinit();
}

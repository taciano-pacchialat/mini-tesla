# Vision Engine - Local Obstacle Detection

## Overview

The Vision Engine provides local obstacle detection capabilities using the onboard OV2640 camera. It implements a **safety veto system** that can override remote navigation commands when green obstacles are detected too close to the vehicle.

## Architecture

### Distributed Vision System

```
┌─────────────────────────────────────────────────────────────┐
│              ESP32-CAM Autonomous Vehicle                    │
├──────────────────────────┬──────────────────────────────────┤
│  Core 0 (Protocol CPU)   │  Core 1 (Application CPU)        │
├──────────────────────────┼──────────────────────────────────┤
│ • WiFi Stack             │ • Vision Processing Task         │
│ • WebSocket Client       │   - Camera capture (RGB565)      │
│ • Status TX              │   - HSV conversion               │
│                          │   - Green detection              │
│                          │   - Distance estimation          │
│                          │ • Control Task                   │
│                          │   - Fusion logic                 │
│                          │   - Veto system                  │
└──────────────────────────┴──────────────────────────────────┘
         ↓                            ↓
   Remote Telemetry           Local Vision Veto
   (Target tracking)          (Obstacle detection)
                    ↓
              ┌────────────┐
              │   FUSION   │ ← Safety Priority
              └────────────┘
                    ↓
              Motor Commands
```

### Safety Fusion Logic

```
if (local_veto_active && remote_command == FORWARD) {
    // VETO: Block forward motion
    motor.stop();
    log("OBSTACLE DETECTED - Veto active");
} else {
    // Execute remote command
    motor.execute(remote_command);
}
```

## Hardware Configuration

### ESP32-CAM (AI Thinker) Pin Mapping

**Camera Pins:**

- PWDN: GPIO 32
- RESET: -1 (not connected)
- XCLK: GPIO 0
- SIOD (I2C SDA): GPIO 26
- SIOC (I2C SCL): GPIO 27
- Y9-Y2: GPIOs 35, 34, 39, 36, 21, 19, 18, 5
- VSYNC: GPIO 25
- HREF: GPIO 23
- PCLK: GPIO 22

**Motor Pins (SD Card GPIOs):**

- Left Motor PWM: GPIO 12 (SD_D2)
- Left Motor DIR: GPIO 13 (SD_D3)
- Right Motor PWM: GPIO 14 (SD_CLK)
- Right Motor DIR: GPIO 15 (SD_CMD)

⚠️ **CRITICAL:** GPIO 12 is a bootstrap pin. Must be LOW at boot. SD card support must be disabled in menuconfig.

## Vision Processing Pipeline

### 1. Image Acquisition

- Format: **PIXFORMAT_RGB565** (16-bit raw, no compression)
- Resolution: **FRAMESIZE_QVGA** (320x240)
- Frame buffers: 2 (double buffering)
- Location: **PSRAM** (external memory)

### 2. Color Space Conversion

RGB565 → BGR888 → HSV

**Why HSV?**

- Hue channel is invariant to lighting changes
- Robust color segmentation under shadows/highlights

### 3. Green Obstacle Detection

**HSV Ranges for GREEN:**

```c
H: 40-80°   (Green hue range)
S: 50-255   (Avoid pale/washed colors)
V: 50-255   (Avoid dark shadows)
```

### 4. Morphological Filtering

- **Erosion** → Remove small noise pixels
- **Dilation** → Restore object size
- Kernel: 3x3

### 5. Contour Analysis

- Find connected components
- Filter by area:
  - Min: 200 px² (ignore noise)
  - Max: 50% of image (avoid false positives from lighting)
- Select largest valid contour

### 6. Distance Estimation

**Pinhole Camera Model:**

```
distance = (real_width × focal_length) / pixel_width
```

Parameters:

- `real_width`: 10 cm (known obstacle size)
- `focal_length`: 400 px (calibrated experimentally)

**Veto Threshold:** 25 cm

## Memory Configuration

### PSRAM Settings (ESP32 Classic)

```ini
CONFIG_ESP32_SPIRAM_SUPPORT=y
CONFIG_SPIRAM_MODE_QUAD=y          # QSPI (not Octal like S3)
CONFIG_SPIRAM_SPEED_40M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_CAMERA_FB_IN_PSRAM=y        # Frame buffers in external RAM
```

### Memory Usage

| Component    | Size     | Location |
| ------------ | -------- | -------- |
| RGB565 frame | 153.6 KB | PSRAM    |
| BGR888 frame | 230.4 KB | PSRAM    |
| HSV frame    | 230.4 KB | PSRAM    |
| Binary mask  | 76.8 KB  | PSRAM    |
| **Total**    | ~700 KB  | PSRAM    |

## Performance Optimization

### Zero-Copy Architecture

```c
camera_fb_t *fb = esp_camera_fb_get();
// Wrap camera buffer directly - no memcpy!
cv::Mat rgb565(fb->height, fb->width, CV_8UC2, fb->buf);
```

### Endianness Correction

OV2640 outputs Big Endian RGB565, but ESP32 is Little Endian.
Must byte-swap or use OpenCV channel permutation.

### C-Style OpenCV Macros

⚠️ **CRITICAL:** esp32-opencv uses C port. Use:

- `CV_RGB5652BGR` (not `cv::COLOR_RGB5652BGR`)
- `CV_BGR2HSV` (not `cv::COLOR_BGR2HSV`)
- `CV_RETR_EXTERNAL` (not `cv::RETR_EXTERNAL`)

## API Reference

### Initialization

```c
esp_err_t vision_engine_init(void);
esp_err_t vision_engine_start(void);
```

### Runtime

```c
// Check veto status (thread-safe)
bool vision_engine_is_veto_active(void);

// Get detailed detection result
vision_result_t result;
vision_engine_get_result(&result);
```

### Vision Result Structure

```c
typedef struct {
    bool obstacle_detected;
    float distance_cm;
    int centroid_x, centroid_y;
    int contour_area;
    uint32_t frame_count;
    uint32_t processing_time_ms;
} vision_result_t;
```

## Integration Example

### Main Control Loop

```c
void control_task(void *pvParameters) {
    while (1) {
        // Get local veto status
        bool local_veto = vision_engine_is_veto_active();

        // Get remote telemetry
        telemetry_data_t remote;
        xQueueReceive(telemetry_queue, &remote, timeout);

        // FUSION: Local veto takes priority
        autonomous_process_with_veto(&remote, local_veto);
    }
}
```

## Compilation Requirements

### CMakeLists.txt

```cmake
PRIV_REQUIRES esp32-camera
target_compile_options(${COMPONENT_LIB} PRIVATE
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
)
```

### idf_component.yml

```yaml
dependencies:
  espressif/esp32-camera: "^2.0.0"
```

### Partition Table

Use `huge_app` or custom 3MB app partition for OpenCV binary size.

## TODO: OpenCV Integration

The current implementation has the camera initialization and veto system ready, but **OpenCV processing is not yet implemented**.

To complete the vision system:

1. **Install esp32-opencv component:**

   ```bash
   idf.py add-dependency "espressif/esp32-opencv"
   ```

2. **Uncomment OpenCV code in `vision_engine.c`:**

   - Search for `TODO: OpenCV Processing Pipeline`
   - Uncomment the processing steps
   - Verify C-style macro usage

3. **Test and calibrate:**
   - Adjust HSV ranges for your green obstacle
   - Calibrate `CAMERA_FOCAL_LENGTH_PX` by measuring known distances
   - Fine-tune morphological kernel size

## Debugging

### Enable Debug Visualization

```c
vision_engine_set_debug(true);  // WARNING: Reduces FPS significantly
```

### Monitor Statistics

```c
float fps, avg_time;
vision_engine_get_stats(&fps, &avg_time);
ESP_LOGI(TAG, "Vision: %.1f FPS, avg %.1f ms", fps, avg_time);
```

### Common Issues

**Camera init fails:**

- Check GPIO pin mapping matches AI Thinker exactly
- Verify PSRAM is enabled in menuconfig

**Colors inverted:**

- Byte order issue (endianness)
- Apply `__builtin_bswap16` or use OpenCV channel mixing

**Low FPS:**

- Reduce resolution (already QVGA)
- Disable debug drawing
- Check PSRAM speed (40MHz recommended)

**False positives:**

- Adjust HSV ranges for lighting conditions
- Increase MIN_CONTOUR_AREA threshold
- Add more morphological filtering

## Safety Notes

⚠️ **The veto system is a last-resort safety mechanism, not a replacement for proper path planning.**

- Veto only triggers when obstacle is < 25cm (very close)
- Works best with high-contrast green objects
- Affected by lighting conditions (tune HSV ranges)
- Processing latency ~100ms (consider when moving fast)

## Performance Targets

- **Frame rate:** ~10 FPS (100ms per frame)
- **Detection range:** 20-200 cm
- **Distance accuracy:** ±5% on flat surface
- **Latency:** < 150 ms (capture + process + command)

---

**Status:** ✅ Camera initialized, ⏳ OpenCV processing pending, ✅ Veto system ready

# ESP32-CAM Autonomous Vehicle - Client Implementation

## 📋 Project Overview

ESP32-CAM based autonomous vehicle that connects to an ESP32-S3 vision server via WebSocket. The vehicle receives telemetry data (object detection, distance, angle) and autonomously navigates using reactive control logic.

**Architecture**: Native ESP-IDF implementation (NO Arduino/PlatformIO)

**Target Hardware**: ESP32 (non-S3) with OV2640 camera module and motor driver

---

## 🏗️ System Architecture

### Multi-Core Task Distribution

```
┌─────────────────────────────────────────────────────┐
│                    ESP32 Dual Core                   │
├──────────────────────────┬──────────────────────────┤
│  Core 0 (Protocol CPU)   │  Core 1 (Application CPU)│
├──────────────────────────┼──────────────────────────┤
│ • WiFi Stack             │ • Control Task (Prio 6)  │
│ • WebSocket Client       │   - Process Telemetry    │
│ • Status TX Task (Prio 4)│   - Motor Commands       │
│   - Send vehicle status  │ • Monitor Task (Prio 3)  │
│                          │   - System health check  │
└──────────────────────────┴──────────────────────────┘
              ↓                         ↓
        ┌──────────┐              ┌──────────┐
        │ WebSocket│              │  Motors  │
        │  Queue   │              │ (MCPWM)  │
        └──────────┘              └──────────┘
```

### Communication Flow

```
ESP32-S3 Server (192.168.4.1)
         │
         │ SoftAP: "ESP32-Vision-Bot"
         │
         ├─[WiFi Station]──► ESP32-CAM Client
         │
         └─[WebSocket ws://192.168.4.1/ws]
                  │
                  ├─► TEXT (JSON): Telemetry Data
                  │   {"detected": true, "distance_cm": 45.2, ...}
                  │
                  └─◄ TEXT (JSON): Vehicle Status
                      {"motors": {"left": 150, "right": 150}, ...}
```

---

## 🔧 Hardware Configuration

### GPIO Pin Mapping

| Function        | GPIO Pin | Description                      |
| --------------- | -------- | -------------------------------- |
| Motor Left PWM  | GPIO 25  | MCPWM0A - Speed control          |
| Motor Left DIR  | GPIO 26  | Direction (1=Forward, 0=Reverse) |
| Motor Right PWM | GPIO 27  | MCPWM1A - Speed control          |
| Motor Right DIR | GPIO 14  | Direction control                |
| Battery Monitor | GPIO 36  | ADC1_CH0 (optional)              |

**Note**: Pins can be modified in `motor_control/motor_control.h`

### Motor Driver Wiring

Supports common H-bridge motor drivers (L298N, TB6612FNG, etc.):

```
ESP32         Motor Driver        Motors
─────         ────────────        ──────
GPIO25 ─────► IN1 (Left PWM)
GPIO26 ─────► IN2 (Left DIR)     ─────► Motor Left
GPIO27 ─────► IN3 (Right PWM)
GPIO14 ─────► IN4 (Right DIR)    ─────► Motor Right
GND    ─────► GND
```

### Power Requirements

- **ESP32**: 3.3V regulated (via USB or external regulator)
- **Motors**: 5-12V (depends on motor spec) - **SEPARATE power supply**
- **Common ground**: Connect GND between ESP32 and motor driver

⚠️ **WARNING**: Never power motors directly from ESP32 3.3V/5V pins!

---

## 📦 Project Structure

```
software-auto-espidf/
├── CMakeLists.txt                 # Top-level build config
├── sdkconfig.defaults             # Mandatory ESP32 configuration
├── README.md                      # This file
└── main/
    ├── CMakeLists.txt             # Main component build config
    ├── main.c                     # Application entry point
    ├── wifi_station/
    │   ├── wifi_station.h
    │   └── wifi_station.c         # WiFi client to ESP32-S3
    ├── ws_client/
    │   ├── ws_client.h
    │   └── ws_client.c            # WebSocket bidirectional comms
    ├── motor_control/
    │   ├── motor_control.h
    │   └── motor_control.c        # MCPWM motor driver
    └── autonomous_task/
        ├── autonomous_task.h
        └── autonomous_task.c      # Reactive control logic
```

---

## 🚀 Build and Flash Instructions

### Prerequisites

1. **ESP-IDF v5.x** installed ([Installation Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/))
2. USB-to-Serial driver for ESP32-CAM (CP2102/CH340)
3. ESP32-CAM with PSRAM (recommended)

### Build Steps

```bash
# 1. Navigate to project directory
cd software-auto-espidf

# 2. Set ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# 3. Set target to ESP32 (not S3!)
idf.py set-target esp32

# 4. Configure project (optional - uses sdkconfig.defaults)
idf.py menuconfig

# 5. Build firmware
idf.py build

# 6. Flash to ESP32-CAM
idf.py -p /dev/ttyUSB0 flash

# 7. Monitor serial output
idf.py -p /dev/ttyUSB0 monitor
```

**Common Flash Ports**:

- Linux: `/dev/ttyUSB0` or `/dev/ttyACM0`
- macOS: `/dev/cu.usbserial-*`
- Windows: `COM3`, `COM4`, etc.

### Flashing ESP32-CAM

ESP32-CAM requires **GPIO0 pulled LOW** during boot to enter flash mode:

1. Connect **GPIO0 to GND** (jumper wire)
2. Press **RESET** button
3. Run `idf.py flash`
4. **Remove GPIO0-GND jumper** after flashing
5. Press **RESET** to run firmware

---

## ⚙️ Configuration

### WiFi Credentials

Edit `main/wifi_station/wifi_station.h`:

```c
#define WIFI_SSID       "ESP32-Vision-Bot"  // Must match ESP32-S3 server
#define WIFI_PASSWORD   "vision2025"
#define WIFI_SERVER_IP  "192.168.4.1"
```

### Motor Calibration

Adjust control parameters in `main/autonomous_task/autonomous_task.h`:

```c
#define DISTANCE_STOP_THRESHOLD_CM  30.0f   // Stop distance
#define BASE_SPEED_FOLLOW           150     // Follow speed (0-255)
#define SEARCH_TURN_SPEED           80      // Search rotation speed
#define ANGLE_CORRECTION_FACTOR     2.0f    // Steering gain
```

**Calibration Procedure**:

1. Place vehicle on test surface
2. Flash firmware and monitor serial output
3. Adjust `BASE_SPEED_FOLLOW` if motors are too fast/slow
4. Adjust `ANGLE_CORRECTION_FACTOR` if steering is over/under-responsive
5. Rebuild and reflash: `idf.py flash monitor`

---

## 🧠 Control Logic

### Behavior Modes

| State     | Condition                   | Motor Action                  |
| --------- | --------------------------- | ----------------------------- |
| SEARCHING | No object detected          | Rotate in place (80, -80)     |
| FOLLOWING | Object detected, 30-100 cm  | Proportional steering control |
| STOPPED   | Object detected, < 30 cm    | Emergency stop (0, 0)         |
| EMERGENCY | WiFi/WebSocket disconnected | Immediate motor stop          |

### Proportional Control Algorithm

```c
// Positive angle = turn right, Negative angle = turn left
int correction = (int)(angle_deg * ANGLE_CORRECTION_FACTOR);
int left_speed = BASE_SPEED_FOLLOW - correction;
int right_speed = BASE_SPEED_FOLLOW + correction;
```

**Example**:

- Target at +15° (to the right): Left=180, Right=120 → Turns right
- Target at -10° (to the left): Left=130, Right=170 → Turns left

---

## 📊 Serial Monitor Output

Expected console output after successful initialization:

```
[Main] ESP32-CAM Autonomous Vehicle Client
[Main] Vehicle ID: ESP32CAM_01
[WiFi] Initializing WiFi Station...
[WiFi] Connected to AP successfully
[WiFi] Got IP address: 192.168.4.2
[WebSocket] Connecting to WebSocket server: ws://192.168.4.1/ws
[WebSocket] WebSocket connected to server
[Motors] Initializing motor control...
[Motors] Left motor: PWM=GPIO25, DIR=GPIO26
[Motors] Right motor: PWM=GPIO27, DIR=GPIO14
[Control] Control task started on core 1
[Control] Telemetry: detected=1, object=Object, distance=54.3 cm, angle=12.5 deg
[Control] State transition: SEARCHING -> FOLLOWING
[Control] Motor commands: Left=125, Right=175
```

---

## 🐛 Troubleshooting

### Issue: Motors not responding

**Causes**:

1. GPIO pins not connected correctly
2. Motor driver not powered
3. Common ground missing

**Solution**:

```bash
# Check GPIO initialization in serial monitor
[Motors] Initializing motor control...
[Motors] Left motor: PWM=GPIO25, DIR=GPIO26  # Should see this

# Test motor directly in code (add to main.c after motor_control_init):
motor_set_speed(100, 100);  // Both motors forward at 100/255
vTaskDelay(pdMS_TO_TICKS(2000));
motor_emergency_stop();
```

### Issue: WiFi connection fails

**Causes**:

1. ESP32-S3 server not running in SoftAP mode
2. Wrong SSID/password
3. Server IP changed

**Solution**:

```bash
# Verify server is running and check SSID:
idf.py monitor
# Look for: [WiFi] Failed to connect to AP

# Verify credentials in wifi_station.h match server
```

### Issue: WebSocket disconnects frequently

**Causes**:

1. Weak WiFi signal
2. Server buffer overflow
3. Network congestion

**Solution**:

- Reduce status transmission rate in `main.c`: Change `vTaskDelay(pdMS_TO_TICKS(100))` to `200` or `500`
- Check server logs for errors
- Move vehicle closer to server during testing

---

## 📚 Dependencies

All dependencies are native ESP-IDF components (no external libraries required):

- `esp_wifi`: WiFi station mode
- `esp_netif`: Network interface abstraction
- `esp_websocket_client`: WebSocket protocol
- `json` (cJSON): JSON parsing
- `driver` (MCPWM): Motor control PWM

---

## 📖 References

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [MCPWM API Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/mcpwm.html)
- [WebSocket Client API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_websocket_client.html)

---

## 👥 Authors

Taller de Proyecto II - 2025-A1-AutoDobleCamara

---

## 🎯 Next Steps

1. **Test WiFi Connection**: Verify ESP32-S3 server is broadcasting SoftAP
2. **Test Motors Standalone**: Run motors at fixed speed before enabling autonomous mode
3. **Calibrate Control Gains**: Adjust `ANGLE_CORRECTION_FACTOR` for smooth steering
4. **Field Testing**: Test with actual ESP32-S3 vision system
5. **Add Sensors**: Integrate battery monitoring, IMU, or ultrasonic sensors

**Ready to compile!** Run `idf.py build flash monitor` 🚀

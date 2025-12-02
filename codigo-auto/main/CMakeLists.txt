idf_component_register(
    SRCS "main.c" 
         "wifi_station/wifi_station.c" 
         "ws_client/ws_client.c" 
         "motor_control/motor_control.c" 
         "autonomous_task/autonomous_task.c"
         "vision_engine/vision_engine.c"
    INCLUDE_DIRS "." 
                 "wifi_station" 
                 "ws_client" 
                 "motor_control" 
                 "autonomous_task"
                 "vision_engine"
    PRIV_REQUIRES esp_wifi 
                  nvs_flash 
                  esp_netif 
                  esp_event 
                  esp_websocket_client
                  json 
                  driver
                  esp32-camera
)

# Compiler flags for PSRAM and ESP32-CAM
target_compile_options(${COMPONENT_LIB} PRIVATE
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
)
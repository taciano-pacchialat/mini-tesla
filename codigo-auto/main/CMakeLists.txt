idf_component_register(
    SRCS "main.c" 
         "wifi_station/wifi_station.c" 
         "ws_client/ws_client.c" 
         "motor_control/motor_control.c" 
         "autonomous_task/autonomous_task.c"
    INCLUDE_DIRS "." 
                 "wifi_station" 
                 "ws_client" 
                 "motor_control" 
                 "autonomous_task"
    PRIV_REQUIRES esp_wifi 
                  nvs_flash 
                  esp_netif 
                  esp_event 
                  esp_websocket_client
                  json 
                  driver
                  esp_driver_mcpwm
)
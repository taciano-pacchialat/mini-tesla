#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_err.h"

/**
 * @brief Start the HTTP webserver with camera streaming
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t webserver_start(void);

/**
 * @brief Stop the HTTP webserver
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t webserver_stop(void);

#endif // WEBSERVER_H

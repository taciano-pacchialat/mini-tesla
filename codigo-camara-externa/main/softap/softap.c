#include "softap.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip_addr.h"
#include "esp_mac.h"
#include <string.h>

static const char *TAG = "SoftAP";
static esp_netif_t *ap_netif = NULL;

/**
 * @brief Manejador de eventos WiFi para SoftAP
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station %02x:%02x:%02x:%02x:%02x:%02x joined, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5],
                 event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station %02x:%02x:%02x:%02x:%02x:%02x left, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5],
                 event->aid);
    }
}

esp_err_t softap_init(void)
{
    esp_err_t ret;

    // Inicializar NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicializar TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Crear interfaz de red para AP
    ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL)
    {
        ESP_LOGE(TAG, "Failed to create default WiFi AP interface");
        return ESP_FAIL;
    }

    // Configurar IP estática para el SoftAP
    esp_netif_dhcps_stop(ap_netif);

    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));

    ip_info.ip.addr = ipaddr_addr(SOFTAP_IP_ADDR);
    ip_info.gw.addr = ipaddr_addr(SOFTAP_GATEWAY_ADDR);
    ip_info.netmask.addr = ipaddr_addr(SOFTAP_NETMASK_ADDR);

    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

    // Configuración WiFi por defecto
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Registrar manejador de eventos
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // Configurar SoftAP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = SOFTAP_SSID,
            .ssid_len = strlen(SOFTAP_SSID),
            .channel = SOFTAP_CHANNEL,
            .password = SOFTAP_PASSWORD,
            .max_connection = SOFTAP_MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    // Si no hay contraseña, usar modo abierto
    if (strlen(SOFTAP_PASSWORD) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "╔════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║        SoftAP Iniciado Exitosamente            ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ SSID:          %s", SOFTAP_SSID);
    ESP_LOGI(TAG, "║ Password:      %s", strlen(SOFTAP_PASSWORD) > 0 ? "********" : "OPEN");
    ESP_LOGI(TAG, "║ IP Address:    %s", SOFTAP_IP_ADDR);
    ESP_LOGI(TAG, "║ Channel:       %d", SOFTAP_CHANNEL);
    ESP_LOGI(TAG, "║ Max Clients:   %d", SOFTAP_MAX_CONNECTIONS);
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════╝");

    return ESP_OK;
}

esp_err_t softap_stop(void)
{
    ESP_LOGI(TAG, "Stopping SoftAP...");

    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to stop WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_deinit();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to deinit WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SoftAP stopped successfully");
    return ESP_OK;
}

uint8_t softap_get_connected_stations(void)
{
    wifi_sta_list_t sta_list;
    esp_wifi_ap_get_sta_list(&sta_list);
    return sta_list.num;
}

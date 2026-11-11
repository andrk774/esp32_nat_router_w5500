/* USB 4G/LTE modem uplink - see usb_modem_uplink.h */

#include "sdkconfig.h"

#if CONFIG_USB_MODEM_UPLINK

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "iot_usbh_cdc.h"
#include "iot_eth.h"
#include "iot_eth_netif_glue.h"
#include "usb_modem_uplink.h"

#if CONFIG_UPLINK_USB_MODEM_PPP
#include "iot_usbh_modem.h"
#elif CONFIG_USB_MODEM_NET_RNDIS
#include "iot_usbh_rndis.h"
#elif CONFIG_USB_MODEM_NET_ECM
#include "iot_usbh_ecm.h"
#endif

static const char *TAG = "usb_modem";

static esp_netif_t *s_netif = NULL;

static esp_err_t install_usb_host(void)
{
    usbh_cdc_driver_config_t cdc_cfg = {
        .task_stack_size = 4 * 1024,
        .task_priority = configMAX_PRIORITIES - 1,
        .task_coreid = 0,
        .skip_init_usb_host_driver = false,
    };
    return usbh_cdc_driver_install(&cdc_cfg);
}

#if CONFIG_UPLINK_USB_MODEM_PPP
/* Modems with known PPP / AT interface numbers. List taken from Espressif's
 * usb_cdc_4g_module example; an extra entry can be added from menuconfig
 * (USB_MODEM_CUSTOM_*) for modems that are not listed here. */
static const usb_modem_id_t s_modem_id_list[] = {
#if CONFIG_USB_MODEM_CUSTOM_VID
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, CONFIG_USB_MODEM_CUSTOM_VID, CONFIG_USB_MODEM_CUSTOM_PID},
     CONFIG_USB_MODEM_CUSTOM_MODEM_ITF, CONFIG_USB_MODEM_CUSTOM_AT_ITF, "Custom (menuconfig)"},
#endif
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x1782, 0x4d11}, 2, -1, "China Mobile ML302 / Fibocom MC610-EU"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x1E0E, 0x9011}, 5, -1, "SIMCom A7600C1 / A7670E"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x1E0E, 0x9205}, 2, -1, "SIMCom SIM7080G"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x2CB7, 0x0D01}, 2, 6,  "Fibocom LE270-CN"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x2C7C, 0x6001}, 4, -1, "Quectel EC600N-CN"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x2C7C, 0x0125}, 2, -1, "Quectel EC20 / EC25"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x19D1, 0x1003}, 2, -1, "YUGE YM310 X09"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x19D1, 0x0001}, 2, -1, "Luat Air780E"},
    {.match_id = {0}}, /* end of list */
};

static esp_err_t create_ppp_modem(void)
{
    usbh_modem_config_t modem_cfg = {
        .modem_id_list = s_modem_id_list,
        .at_tx_buffer_size = 256,
        .at_rx_buffer_size = 256,
    };
    /* Empty APN = leave the modem's own PDP context alone (works for most
     * SIMs, the operator pushes the APN). Set one if the modem stays offline. */
    if (strlen(CONFIG_USB_MODEM_APN) > 0) {
        modem_cfg.pdp.enable = true;
        modem_cfg.pdp.cid = 1;
        modem_cfg.pdp.type = "IP";
        modem_cfg.pdp.apn = CONFIG_USB_MODEM_APN;
        ESP_LOGI(TAG, "APN: %s", CONFIG_USB_MODEM_APN);
    }
    ESP_RETURN_ON_ERROR(usbh_modem_install(&modem_cfg), TAG, "usbh_modem_install failed");
    s_netif = usbh_modem_get_netif();
    ESP_RETURN_ON_FALSE(s_netif != NULL, ESP_FAIL, TAG, "modem netif is NULL");
    return ESP_OK;
}
#else /* network-card modem (RNDIS / ECM) */

static iot_eth_handle_t s_eth_handle = NULL;

static esp_err_t create_net_modem(void)
{
    /* Match any device; the driver itself only binds to interfaces of its
     * own class (RNDIS or CDC-ECM). The list is owned by the driver. */
    usb_device_match_id_t *match = calloc(2, sizeof(usb_device_match_id_t));
    ESP_RETURN_ON_FALSE(match != NULL, ESP_ERR_NO_MEM, TAG, "no mem for match list");
    match[0].match_flags = USB_DEVICE_ID_MATCH_VID_PID;
    match[0].idVendor = USB_DEVICE_VENDOR_ANY;
    match[0].idProduct = USB_DEVICE_PRODUCT_ANY;

    iot_eth_driver_t *drv = NULL;
    esp_err_t err;
#if CONFIG_USB_MODEM_NET_RNDIS
    iot_usbh_rndis_config_t cfg = { .match_id_list = match };
    err = iot_eth_new_usb_rndis(&cfg, &drv);
#else
    iot_usbh_ecm_config_t cfg = { .match_id_list = match };
    err = iot_eth_new_usb_ecm(&cfg, &drv);
#endif
    if (err != ESP_OK || drv == NULL) {
        free(match);
        ESP_LOGE(TAG, "failed to create USB %s driver: %s", usb_modem_uplink_name(), esp_err_to_name(err));
        return err != ESP_OK ? err : ESP_FAIL;
    }

    iot_eth_config_t eth_cfg = { .driver = drv, .stack_input = NULL };
    ESP_RETURN_ON_ERROR(iot_eth_install(&eth_cfg, &s_eth_handle), TAG, "iot_eth_install failed");

    /* Plain Ethernet-style netif with DHCP client: the modem acts as a
     * small router (typically 192.168.8.1 on Huawei HiLink). IP events are
     * the regular IP_EVENT_ETH_GOT_IP ones. */
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.if_key = "USB_MODEM";
    base.if_desc = "usb_modem";
    esp_netif_config_t netif_cfg = {
        .base = &base,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };
    s_netif = esp_netif_new(&netif_cfg);
    ESP_RETURN_ON_FALSE(s_netif != NULL, ESP_FAIL, TAG, "esp_netif_new failed");

    iot_eth_netif_glue_handle_t glue = iot_eth_new_netif_glue(s_eth_handle);
    ESP_RETURN_ON_FALSE(glue != NULL, ESP_FAIL, TAG, "netif glue failed");
    ESP_RETURN_ON_ERROR(esp_netif_attach(s_netif, glue), TAG, "netif attach failed");
    return ESP_OK;
}
#endif

esp_err_t usb_modem_uplink_init(esp_netif_t **out_netif)
{
    ESP_RETURN_ON_FALSE(out_netif != NULL, ESP_ERR_INVALID_ARG, TAG, "out_netif is NULL");
    ESP_LOGI(TAG, "USB modem uplink: %s", usb_modem_uplink_name());
    ESP_RETURN_ON_ERROR(install_usb_host(), TAG, "USB host / CDC driver install failed");
#if CONFIG_UPLINK_USB_MODEM_PPP
    /* The PPP driver dials as soon as it is installed, so creation is
     * deferred to usb_modem_uplink_start(); only the host stack goes up here. */
    *out_netif = NULL;
    return ESP_OK;
#else
    ESP_RETURN_ON_ERROR(create_net_modem(), TAG, "network-card modem init failed");
    *out_netif = s_netif;
    return ESP_OK;
#endif
}

esp_err_t usb_modem_uplink_start(void)
{
#if CONFIG_UPLINK_USB_MODEM_PPP
    return create_ppp_modem();
#else
    ESP_RETURN_ON_FALSE(s_eth_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "modem not initialised");
    return iot_eth_start(s_eth_handle);
#endif
}

esp_netif_t *usb_modem_uplink_netif(void)
{
    return s_netif;
}

const char *usb_modem_uplink_name(void)
{
#if CONFIG_UPLINK_USB_MODEM_PPP
    return "USB modem (PPP)";
#elif CONFIG_USB_MODEM_NET_RNDIS
    return "USB modem (RNDIS)";
#else
    return "USB modem (ECM)";
#endif
}

#endif /* CONFIG_USB_MODEM_UPLINK */

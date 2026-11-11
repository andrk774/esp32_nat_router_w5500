/* USB 4G/LTE modem uplink (ESP32-S2/S3/P4 USB OTG host)
 *
 * Two modem families are supported, selected in menuconfig
 * ("NAT Router Uplink" -> "Uplink device"):
 *
 *   UPLINK_USB_MODEM_PPP - modems that expose AT-command / PPP serial
 *                          interfaces (Quectel EC20/EC25, SIMCom A76xx,
 *                          Air780E, Fibocom, ...). Driver: iot_usbh_modem.
 *   UPLINK_USB_MODEM_NET - modems that enumerate as a USB network card
 *                          (RNDIS or CDC-ECM, e.g. Huawei E3372h HiLink).
 *                          Driver: iot_usbh_rndis / iot_usbh_ecm.
 *
 * Both end up as a plain esp_netif, which the rest of the router treats
 * exactly like the Ethernet uplink (ethNetif).
 */
#pragma once

#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Install the USB host stack and, for RNDIS/ECM, the modem driver and the
 * uplink esp_netif. Does not start traffic yet - call usb_modem_uplink_start().
 *
 * @param[out] out_netif uplink network interface (NULL for PPP: the PPP
 *             driver creates its netif when it is started)
 */
esp_err_t usb_modem_uplink_init(esp_netif_t **out_netif);

/**
 * Start the modem link (PPP dial-up / network card DHCP). Call after WiFi AP
 * has been configured, like esp_eth_start() on the Ethernet path.
 */
esp_err_t usb_modem_uplink_start(void);

/** Uplink netif. For PPP it only exists after usb_modem_uplink_start(). */
esp_netif_t *usb_modem_uplink_netif(void);

/** Human readable uplink name for status pages. */
const char *usb_modem_uplink_name(void);

#ifdef __cplusplus
}
#endif

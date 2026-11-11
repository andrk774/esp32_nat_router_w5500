# USB 4G modem uplink. This directory is only added to the build by the
# esp32-s3-usb-modem-* environments (-DEXTRA_COMPONENT_DIRS=usb_modem_components),
# so the USB host drivers stay out of every other build.
idf_component_register(SRCS "usb_modem_uplink.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_netif espressif__iot_eth
                       PRIV_REQUIRES espressif__iot_usbh_modem espressif__modem_at espressif__iot_usbh_rndis
                                     espressif__iot_usbh_ecm)

set(main_priv_requires
    pcap_capture esp_wifi esp_netif console nvs_flash driver esp_http_server
    fatfs cmd_router dhcpserver wpa_supplicant cmd_system acl remote_console
    syslog oled_display json esp_wireguard esp_eth http_server mqtt_ha app_update)

# ESP32-C5 uses a bit-bang WS2812 driver; all other targets use the RMT-backed
# espressif/led_strip managed component.
if(NOT IDF_TARGET STREQUAL "esp32c5")
    list(APPEND main_priv_requires led_strip)
endif()

list(APPEND main_priv_requires espressif__mdns)

idf_component_register(SRCS "esp32_nat_router.c"
                            "portmap.c"
                            "dhcp_manager.c"
                            "vpn_manager.c"
                            "netif_hooks.c"
                            "led_strip_status.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES ${main_priv_requires})
# USB modem uplink lives in usb_modem_components/ (see platformio.ini). sdkconfig
# is not known while component requirements are expanded, so it is linked here.
if(CONFIG_USB_MODEM_UPLINK)
    idf_build_get_property(build_components BUILD_COMPONENTS)
    if(NOT usb_modem_uplink IN_LIST build_components)
        message(FATAL_ERROR "CONFIG_USB_MODEM_UPLINK needs -DEXTRA_COMPONENT_DIRS=usb_modem_components")
    endif()
    idf_component_get_property(usb_modem_lib usb_modem_uplink COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PRIVATE ${usb_modem_lib})
endif()
# PPP hands received packets straight to ip4_input(), see netif_hooks.c
if(CONFIG_UPLINK_USB_MODEM_PPP)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=ip4_input")
endif()
target_compile_options(__idf_main PRIVATE -O2)
# Setup and event-handling files build for size; netif_hooks.c stays on -O2
# because it sits on the packet path. Source-level COMPILE_OPTIONS are emitted
# after the target options, so the later -Os wins for the listed files.
set_source_files_properties(esp32_nat_router.c portmap.c dhcp_manager.c vpn_manager.c led_strip_status.c
    PROPERTIES COMPILE_OPTIONS "-Os")

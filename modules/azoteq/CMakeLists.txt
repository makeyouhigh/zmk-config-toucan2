zephyr_include_directories(include)
add_subdirectory(drivers)
target_sources_ifdef(CONFIG_ZMK_SPLIT_ROLE_CENTRAL app PRIVATE src/toucan_touch_guard.c)

if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL AND CONFIG_ZMK_INPUT_SPLIT)
  target_sources(app PRIVATE src/toucan_packed_xy.c)
  zephyr_link_libraries(-Wl,--wrap=zmk_input_split_report_peripheral_event)
endif()

if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL AND CONFIG_ZMK_BLE AND CONFIG_ZMK_POINTING)
  target_sources(app PRIVATE src/toucan_ble_mouse.c)
  zephyr_link_libraries(-Wl,--wrap=zmk_hog_send_mouse_report)
endif()

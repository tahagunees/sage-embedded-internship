idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES
                           esp_wifi
                           esp_http_server
                           esp_event
                           esp_netif
                           nvs_flash
                           spi_flash
                           esp_timer
                           esp_adc
                           esp_phy
                           driver
                           esp_driver_gpio
                           esp_driver_ledc
                           heap
                           esp_hw_support
                           esp_system
                           freertos
                           log)

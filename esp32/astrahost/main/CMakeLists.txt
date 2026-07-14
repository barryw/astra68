set(astra_embed_args)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/astra68_provision.rom")
    list(APPEND astra_embed_args EMBED_FILES "astra68_provision.rom")
endif()

idf_component_register(
    SRCS "astra_host_main.c" "astra_rom.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_driver_sdspi fatfs sdmmc
                  esp_timer
    ${astra_embed_args}
)

if(astra_embed_args)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTRA_PROVISION_ROM=1)
endif()

if(DEFINED ASTRA_FPGA_SPI_HZ)
    target_compile_definitions(
        ${COMPONENT_LIB} PRIVATE ASTRA_FPGA_SPI_HZ=${ASTRA_FPGA_SPI_HZ}
    )
endif()

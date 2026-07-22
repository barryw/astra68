set(astra_embed_args)
if(ASTRA_PROVISION_ROM_ENABLED)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/astra68_provision.rom")
        message(FATAL_ERROR "provisioning enabled without an embedded ROM")
    endif()
    file(SIZE "${CMAKE_CURRENT_SOURCE_DIR}/astra68_provision.rom"
         astra_provision_size)
    if(astra_provision_size LESS 1)
        message(FATAL_ERROR "embedded provisioning ROM is empty")
    endif()
    list(APPEND astra_embed_args EMBED_FILES "astra68_provision.rom")
endif()

idf_component_register(
    SRCS "astra_host_main.c" "astra_rom.c" "astra_partition.c"
         "astra_block_policy.c" "astra_input.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_driver_sdspi fatfs sdmmc
                  esp_timer
    ${astra_embed_args}
)

if(astra_embed_args)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ASTRA_PROVISION_ROM=1)
endif()

if(ASTRA_PROVISION_REPLACE AND NOT ASTRA_PROVISION_ROM_ENABLED)
    message(FATAL_ERROR "ROM replacement requires provisioning to be enabled")
endif()

if(ASTRA_PROVISION_REPLACE)
    target_compile_definitions(
        ${COMPONENT_LIB} PRIVATE ASTRA_PROVISION_REPLACE=1
    )
endif()

if(DEFINED ASTRA_FPGA_SPI_HZ)
    target_compile_definitions(
        ${COMPONENT_LIB} PRIVATE ASTRA_FPGA_SPI_HZ=${ASTRA_FPGA_SPI_HZ}
    )
endif()

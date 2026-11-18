# Info Orbs firmware for the Pimoroni Pico LiPo 2 XL W Build: ./build.sh (writes build/orbs_xl.uf2)
cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pimoroni_pico_lipo2xl_w CACHE STRING "")
# 250 MHz instead of the chip's rated 150 (step 4, 2026-11-17). OFF = back to 150.
option(ORBS_OVERCLOCK "run the XL at 250 MHz" ON)
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "")
include(pico_sdk_import.cmake)
# mbedTLS 3.6.6 for HTTPS (third_party/mbedtls, upstream source, Apache-2.0), so the SDK's own copy isn't needed. Settings: src/mbedtls_config.h
set(PICO_MBEDTLS_PATH ${CMAKE_CURRENT_LIST_DIR}/third_party/mbedtls)
project(orbs_xl C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# OpenFontRender + its FreeType, exactly as the orbs' original firmware carried them (firmware/lib/OpenFontRender)
file(GLOB_RECURSE OFR_SOURCES ${CMAKE_CURRENT_LIST_DIR}/third_party/OpenFontRender/*.c
                              ${CMAKE_CURRENT_LIST_DIR}/third_party/OpenFontRender/*.cpp)
add_library(ofr STATIC ${OFR_SOURCES})
target_include_directories(ofr PUBLIC ${CMAKE_CURRENT_LIST_DIR}/third_party/OpenFontRender)
target_compile_definitions(ofr PRIVATE FT2_BUILD_LIBRARY)
target_compile_options(ofr PRIVATE -w $<$<COMPILE_LANGUAGE:CXX>:-include cstdint>)
target_link_libraries(ofr PUBLIC pico_stdlib)

add_executable(orbs_xl src/main.cpp src/screen.cpp src/tft.cpp src/clock_widget.cpp src/lcd.c src/net.c src/orbs_time.c
               src/settings.c src/assets.c src/https.c src/weather.cpp src/sets_new.cpp src/rig.cpp src/pics.cpp src/live.c src/presets.c src/jpegdec_glue.c src/camlogin.c src/frigate_cams.cpp src/settings_json.cpp src/web.cpp src/hotspot.c third_party/qrcodegen/qrcodegen.c third_party/tjpgd_bodmer/tjpgd.c)
target_include_directories(orbs_xl PRIVATE src third_party/ArduinoJson)
target_compile_options(orbs_xl PRIVATE -Wall -Wextra -Wno-unused-parameter $<$<COMPILE_LANGUAGE:CXX>:-include cstdint>)
set_source_files_properties(third_party/tjpgd_bodmer/tjpgd.c src/jpegdec_glue.c third_party/qrcodegen/qrcodegen.c PROPERTIES COMPILE_OPTIONS "-w")
# the build's name: YYMM.(day * 1440 + minute of the day) (build.sh re-runs cmake each time)
string(TIMESTAMP ORBS_YM "%y%m")
string(TIMESTAMP ORBS_D "%d")
string(TIMESTAMP ORBS_H "%H")
string(TIMESTAMP ORBS_M "%M")
foreach(v ORBS_YM ORBS_D ORBS_H ORBS_M)
	string(REGEX REPLACE "^0+([0-9])" "\\1" ${v} "${${v}}")
endforeach()
math(EXPR ORBS_V_MINOR "${ORBS_D} * 1440 + ${ORBS_H} * 60 + ${ORBS_M}")
message(STATUS "orbs firmware build ${ORBS_YM}.${ORBS_V_MINOR}")
file(WRITE ${CMAKE_BINARY_DIR}/orbs_xl.version "${ORBS_YM}.${ORBS_V_MINOR}\n")
target_compile_definitions(orbs_xl PRIVATE ORBS_BUILD="${ORBS_YM}.${ORBS_V_MINOR}")
# USB: its own product name ("Info Orbs XL": tools/orbs_xl.py finds it by that, never another Pico); a log line nobody reads gives up
# after 20 ms; plain "\n" line ends
target_compile_definitions(orbs_xl PRIVATE USBD_PRODUCT="Info Orbs XL" PICO_STDIO_USB_STDOUT_TIMEOUT_US=20000
    PICO_STDIO_ENABLE_CRLF_SUPPORT=0)
# step 4: USB buffers big enough that pictures keep flowing while the XL is busy (the SDK's default is 64 bytes each)
target_compile_definitions(orbs_xl PRIVATE CFG_TUD_CDC_RX_BUFSIZE=4096 CFG_TUD_CDC_TX_BUFSIZE=1024)
# the Wi-Fi: lwIP in the background (src/lwipopts.h), the network time client; the name to the router: info-orbs
target_compile_definitions(orbs_xl PRIVATE CYW43_HOST_NAME="info-orbs")
if(ORBS_OVERCLOCK)
	# the SDK sets 250 MHz at start-up (PLL 12 MHz x 125 = 1500 MHz / 6), raising the core to 1.20 V first (the XL bench
	# ran 250 MHz at 1.20 V, 2026-11-14). Everything else kept near its proven speed: flash /3 = 83 MHz (75 at 150 MHz),
	# PSRAM at most 84 MHz (/3 = 83; 75 at 150), the Wi-Fi chip's link /3 = 41.7 MHz (37.5 at 150; its limit is 50).
	target_compile_definitions(orbs_xl PRIVATE SYS_CLK_HZ=250000000 PLL_SYS_VCO_FREQ_HZ=1500000000 PLL_SYS_POSTDIV1=6
	    PLL_SYS_POSTDIV2=1 SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST=1 SYS_CLK_VREG_VOLTAGE_MIN=VREG_VOLTAGE_1_20
	    PICO_FLASH_SPI_CLKDIV=3 PICO_DEFAULT_PSRAM_MAX_FREQ=84000000 CYW43_PIO_CLOCK_DIV_INT=3 ORBS_LCD_HZ=42000000)
endif()
# PSRAM (8 MB, CS GP47 from the board header): its size checked at start-up, not taken on trust
target_compile_definitions(orbs_xl PRIVATE PICO_AUTO_DETECT_PSRAM_SIZE=1)
target_link_libraries(orbs_xl ofr pico_stdlib pico_rand hardware_spi hardware_dma hardware_flash hardware_psram hardware_adc hardware_vreg pico_flash pico_multicore
                      pico_cyw43_arch_lwip_threadsafe_background pico_lwip_sntp pico_lwip_mdns pico_lwip_mbedtls pico_mbedtls)
# USB serial for the link; it keeps the 1200-baud restart + picotool's reset interface (new firmware with the case shut)
pico_enable_stdio_usb(orbs_xl 1)
pico_enable_stdio_uart(orbs_xl 0)
pico_set_program_name(orbs_xl "info orbs XL")
pico_add_extra_outputs(orbs_xl)

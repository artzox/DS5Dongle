# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if (WIN32)
    set(USERHOME $ENV{USERPROFILE})
else ()
    set(USERHOME $ENV{HOME})
endif ()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif ()
# ====================================================================================

option(PICO_W_BUILD "Build for Rasberry Pi Pico W, audio is missing" OFF)
option(WAVESHARE_RP2350B_PLUS_W_BUILD "Build for Waveshare RP2350B-Plus-W (USB-C, 16MB flash, RM2 wireless)" OFF)

if (PICO_W_BUILD AND WAVESHARE_RP2350B_PLUS_W_BUILD)
        message(FATAL_ERROR "PICO_W_BUILD and WAVESHARE_RP2350B_PLUS_W_BUILD are mutually exclusive")
endif ()

if (PICO_W_BUILD)
        set(PICO_BOARD pico_w CACHE STRING "Board type")
        set(PICO_RP2350 OFF CACHE BOOL "Not using RP2350")
elseif (WAVESHARE_RP2350B_PLUS_W_BUILD)
        set(PICO_BOARD waveshare_rp2350b_plus_w CACHE STRING "Board type")
        # Waveshare board header is not in upstream pico-sdk; ship it here and
        # tell the SDK to search this directory when resolving PICO_BOARD.
        list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/boards/headers")
else ()
        set(PICO_BOARD pico2_w CACHE STRING "Board type")
endif ()

# Diagnostic: confirm PICO_BOARD resolution actually applied. Build log will
# show which board path was taken. Remove these once verified on hardware.
message(STATUS "DS5Dongle build target -- PICO_BOARD=${PICO_BOARD}")
message(STATUS "DS5Dongle build target -- PICO_W_BUILD=${PICO_W_BUILD}")
message(STATUS "DS5Dongle build target -- WAVESHARE_RP2350B_PLUS_W_BUILD=${WAVESHARE_RP2350B_PLUS_W_BUILD}")
message(STATUS "DS5Dongle build target -- PICO_BOARD_HEADER_DIRS=${PICO_BOARD_HEADER_DIRS}")
#set(PICO_CYW43_SUPPORTED 1)
#set(PICO_CYW43_ARCH_THREADSAFE_BACKGROUND 1)

option(ENABLE_SERIAL "Enable USB CDC serial printf output" OFF)
if (ENABLE_SERIAL)
        set(ENABLE_SERIAL_VALUE 1)
else ()
        set(ENABLE_SERIAL_VALUE 0)
endif ()
option(ENABLE_VERBOSE "Enable Log Verbose" OFF)
if(ENABLE_VERBOSE)
    set(ENABLE_VERBOSE_VALUE 1)
else()
    set(ENABLE_VERBOSE_VALUE 0)
endif ()
option(ENABLE_BATT_LED "Blink Pico LED when DualSense battery is low (<=10%)" ON)
if (ENABLE_BATT_LED)
        set(ENABLE_BATT_LED_VALUE 1)
else ()
        set(ENABLE_BATT_LED_VALUE 0)
endif ()
option(DISABLE_SPEAKER_PROC "Disable Speaker in controller" OFF)
if (DISABLE_SPEAKER_PROC)
        set(DISABLE_SPEAKER_PROC_VALUE 1)
else ()
        set(DISABLE_SPEAKER_PROC_VALUE 0)
endif ()
option(PICO_W_BUILD "Build for Rasberry Pi Pico W, audio is missing" OFF)
if (PICO_W_BUILD)
        set(CYW43_PIO_CLOCK_DIV_VALUE 3)
        set(SYS_CLOCK_KHZ_VALUE 200000)
        set(DISABLE_SPEAKER_PROC_VALUE 1)
else ()
        if(DISABLE_SPEAKER_PROC_VALUE)
            set(CYW43_PIO_CLOCK_DIV_VALUE 2)
            set(SYS_CLOCK_KHZ_VALUE 150000)
        else ()
            set(CYW43_PIO_CLOCK_DIV_VALUE 2)
            set(SYS_CLOCK_KHZ_VALUE 150000)
        endif ()
endif ()



# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(ds5-bridge C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(ds5-bridge
        src/usb_descriptors.cpp
        src/main.cpp
        src/usb.cpp
        src/audio.cpp
        src/bt.cpp
        src/config.cpp
        src/cmd.cpp
        src/state_mgr.cpp
)

if (ENABLE_BATT_LED)
        target_sources(ds5-bridge PRIVATE src/battery_led.cpp)
endif ()

# Cockos WDLResampler
add_library(wdl_resampler STATIC
        lib/WDL/WDL/resample.cpp
)
target_include_directories(wdl_resampler PUBLIC
        lib/WDL/WDL
)

# Opus
#set(OPUS_FIXED_POINT ON)
add_subdirectory(lib/opus)

# Phase A.3: Move libopus to RAM via .time_critical sections.
# libopus is built by add_subdirectory(lib/opus); the target `opus` is defined in
# the subdirectory's CMakeLists.txt, so we can't use POST_BUILD here. Instead we
# use a stamp-file custom command + custom target that runs after opus is built
# and before ds5-bridge links. Renames .text and .rodata of libopus.a to
# .time_critical.* which the Pico SDK linker places in RAM (loaded from flash
# at boot, executed from RAM at runtime). ~220 KB total relocated, ~5-6 ms boot
# time impact for the flash-to-RAM copy.
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp
    COMMAND ${CMAKE_OBJCOPY} --rename-section .text=.time_critical.opus_text --rename-section .rodata=.time_critical.opus_rodata --rename-section .rodata.str1.4=.time_critical.opus_strings $<TARGET_FILE:opus>
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp
    DEPENDS $<TARGET_FILE:opus>
    COMMENT "Phase A.3: relocating libopus to .time_critical.* sections"
    VERBATIM
)
add_custom_target(opus_ram_relocate ALL DEPENDS ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp)
add_dependencies(ds5-bridge opus_ram_relocate)

target_compile_options(wdl_resampler PRIVATE -fsigned-char)

# Phase A.2: Move WDL_Resampler hot-path methods to RAM via .time_critical sections.
# -ffunction-sections gives each function its own .text.<mangled_symbol> section.
# We then rename just the two hot-path methods (called every audio frame) to
# .time_critical.WDL_<name>, which the Pico SDK linker script places in RAM
# (loaded from flash at boot, executed from RAM at runtime). Other WDL methods
# stay in flash. No submodule modification required.
target_compile_options(wdl_resampler PRIVATE -ffunction-sections)
add_custom_command(TARGET wdl_resampler POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text._ZN13WDL_Resampler11ResampleOutEPfiii=.time_critical.WDL_ResampleOut
        --rename-section .text._ZN13WDL_Resampler15ResamplePrepareEiiPPf=.time_critical.WDL_ResamplePrepare
        $<TARGET_FILE:wdl_resampler>
    COMMENT "Phase A.2: relocating WDL_Resampler hot-path methods to .time_critical.*"
)

# Phase B.2: Move Tier-1 BT/USB hot-path functions to RAM via .time_critical
# sections. These live in pristine SDK/submodule sources (btstack, cyw43-driver,
# tinyusb) compiled directly into ds5-bridge, so they can't be annotated with
# __not_in_flash_func at the source. Since -ffunction-sections is already on,
# every function has its own .text.<name> section; we rename just the per-packet
# hot functions to .time_critical.<name> on the compiled object files in a
# PRE_LINK step (after compile, before link). The Pico SDK linker places
# .time_critical.* in RAM (LMA flash, VMA RAM). objcopy silently skips a section
# that is absent, so this stays idempotent across incremental relinks.
# Object paths mirror the absolute source path under the target's .dir; every
# source here lives under PICO_SDK_PATH.
set(_b2_objdir ${CMAKE_BINARY_DIR}/CMakeFiles/ds5-bridge.dir${PICO_SDK_PATH})
add_custom_command(TARGET ds5-bridge PRE_LINK
    # btstack: central HCI packet dispatch (runs for every HCI event / ACL packet)
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.packet_handler=.time_critical.bt_packet_handler
        ${_b2_objdir}/lib/btstack/src/hci.c.o
    # btstack: inbound L2CAP/ACL demux (carries the audio media channel)
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.l2cap_acl_handler=.time_critical.l2cap_acl_handler
        ${_b2_objdir}/lib/btstack/src/l2cap.c.o
    # tinyusb: USB audio data is copied through these FIFOs
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.tu_fifo_read=.time_critical.tu_fifo_read
        --rename-section .text.tu_fifo_read_n=.time_critical.tu_fifo_read_n
        --rename-section .text.tu_fifo_write=.time_critical.tu_fifo_write
        --rename-section .text.tu_fifo_write_n=.time_critical.tu_fifo_write_n
        --rename-section .text.tu_fifo_count=.time_critical.tu_fifo_count
        ${_b2_objdir}/lib/tinyusb/src/common/tusb_fifo.c.o
    # cyw43: PIO-SPI transport to the BT/WiFi chip (every byte to/from the radio)
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.cyw43_spi_transfer=.time_critical.cyw43_spi_transfer
        --rename-section .text.cyw43_read_bytes=.time_critical.cyw43_read_bytes
        --rename-section .text.cyw43_write_bytes=.time_critical.cyw43_write_bytes
        --rename-section .text.cyw43_read_reg_u32=.time_critical.cyw43_read_reg_u32
        --rename-section .text.cyw43_read_reg_u16=.time_critical.cyw43_read_reg_u16
        --rename-section .text.cyw43_read_reg_u8=.time_critical.cyw43_read_reg_u8
        --rename-section .text.cyw43_write_reg_u32=.time_critical.cyw43_write_reg_u32
        --rename-section .text.cyw43_write_reg_u16=.time_critical.cyw43_write_reg_u16
        --rename-section .text.cyw43_write_reg_u8=.time_critical.cyw43_write_reg_u8
        --rename-section .text.read_reg_u32_swap=.time_critical.read_reg_u32_swap
        --rename-section .text.write_reg_u32_swap=.time_critical.write_reg_u32_swap
        --rename-section .text.ns_delay.constprop.0=.time_critical.cyw43_ns_delay
        ${_b2_objdir}/src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.c.o
    # cyw43: SDPCM packet pump (parses/sends radio packets, BT work check)
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.cyw43_ll_sdpcm_poll_device=.time_critical.cyw43_ll_sdpcm_poll_device
        --rename-section .text.cyw43_ll_process_packets=.time_critical.cyw43_ll_process_packets
        --rename-section .text.cyw43_sdpcm_send_common=.time_critical.cyw43_sdpcm_send_common
        --rename-section .text.cyw43_do_ioctl.part.0=.time_critical.cyw43_do_ioctl
        --rename-section .text.cyw43_ll_bt_has_work=.time_critical.cyw43_ll_bt_has_work
        --rename-section .text.cyw43_ll_parse_async_event=.time_critical.cyw43_ll_parse_async_event
        ${_b2_objdir}/lib/cyw43-driver/src/cyw43_ll.c.o
    # cyw43: poll loop + BT HCI read/write over the shared bus
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.cyw43_poll_func=.time_critical.cyw43_poll_func
        --rename-section .text.cyw43_bluetooth_hci_read=.time_critical.cyw43_bluetooth_hci_read
        --rename-section .text.cyw43_bluetooth_hci_write=.time_critical.cyw43_bluetooth_hci_write
        ${_b2_objdir}/lib/cyw43-driver/src/cyw43_ctrl.c.o
    COMMENT "Phase B.2: relocating Tier-1 BT/USB hot functions to .time_critical.* (RAM)"
    VERBATIM
)

# Phase B.3: Tier-2 relocation -- the run-loop TX pumps and per-transfer submit
# paths. Same PRE_LINK objcopy mechanism as B.2. These fire every btstack/USB
# run-loop iteration (outbound ACL/L2CAP, USB task) or per audio transfer rather
# than strictly per received byte, so they are a notch less hot than Tier 1 but
# still on the audio path. state_update is intentionally NOT moved: it runs on
# the host->controller HID-OUT (rumble/LED) path, not the audio frame path, so
# it stays in flash to preserve RAM headroom.
add_custom_command(TARGET ds5-bridge PRE_LINK
    # btstack: HCI TX/state pump + outbound ACL (speaker/haptics) fragmentation
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.hci_run=.time_critical.hci_run
        --rename-section .text.hci_send_acl_packet_fragments=.time_critical.hci_send_acl_packet_fragments
        --rename-section .text.hci_send_acl_packet_buffer=.time_critical.hci_send_acl_packet_buffer
        ${_b2_objdir}/lib/btstack/src/hci.c.o
    # btstack: L2CAP TX pump + outbound send paths
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.l2cap_run=.time_critical.l2cap_run
        --rename-section .text.l2cap_send=.time_critical.l2cap_send
        --rename-section .text.l2cap_send_prepared=.time_critical.l2cap_send_prepared
        --rename-section .text.l2cap_notify_channel_can_send=.time_critical.l2cap_notify_channel_can_send
        ${_b2_objdir}/lib/btstack/src/l2cap.c.o
    # cyw43: backplane mem/reg accessors used inside the SDPCM/ioctl packet path
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.cyw43_ll_write_backplane_mem=.time_critical.cyw43_ll_write_backplane_mem
        --rename-section .text.cyw43_ll_read_backplane_mem=.time_critical.cyw43_ll_read_backplane_mem
        --rename-section .text.cyw43_write_backplane=.time_critical.cyw43_write_backplane
        --rename-section .text.cyw43_read_backplane.constprop.0=.time_critical.cyw43_read_backplane
        ${_b2_objdir}/lib/cyw43-driver/src/cyw43_ll.c.o
    # tinyusb: USB device task pump + endpoint transfer submit
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.tud_task_ext=.time_critical.tud_task_ext
        --rename-section .text.usbd_edpt_xfer=.time_critical.usbd_edpt_xfer
        --rename-section .text.usbd_edpt_xfer_fifo=.time_critical.usbd_edpt_xfer_fifo
        ${_b2_objdir}/lib/tinyusb/src/device/usbd.c.o
    # tinyusb: RP2350 DCD per-transfer submit + iso endpoint activation
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.dcd_edpt_xfer=.time_critical.dcd_edpt_xfer
        --rename-section .text.dcd_edpt_iso_activate=.time_critical.dcd_edpt_iso_activate
        ${_b2_objdir}/lib/tinyusb/src/portable/raspberrypi/rp2040/dcd_rp2040.c.o
    # tinyusb: audio class data path (read/write, TX packet sizing, xfer ISR)
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.tud_audio_n_read=.time_critical.tud_audio_n_read
        --rename-section .text.tud_audio_n_write=.time_critical.tud_audio_n_write
        --rename-section .text.audiod_calc_tx_packet_sz.isra.0=.time_critical.audiod_calc_tx_packet_sz
        --rename-section .text.audiod_xfer_isr=.time_critical.audiod_xfer_isr
        ${_b2_objdir}/lib/tinyusb/src/class/audio/audio_device.c.o
    COMMENT "Phase B.3: relocating Tier-2 BT/USB pump + transfer paths to .time_critical.* (RAM)"
    VERBATIM
)

# Phase B.4: Input-report path tail. The controller->host report path is now
# almost entirely in RAM; these are the small remaining flash functions on it.
# Cheap to move (~0.6 KiB) and removes any residual XIP miss when the cache was
# evicted by heavy flash activity (config write, BT reconnect). queue_try_* also
# back the audio inter-core FIFOs. Same PRE_LINK objcopy mechanism as B.2/B.3.
add_custom_command(TARGET ds5-bridge PRE_LINK
    # tinyusb HID: per-report submit, readiness check, xfer-complete callback
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.tud_hid_n_report=.time_critical.tud_hid_n_report
        --rename-section .text.tud_hid_n_ready=.time_critical.tud_hid_n_ready
        --rename-section .text.hidd_xfer_cb=.time_critical.hidd_xfer_cb
        ${_b2_objdir}/lib/tinyusb/src/class/hid/hid_device.c.o
    # pico_util: inter-core FIFO add/remove (input send_fifo + audio queues)
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.queue_try_add=.time_critical.queue_try_add
        --rename-section .text.queue_try_remove=.time_critical.queue_try_remove
        ${_b2_objdir}/src/common/pico_util/queue.c.o
    # pico_cyw43_arch: per-loop BT/WiFi poll entry
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.cyw43_arch_poll=.time_critical.cyw43_arch_poll
        ${_b2_objdir}/src/rp2_common/pico_cyw43_arch/cyw43_arch.c.o
    # btstack: run-loop data-source poll (drives BT servicing each iteration)
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text.btstack_run_loop_base_poll_data_sources=.time_critical.btstack_run_loop_base_poll_data_sources
        --rename-section .text.btstack_run_loop_poll_data_sources_from_irq=.time_critical.btstack_run_loop_poll_data_sources_from_irq
        ${_b2_objdir}/lib/btstack/src/btstack_run_loop.c.o
    COMMENT "Phase B.4: relocating input-report path tail to .time_critical.* (RAM)"
    VERBATIM
)

target_compile_options(ds5-bridge PRIVATE -fsigned-char)
target_compile_definitions(wdl_resampler PRIVATE
        WDL_RESAMPLE_TYPE=float
)
if(${SYS_CLOCK_KHZ_VALUE} GREATER 266000)
    set(PICO_FLASH_SPI_CLKDIV 4)
else()
    set(PICO_FLASH_SPI_CLKDIV 2)
endif()
target_compile_definitions(bs2_default PRIVATE PICO_FLASH_SPI_CLKDIV=${PICO_FLASH_SPI_CLKDIV})
target_compile_definitions(ds5-bridge PRIVATE PICO_EMBED_XIP_SETUP=1)
target_compile_definitions(ds5-bridge PRIVATE
        CYW43_LWIP=0
        ENABLE_SERIAL=${ENABLE_SERIAL_VALUE}
        PICO_FLASH_ASSUME_CORE1_SAFE=1
        WDL_RESAMPLE_TYPE=float
        CYW43_PIO_CLOCK_DIV_INT=${CYW43_PIO_CLOCK_DIV_VALUE}
        PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
        PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=0
        PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
        ENABLE_VERBOSE=${ENABLE_VERBOSE_VALUE}
        ENABLE_BATT_LED=${ENABLE_BATT_LED_VALUE}
        SYS_CLOCK_KHZ=${SYS_CLOCK_KHZ_VALUE}
        DISABLE_SPEAKER_PROC=${DISABLE_SPEAKER_PROC_VALUE}
)

option(ENABLE_WAKE_HID "Wake the Windows host on PS-button press" OFF)
if(ENABLE_WAKE_HID)
    target_compile_definitions(ds5-bridge PRIVATE
            ENABLE_WAKE_HID
    )
    target_sources(ds5-bridge PRIVATE
            src/wake.cpp
            src/ps_shortcut.cpp
     )
endif()

option(WAKE_DEBUG "Print wake-FSM transitions to UART (developer use)" OFF)
if(WAKE_DEBUG)
    target_compile_definitions(ds5-bridge PRIVATE
            WAKE_DEBUG
    )
endif()

set(VERSION "dev" CACHE STRING "Program version string")

pico_set_program_name(ds5-bridge "ds5-bridge")
pico_set_program_version(ds5-bridge "${VERSION}")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(ds5-bridge 1)
pico_enable_stdio_usb(ds5-bridge ${ENABLE_SERIAL_VALUE})

# Add the standard include files to the build
#target_include_directories(ds5-bridge PRIVATE
#        ${CMAKE_CURRENT_LIST_DIR}
#)

target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Add any user requested libraries
target_link_libraries(ds5-bridge
        pico_stdlib
        pico_multicore
        hardware_watchdog
        hardware_timer
        hardware_flash
        pico_btstack_classic
#        pico_cyw43_arch_threadsafe_background
        pico_cyw43_arch_poll
        pico_btstack_cyw43
        tinyusb_device
        tinyusb_board
        wdl_resampler
        opus
)

pico_add_extra_outputs(ds5-bridge)


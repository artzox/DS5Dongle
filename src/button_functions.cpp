//
// BOOTSEL button gestures, split out of bt.cpp.
//

#include "button_functions.h"

#include <cstdio>

#include "bt.h"
#include "pico/time.h"
#include "pico/flash.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "pico/bootrom.h"
#if PICO_RP2350
#include "hardware/regs/sio.h"
#endif

// Gesture thresholds, in samples at the 100 ms (10 Hz) poll cadence.
static constexpr int HOLD_SAMPLES = 15;         // ~1.5 s held -> clear all pairings
static constexpr int CLICK_WINDOW_SAMPLES = 5;  // ~500 ms allowed between clicks

// FSM: 0 idle, 1 pressing (counting for hold), 2 held (fired), 3 wait-for-next-click.
static int button_fsm = 0;
static int button_press_samples = 0;
static int button_wait_samples = 0;
static int button_click_count = 0;
static uint32_t button_last_check_ms = 0;

// Read BOOTSEL by briefly floating the QSPI CSn line. Must run with both cores
// in a known-safe state - if core 1 does an XIP read while CSn is floating it
// gets garbage and the CYW43 driver misbehaves (immediate BT disconnect on audio
// start). flash_safe_execute() handles the multicore coordination - the same SDK
// mechanism BTstack uses for its TLV flash writes.
static void __no_inline_not_in_flash_func(button_read_cb)(void *param) {
    bool *out = (bool *) param;
    const uint CS_PIN_INDEX = 1;

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    for (volatile int i = 0; i < 1000; ++i);

#if PICO_RP2350
    *out = !(sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS);
#else
    *out = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));
#endif

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
}

static bool button_read_bootsel() {
    bool pressed = false;
    // 100 ms timeout for the safe-state coordination; the poll is gated on the
    // 10 Hz cadence and the safe-execute parks core1, so this never runs during
    // active audio reads anyway.
    int rc = flash_safe_execute(button_read_cb, &pressed, 100);
    if (rc != PICO_OK) return false;
    return pressed;
}

// Act on a completed click sequence once the inter-click window closes.
static void button_dispatch(int clicks) {
    if (clicks <= 1) {
        bt_bootsel_click_action(); // single click -> pair / switch controller
    } else if (clicks == 2) {
        // double click -> normal reboot. A Cortex-M33 SYSRESETREQ does a warm
        // reset that re-runs the flash app; a watchdog reset instead drops the
        // RP2350 bootrom into BOOTSEL, which is why watchdog_reboot/enable bricked.
        printf("[BTN] BOOTSEL double click - reboot\n");
        *((volatile uint32_t *) 0xe000ed0c) = 0x05fa0004; // SCB AIRCR: VECTKEY | SYSRESETREQ
        __dsb();
        while (true) { tight_loop_contents(); } // wait for the reset
    } else {
        // triple click -> reboot into BOOTSEL (USB mass storage) for reflashing.
        printf("[BTN] BOOTSEL triple click - reboot to BOOTSEL\n");
        reset_usb_boot(0, 0); // noreturn
    }
}

// Poll BOOTSEL at 10 Hz and dispatch single / double / triple click + hold:
//   - hold (>= HOLD_SAMPLES, ~1.5 s) -> clear all pairings
//   - 1 click  -> pair / switch        2 clicks -> reboot        3 clicks -> BOOTSEL
// Clicks are counted across the inter-click window; the action fires when it closes.
// Also services the deferred blacklist persist on the same cadence.
void button_check() {
    // No connection gate: safe to poll during audio because button_read_bootsel()
    // uses flash_safe_execute(), which parks core1 (the audio core) for the QSPI
    // CSn float -- see flash_safe_execute_core_init() and PICO_FLASH_ASSUME_CORE1_SAFE=0.
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - button_last_check_ms < 100) return;
    button_last_check_ms = now;

    bt_blacklist_persist_if_dirty();

    bool pressed = button_read_bootsel();

    switch (button_fsm) {
        case 0: // IDLE - wait for the first press
            if (pressed) {
                button_fsm = 1;
                button_press_samples = 1;
            }
            break;
        case 1: // PRESSING - counting samples for hold
            if (pressed) {
                if (++button_press_samples >= HOLD_SAMPLES) {
                    button_click_count = 0;
                    button_fsm = 2;
                    bt_bootsel_hold_action();
                }
            } else {
                // released before the hold threshold -> count this click and
                // wait to see whether more clicks follow
                button_click_count++;
                button_fsm = 3;
                button_wait_samples = 0;
            }
            break;
        case 2: // HELD - hold already fired, wait for release
            if (!pressed) {
                button_fsm = 0;
                button_press_samples = 0;
            }
            break;
        case 3: // WAIT - released; watch for the next press inside the window
            if (pressed) {
                button_fsm = 1;
                button_press_samples = 1;
            } else if (++button_wait_samples >= CLICK_WINDOW_SAMPLES) {
                int clicks = button_click_count;
                button_click_count = 0;
                button_fsm = 0;
                button_press_samples = 0;
                button_dispatch(clicks);
            }
            break;
    }
}

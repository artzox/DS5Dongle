//
// Created by awalol on 2026/3/4.
//

#include <cstdio>
#include <cmath>
#include "bsp/board_api.h"
#include "bt.h"
#include "button_functions.h"
#include "utils.h"
#include "resample.h"
#include "audio.h"
#include "wake.h"
#ifdef ENABLE_WAKE_HID
#include "ps_shortcut.h"
#endif
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "state_mgr.h"
#if ENABLE_SERIAL
#include "pico/stdio_usb.h"
#endif
#include "config.h"
#include "cmd.h"
#include "dse.h"
#if ENABLE_BATT_LED
#include "battery_led.h"
#endif

// Pico SDK speciifically for waiting on conditions
#include "pico/critical_section.h"

int reportSeqCounter = 0;
uint8_t packetCounter = 0;
bool spk_active = false;

uint8_t interrupt_in_data[63] = {
    0x7f, 0x7d, 0x7f, 0x7e, 0x00, 0x00, 0xa7,
    0x08, 0x00, 0x00, 0x00, 0x52, 0x43, 0x30, 0x41,
    0x01, 0x00, 0x0e, 0x00, 0xef, 0xff, 0x03, 0x03,
    0x7b, 0x1b, 0x18, 0xf0, 0xcc, 0x9c, 0x60, 0x00,
    0xfc, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xa7, 0xad, 0x60, 0x00, 0x29, 0x18, 0x00,
    0x53, 0x9f, 0x28, 0x35, 0xa5, 0xa8, 0x0c, 0x8b
};

critical_section_t report_cs;
volatile bool report_dirty = false;

// Trigger activation dead zone (v1.8.0): mask what the HOST sees until the pull
// reaches the configured zone - analog forced to 0 and the digital press bit
// cleared, so games that fire on a hair-trigger register the action exactly where
// the resistance/detent/bow feel says they should. Applied ONLY to the outbound
// report copy: every internal consumer (AT gating, kick, shapes, gyro) keeps
// reading the raw trigger. Report body: [4]=L2 analog, [5]=R2 analog,
// [8] bit2=L2 pressed, bit3=R2 pressed. Zone N starts at N*25.5 counts.
static inline void apply_trigger_deadzone(uint8_t *r) {
    const auto &c = get_config();
    if (c.at_deadzone) {        // R2
        const uint8_t thr = (uint8_t)(((uint16_t)c.at_deadzone * 51u) / 2u);
        if (r[5] < thr) { r[5] = 0; r[8] &= (uint8_t)~0x08; }
    }
    if (c.at_l2_deadzone) {     // L2
        const uint8_t thr = (uint8_t)(((uint16_t)c.at_l2_deadzone * 51u) / 2u);
        if (r[4] < thr) { r[4] = 0; r[8] &= (uint8_t)~0x04; }
    }
}

void __not_in_flash_func(interrupt_loop)() {
    if (!tud_hid_ready()) return;

    // TODO: Refactor for better code reuse
    if (get_config().polling_rate_mode != 2) {
        const auto &cdz = get_config();
        if (cdz.at_deadzone || cdz.at_l2_deadzone) {
            static uint8_t dz_report[63];
            memcpy(dz_report, interrupt_in_data, 63);
            apply_trigger_deadzone(dz_report);
            if (!tud_hid_report(0x01, dz_report, 63)) {
                printf("[USBHID] tud_hid_report error\n");
            }
        } else if (!tud_hid_report(0x01, interrupt_in_data, 63)) {
            printf("[USBHID] tud_hid_report error\n");
        }
        return;
    }

    bool should_send = false;
    // Local buffer to hold the report data while we prepare it to send. 
    uint8_t safe_report[63];


    critical_section_enter_blocking(&report_cs);
    if (report_dirty) {
        memcpy(safe_report, interrupt_in_data, 63);
        report_dirty = false;
        should_send = true;
    }
    critical_section_exit(&report_cs);

    // Only send to TinyUSB if we actually grabbed fresh data
    if (should_send) {
        apply_trigger_deadzone(safe_report); // no-op when both dead zones are 0
        if (!tud_hid_report(0x01, safe_report, 63)) {
            printf("[USBHID] tud_hid_report error\n");

            // If the report failed to queue, restore the dirty flag 
            // so we try again on the next loop iteration.
            critical_section_enter_blocking(&report_cs);
            report_dirty = true;
            critical_section_exit(&report_cs);
        }
    }
}

// --- Gyro -> right-stick aiming ---------------------------------------------
// Adds the controller's angular velocity onto the right stick in the input
// report the PC sees, so ANY game gets gyro aiming with zero PC software
// (DSX needs its app running for this; here it lives in the dongle).
// Integer-only so it is safe inside the report critical section.
// Report offsets: RightStickX=2, RightStickY=3, TriggerLeft=4,
// AngularVelocityX(pitch)=15, Z(roll)=17, Y(yaw)=19 (int16 LE).
volatile uint16_t g_diag_gyro = 0; // |horizontal gyro raw|, field 0x35

// Gyro space modes
enum GyroSpaceMode {
    GYRO_SPACE_TRADITIONAL = 0,
    GYRO_SPACE_YAW_ROLL = 1,
    GYRO_SPACE_LOCAL = 2,
    GYRO_SPACE_PLAYER = 3,
    GYRO_SPACE_WORLD = 4
};

struct Vector3 { int32_t x; int32_t y; int32_t z; };
struct Quaternion { float w; float x; float y; float z; };

static inline Quaternion quat_identity() { return {1.0f, 0.0f, 0.0f, 0.0f}; }
static inline Quaternion quat_mul(const Quaternion &a, const Quaternion &b) {
    return {
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w
    };
}
static inline Quaternion quat_inverse(const Quaternion &q) {
    const float n = q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z;
    if (n == 0.0f) return quat_identity();
    const float inv = 1.0f / n;
    return { q.w * inv, -q.x * inv, -q.y * inv, -q.z * inv };
}
static inline Vector3 quat_rotate(const Quaternion &q, const Vector3 &v) {
    // q * (0, v) * q^-1
    const Quaternion qv{0.0f, (float)v.x, (float)v.y, (float)v.z};
    const Quaternion tmp = quat_mul(q, qv);
    const Quaternion res = quat_mul(tmp, quat_inverse(q));
    return {(int32_t)res.x, (int32_t)res.y, (int32_t)res.z};
}

// Runtime orientation (integrated from gyro). If a Mahony/IMU exists elsewhere
// it can be wired into these variables instead; by default a simple integrator
// from gyro deltas provides a usable orientation for the space transforms.
static Quaternion g_orientation = quat_identity();
static Quaternion g_player_reference = quat_identity();
static bool g_player_ref_set = false;
static bool g_prev_gyro_allowed = false;

static inline void integrate_orientation_from_gyro(const Vector3 &gyro, float dt) {
    // The raw gyro units are device-specific; use a small scale so integration is stable.
    const float scale = 0.0001f; // empirical small gain
    const float ax = gyro.x * scale * dt;
    const float ay = gyro.y * scale * dt;
    const float az = gyro.z * scale * dt;
    const float angle = sqrtf(ax*ax + ay*ay + az*az);
    if (angle < 1e-9f) return;
    const float s = sinf(angle * 0.5f) / angle;
    const Quaternion dq{ cosf(angle * 0.5f), ax * s, ay * s, az * s };
    g_orientation = quat_mul(dq, g_orientation);
    // normalize
    const float n = sqrtf(g_orientation.w*g_orientation.w + g_orientation.x*g_orientation.x + g_orientation.y*g_orientation.y + g_orientation.z*g_orientation.z);
    if (n > 0.0f) { g_orientation.w /= n; g_orientation.x /= n; g_orientation.y /= n; g_orientation.z /= n; }
}

static Vector3 apply_gyro_space(Vector3 gyro, const Quaternion &orientation, uint8_t mode) {
    switch (mode) {
        case GYRO_SPACE_TRADITIONAL:
            return gyro; // exact original behaviour
        case GYRO_SPACE_YAW_ROLL: {
            // horizontal = yaw, vertical = roll; ignore pitch
            // gyro layout: x = pitch(15), y = yaw(17), z = roll(19)
            return {0, gyro.y, gyro.z};
        }
        case GYRO_SPACE_LOCAL: {
            // local space: rotate into controller-local frame (inverse of orientation)
            Quaternion inv = quat_inverse(orientation);
            return quat_rotate(inv, gyro);
        }
        case GYRO_SPACE_PLAYER: {
            if (!g_player_ref_set) return gyro;
            // relative rotation: current * inverse(start)
            const Quaternion rel = quat_mul(orientation, quat_inverse(g_player_reference));
            // Convert relative quaternion to small-angle vector approximation.
            // Use vector part as axis*sin(theta/2); convert back to an angle-like measure.
            const float vx = rel.x, vy = rel.y, vz = rel.z, vw = rel.w;
            // angle ~ 2 * asin(|v|)
            const float vm = sqrtf(vx*vx + vy*vy + vz*vz);
            const float angle = 2.0f * asinf(fminf(1.0f, vm));
            if (vm < 1e-8f) return {0,0,0};
            const float ax = vx / vm, ay = vy / vm, az = vz / vm;
            // Map relative rotation to a comparable raw-scale vector (heuristic)
            const float scale = 5000.0f; // converts radians -> raw-ish units
            // horizontal -> yaw (ay * angle), vertical -> pitch (ax * angle)
            const int32_t horiz = (int32_t)(ay * angle * scale);
            const int32_t pitch = (int32_t)(ax * angle * scale);
            return {pitch, horiz, 0};
        }
        case GYRO_SPACE_WORLD: {
            // world space: rotate controller gyro into world coordinates
            return quat_rotate(orientation, gyro);
        }
        default:
            return gyro;
    }
}

static inline void __not_in_flash_func(apply_gyro_stick)(uint8_t *d) {
    const auto &cfg = get_config();
    if (cfg.gyro_mode == 0) return;
    // Activation schemes (industry set: ADS-gated, always-on, touch-enable, ratchet):
    //   1 = only while L2 (aim) held past ~12%
    //   2 = always on
    //   3 = only while the touchpad is touched (Steam 'touch to enable' style)
    //   4 = always on, touching the touchpad PAUSES gyro (ratchet: re-center like
    //       lifting a mouse)
    const bool touch = !(d[32] & 0x80);            // touchpad finger 1 down
    if (cfg.gyro_mode == 1 && d[4] < 30) return;                 // L2 held (aim)
    if (cfg.gyro_mode == 3 && !touch)    return;
    if (cfg.gyro_mode == 4 && touch)     return;
    // v1.11.0: additional gates for games that don't aim on L2. Same 30-count
    // threshold for the R2 analog gate; shoulders are digital (bit0=L1, bit1=R1).
    if (cfg.gyro_mode == 5 && d[5] < 30)         return;         // R2 held
    if (cfg.gyro_mode == 6 && !(d[8] & 0x01))    return;         // L1 held
    if (cfg.gyro_mode == 7 && !(d[8] & 0x02))    return;         // R1 held

    auto rd16 = [&](int off) -> int32_t {
        return (int16_t)((uint16_t)d[off] | ((uint16_t)d[off + 1] << 8));
    };

    // Raw sensor reads (preserve original mapping):
    const int32_t raw_pitch = rd16(15);
    const int32_t raw_yaw = rd16(17);
    const int32_t raw_roll = rd16(19);

    // Diagnostic uses the horizontal axis chosen by the old axis config for parity
    int32_t horiz_raw = (cfg.gyro_axis == 1) ? raw_roll /* roll */ : raw_yaw /* yaw */;
    { extern volatile uint16_t g_diag_gyro; int32_t ah = horiz_raw < 0 ? -horiz_raw : horiz_raw; g_diag_gyro = (ah > 65535) ? 65535 : (uint16_t)ah; }

    // Determine whether gyro is allowed (activation gates passed). We'll use this
    // to latch the player reference quaternion when PLAYER mode starts.
    const bool gyro_allowed = true; // we already returned if gates failed above

    // Integrate orientation from raw gyro for modes that use orientation.
    // Choose dt based on polling mode (approx): 250Hz=0.004s, 500Hz=0.002s, realtime=0.001s
    float dt = 0.004f;
    if (cfg.polling_rate_mode == 1) dt = 0.002f;
    if (cfg.polling_rate_mode == 2) dt = 0.001f;

    Vector3 raw_vec{ raw_pitch, raw_yaw, raw_roll };
    if (cfg.gyro_space_mode != GYRO_SPACE_TRADITIONAL) {
        integrate_orientation_from_gyro(raw_vec, dt);
    }

    // If PLAYER_SPACE and activation just started, capture reference
    if (cfg.gyro_space_mode == GYRO_SPACE_PLAYER && gyro_allowed && !g_prev_gyro_allowed) {
        g_player_reference = g_orientation;
        g_player_ref_set = true;
    }
    g_prev_gyro_allowed = gyro_allowed;

    // Apply gyro space transform
    const Vector3 transformed = apply_gyro_space(raw_vec, g_orientation, cfg.gyro_space_mode);

    // Apply small deadzone as before but on transformed values
    int32_t pitch = transformed.x;
    int32_t horiz = (cfg.gyro_axis == 1) ? transformed.z : transformed.y;
    if (horiz > -12 && horiz < 12) horiz = 0;
    if (pitch > -12 && pitch < 12) pitch = 0;
    if (horiz == 0 && pitch == 0) return;

    // Scale and convert to stick delta (preserve original scaling behavior)
    const int32_t s = cfg.gyro_sens;
    int32_t dx = -horiz * s / 200;    // turn controller right -> aim right
    int32_t dy = -pitch * s / 200;    // tilt up -> aim up (flip via invert if wrong)
    if (cfg.gyro_invert & 1) dx = -dx;
    if (cfg.gyro_invert & 2) dy = -dy;
    int32_t rx = (int32_t)d[2] + dx;
    int32_t ry = (int32_t)d[3] + dy;
    d[2] = (uint8_t)(rx < 0 ? 0 : (rx > 255 ? 255 : rx));
    d[3] = (uint8_t)(ry < 0 ? 0 : (ry > 255 ? 255 : ry));
}

void __not_in_flash_func(on_bt_data)(CHANNEL_TYPE channel, uint8_t *data, uint16_t len) {
    // printf("[Main] BT data callback: channel=%u len=%u\n", channel, len);
    if (channel == INTERRUPT && len > 2 && data[1] == 0x31) {
        // Mic audio: controller signals mic payload via bit1 of data[2];
        // the opus-encoded mic frame starts at data+4.
        if ((data[2] >> 1) & 1) {
            if (len >= 4) {
                mic_add_queue(data + 4, len - 4);
            }
            return;
        }
        if ((data[56] & 1) != (interrupt_in_data[53] & 1)) {
            set_headset(data[56] & 1);
        }

        // Wake-on-PS must observe every BT input report regardless of polling
        // mode: the wake feature has its own state to maintain (button-byte
        // diff for edge detection) and short-circuiting it on non-2 polling
        // modes silently breaks wake while the host is suspended.
        wake_on_bt_input(data + 3, len - 3);
        #ifdef ENABLE_WAKE_HID
        ps_shortcut_tick(data + 3, len - 3);
        #endif

        if (get_config().polling_rate_mode != 2) {
            memcpy(interrupt_in_data, data + 3, 63);
            apply_gyro_stick(interrupt_in_data);
            { extern volatile uint8_t g_l2_pos, g_r2_pos, g_l1_btn, g_r1_btn; g_l2_pos = interrupt_in_data[4]; g_r2_pos = interrupt_in_data[5]; g_l1_btn = (interrupt_in_data[8] & 0x01) ? 1 : 0; g_r1_btn = (interrupt_in_data[8] & 0x02) ? 1 : 0; } // L2@4 R2@5 L1/R1@8
#if ENABLE_BATT_LED
            battery_led_note_report();
#endif
            return;
        }

        // We add the critical section here to avoid any race conditions when writing to the interrupt_in_data buffer,
        // which is shared between the main loop and this callback.
        // The critical section ensures that only one thread can access the buffer at a time,
        // preventing data corruption and ensuring thread safety.
        // We also set the report_dirty flag to true to indicate that new data is available
        //  and needs to be sent in the next interrupt report.
        critical_section_enter_blocking(&report_cs);
        memcpy(interrupt_in_data, data + 3, 63);
        apply_gyro_stick(interrupt_in_data);
        report_dirty = true;
        critical_section_exit(&report_cs);
        { extern volatile uint8_t g_l2_pos, g_r2_pos, g_l1_btn, g_r1_btn; g_l2_pos = data[3 + 4]; g_r2_pos = data[3 + 5]; g_l1_btn = (data[3 + 8] & 0x01) ? 1 : 0; g_r1_btn = (data[3 + 8] & 0x02) ? 1 : 0; } // L2@4 R2@5 L1/R1@8
#if ENABLE_BATT_LED
        battery_led_note_report();
#endif
    }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
#ifdef ENABLE_WAKE_HID
    if (itf == 1) {
        if (reqlen >= 8) {
            memset(buffer, 0, 8);
            return 8;
        }
        return 0;
    }
#endif
    (void) itf;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    // DSE profiles: while the unlock + prefetch is still in progress, return 0
    // (NAK) for profile reads so the PS app retries rather than caching an
    // empty snapshot. Still kick off the background BT fetch.
    if (dse_is_profile_report(report_id) && !dse_profiles_ready()) {
        get_feature_data(report_id, reqlen);
        return 0;
    }

    std::vector<uint8_t> feature_data = get_feature_data(report_id, reqlen);
    if (!feature_data.empty()) {
        // 0x81 (portal command replies) and 0x82 (slot-command replies, split off
        // to dodge the portal's 0x81 diagnostic poll) both carry a full 0x66-framed
        // reply that must be returned VERBATIM. Every other report id is a native
        // report whose stored leading byte is the report id and gets stripped.
        // CLAMP to reqlen in every path: TinyUSB sizes the transfer buffer from
        // the DESCRIPTOR-declared report size. Copying more than reqlen is a
        // buffer overflow in the USB stack (this is exactly how routing 63-byte
        // slot replies through 0x82 - declared as a 9-byte report - corrupted
        // reads and threw errors in WebHID). Slot replies live on 0x84, whose
        // declared size is the full 63 bytes.
        if ((report_id == 0x81 || report_id == 0x84) && feature_data[0] == 0x66) {
            const uint16_t n = (uint16_t)((feature_data.size() < reqlen) ? feature_data.size() : reqlen);
            memcpy(buffer, feature_data.data(), n);
            return n;
        }
        const uint16_t n = (uint16_t)(((feature_data.size() - 1) < reqlen) ? (feature_data.size() - 1) : reqlen);
        memcpy(buffer, feature_data.data() + 1, n);
        return n;
    }

    return 0;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void) rhport;
    uint8_t const itf = tu_u16_low(p_request->wIndex); // wInterface
    uint8_t const alt = tu_u16_low(p_request->wValue); // bAlternateSetting

    if (itf == 1) {
        printf("[AUDIO] Set interface Speaker to alternate setting %d\n", alt);
        spk_active = alt;
    }
    if (itf == 2) { // ITF_NUM_AUDIO_STREAMING_IN (microphone)
        printf("[AUDIO] Set interface Microphone to alternate setting %d\n", alt);
        set_mic_active(alt);
    }

    return true;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
#ifdef ENABLE_WAKE_HID
    if (itf == 1) {
        // Drop keyboard SET_REPORT (host LED state).
        return;
    }
#endif
    (void) itf;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;

    // INTERRUPT OUT
    if (report_id == 0) {
        switch (buffer[0]) {
            case 0x02: {
                bool changed = state_update(buffer + 1, bufsize - 1);
                if (spk_active && !changed) {
                    break;
                }
                uint8_t outputData[78]{};
                outputData[0] = 0x31;
                outputData[1] = reportSeqCounter << 4;
                if (++reportSeqCounter == 256) {
                    reportSeqCounter = 0;
                }
                outputData[2] = 0x10;
                // memcpy(outputData + 3, buffer + 1, bufsize - 1);
                state_set(outputData + 3, sizeof(SetStateData));
                bt_write(outputData, sizeof(outputData));
                break;
            }
        }
    }
    if (report_id == 0x80 && bufsize >= 2 && buffer[0] == 0x66) {
#if ENABLE_VERBOSE
        printf("[HID] Receive 0x66 setting config, funcid:0x%02X\n", buffer[1]);
#endif

        // 0x80 0x66 cmd_id payload...
        pico_cmd_set(buffer[1], buffer + 2, bufsize - 2);
        return;
    }
    if (report_id == 0x80 ||
        // DSE: Write Profile Block
        report_id == 0x60 ||
        report_id == 0x62 ||
        report_id == 0x61) {
        set_feature_data(report_id, const_cast<uint8_t *>(buffer), bufsize);
    }
}

int main() {
#if SYS_CLOCK_KHZ != 150000
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(1000);
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
#endif

    board_init();
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);
#if !ENABLE_SERIAL
    sleep_ms(150);
    tud_disconnect();
#endif
    board_init_after_tusb();
#if ENABLE_SERIAL
    stdio_usb_init();
#endif

    if (cyw43_arch_init()) {
        printf("Failed to initialize CYW43\n");
        return 1;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);

    // SMPS coil-whine fix: at light load the on-board buck regulator drops into PFM
    // (power-save) mode, and its pulse-skipping repetition rate falls into the
    // audible band -> the board whines at idle. Driving the CYW43 SMPS power-save
    // control pin (WL_GPIO1 on the Pico 2 W / Pico W) HIGH forces continuous PWM,
    // which has lower 3V3 ripple at light load and silences the whine. No-op on
    // boards without the pin. (From awalol PR #207, independent of Wake-on-LAN.)
#ifndef CYW43_WL_GPIO_SMPS_PIN
#define CYW43_WL_GPIO_SMPS_PIN 1   // WL_GPIO1 on Pico W / Pico 2 W
#endif
    cyw43_arch_gpio_put(CYW43_WL_GPIO_SMPS_PIN, true);

#if ENABLE_BATT_LED
    battery_led_init();
#endif

#if !ENABLE_SERIAL
    if (watchdog_caused_reboot()) {
        printf("Rebooted by Watchdog!\n");
        // 当崩溃重启以后，闪三下灯
        for (int i = 0; i < 6; i++) {
            if (i % 2 == 0) {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
            } else {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
            }
            sleep_ms(500);
        }
    } else {
        printf("Clean boot\n");
    }
#endif

    // Initialize the critical section for the report buffer
    critical_section_init(&report_cs);
    wake_init();

    config_load();

    bt_init();
    bt_register_data_callback(on_bt_data);

    audio_init();
    state_init();

#if !ENABLE_SERIAL
    watchdog_enable(1000, true);
#endif

    while (1) {
#if !ENABLE_SERIAL
        watchdog_update();
#endif
        // Synth tick: with the host quiet, gated adaptive triggers must still
        // engage/release from live trigger movement, and releases must actually
        // reach the controller (fixes triggers stuck in resistance after rapid
        // R2/L2 play in games that only send reports when rumble changes).
        // Host just went to sleep: actively release the triggers ONCE before
        // standing down, so nothing stays latched on the controller through the
        // sleep and across the deferred power-off the wake path relies on.
        {
            static bool was_suspended = false;
            const bool susp = wake_host_is_suspended();
            if (susp && !was_suspended && state_release_for_suspend()) {
                uint8_t outputData[78]{};
                outputData[0] = 0x31;
                outputData[1] = reportSeqCounter << 4;
                if (++reportSeqCounter == 256) reportSeqCounter = 0;
                outputData[2] = 0x10;
                state_set(outputData + 3, sizeof(SetStateData));
                bt_write(outputData, sizeof(outputData));
            }
            was_suspended = susp;
        }
        {
            static uint32_t last_synth_tick_ms = 0;
            const uint32_t now = to_ms_since_boot(get_absolute_time());
            // 8 ms, not 50: a custom-effect stage sequence latches on trigger
            // POSITION, and a pull takes ~100-200 ms, so a 50 ms cadence gave only
            // 2-4 samples per pull and routinely skipped a stage's arming window
            // ("sometimes I get the wall, sometimes I don't"). The call is cheap -
            // it early-returns unless the host has gone quiet, and only pushes a
            // report when the composed state actually changes.
            // While the host is SUSPENDED there is nothing to synthesize for, and
            // the extra BT output traffic competes with the input reports that
            // wake-on-PS has to observe - so stand down completely until resume.
            // (Raising this cadence from 50 ms without that guard is what made
            // wake less reliable than 1.13.3.)
            // Interval is re-evaluated EVERY pass (cheap), so trigger movement
            // restores the fast cadence immediately; only the tick is rate-limited.
            if (!wake_host_is_suspended() &&
                now - last_synth_tick_ms >= state_synth_interval_ms()) {
                last_synth_tick_ms = now;
                if (state_synth_tick()) {
                    uint8_t outputData[78]{};
                    outputData[0] = 0x31;
                    outputData[1] = reportSeqCounter << 4;
                    if (++reportSeqCounter == 256) reportSeqCounter = 0;
                    outputData[2] = 0x10;
                    state_set(outputData + 3, sizeof(SetStateData));
                    bt_write(outputData, sizeof(outputData));
                }
            }
        }
        cyw43_arch_poll();
        tud_task();
        wake_task();
        audio_loop();
        interrupt_loop();
#if ENABLE_BATT_LED
        battery_led_tick();
#endif
        button_check();
        bt_inquiring_led();
        dse_task();
    }
}

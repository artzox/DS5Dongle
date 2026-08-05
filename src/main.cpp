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

// --- Gyro -> right-stick aiming with Mahony AHRS fusion --------------------
// Adds the controller's angular velocity onto the right stick in the input
// report the PC sees, so ANY game gets gyro aiming with zero PC software.
// Integer-only so it is safe inside the report critical section.
// Report offsets (from utils.h DualSense report layout):
//   RightStickX=2, RightStickY=3, TriggerLeft=4,
//   Gyro pitch (AngularVelocityX)=15, roll (AngularVelocityZ)=17, yaw (AngularVelocityY)=19
//   AccelerometerX=21, AccelerometerY=23, AccelerometerZ=25 (int16 LE)
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
static inline Quaternion quat_normalize(const Quaternion &q) {
    const float n = sqrtf(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    if (n < 1e-9f) return quat_identity();
    const float inv = 1.0f / n;
    return {q.w * inv, q.x * inv, q.y * inv, q.z * inv};
}
static inline Vector3 quat_rotate(const Quaternion &q, const Vector3 &v) {
    // q * (0, v) * q^-1 : rotate vector v by quaternion q
    const Quaternion qv{0.0f, (float)v.x, (float)v.y, (float)v.z};
    const Quaternion tmp = quat_mul(q, qv);
    const Quaternion res = quat_mul(tmp, quat_inverse(q));
    return {(int32_t)res.x, (int32_t)res.y, (int32_t)res.z};
}

// --- World Space: Mahony AHRS Filter for drift-resistant orientation -------
// The Mahony filter fuses gyro and accelerometer to maintain a stable quaternion
// representing the controller's orientation in the world frame.
// 
// Coordinate system:
//   World frame (gravity reference, independent of controller rotation):
//     X = left/right   (aiming left/right)
//     Y = up/down      (aiming up/down,  +Y points AWAY from gravity)
//     Z = forward/back
//   Controller sensor frame:
//     Local gyro and accelerometer readings
//
// The filter works by:
// 1. Integrating gyro to get rotation (fast, but drifts over time)
// 2. Correcting roll/pitch with the accelerometer gravity vector (slow, stable
//    reference that never drifts). Yaw around gravity is unobservable without a
//    magnetometer, so it is anchored by the world-space calibration reference
//    and can slowly wander; pressing "Calibrate World Space" re-anchors it.
// 3. Using proportional + integral error correction to dampen drift
//
// The controller can be held at ANY angle (flat, vertical, even upside down);
// the fused quaternion always tells us which way the world's up/down/left/right
// are relative to the sensor, so the aiming axes stay fixed to the game world.

// Mahony filter coefficients (tuned for RP2040 + DualSense)
// Kp: proportional gain (larger = faster convergence, more noise)
// Ki: integral gain (corrects gyro bias drift)
static constexpr float MAHONY_KP = 0.5f;  // proportional
static constexpr float MAHONY_KI = 0.02f; // integral

// Scale from raw gyro counts to radians/second (empirical for the DualSense
// IMU at the ~250Hz report rate; kept identical to the simple integrator so
// all space modes share one consistent scale).
static constexpr float GYRO_TO_RAD_PER_SEC = 0.0001f;

// Runtime orientation (Mahony-fused world orientation for World Space mode)
Quaternion g_world_orientation = quat_identity();
Quaternion g_world_reference = quat_identity();  // calibration reference
bool g_world_ref_set = false;

// Gyro integration state (for modes that need it)
Quaternion g_orientation = quat_identity();
Quaternion g_player_reference = quat_identity();
bool g_player_ref_set = false;
bool g_prev_gyro_allowed = false;

// Mahony AHRS error accumulator (integral term)
static float mahony_error_x = 0.0f;
static float mahony_error_y = 0.0f;
static float mahony_error_z = 0.0f;

// Helper to set the player reference to the current orientation (calibration)
// Exposed so UI/commands can reset the player reference explicitly.
void set_player_reference_now() {
    g_player_reference = g_orientation;
    g_player_ref_set = true;
}

// Helper to set the world reference to the current world orientation (calibration)
// Exposed so UI/commands (command 0x67) can reset the world-space heading.
void set_world_reference_now() {
    g_world_reference = g_world_orientation;
    g_world_ref_set = true;
}

// Mahony AHRS filter update
// Updates g_world_orientation based on gyro and accelerometer.
//
// World reference used here is the world "up" vector (+Y, opposite gravity).
// At rest the accelerometer measures the specific force which points UP (away
// from gravity), so the normalized accel IS the measured up direction in the
// sensor frame. We compare it against the estimated up direction
// (inverse(q) * world_up * q) and drive the quaternion with the cross product
// of the two.
static inline void mahony_update(const Vector3 &gyro_raw, const Vector3 &accel_raw, float dt) {
    // Normalize accelerometer (measured up direction, ~1.0 at rest)
    const float accel_mag = sqrtf((float)accel_raw.x * accel_raw.x +
                                  (float)accel_raw.y * accel_raw.y +
                                  (float)accel_raw.z * accel_raw.z);
    if (accel_mag < 1e-6f) return; // no valid gravity reference

    const float ax = accel_raw.x / accel_mag;  // measured up, sensor frame
    const float ay = accel_raw.y / accel_mag;
    const float az = accel_raw.z / accel_mag;

    // Current quaternion
    const Quaternion q = g_world_orientation;

    // Estimated world-up direction mapped into the sensor frame:
    //   est_sensor_up = inverse(q) * world_up * q,  world_up = (0, +1, 0)
    const Quaternion q_inv = quat_inverse(q);
    const Quaternion world_up = {0.0f, 0.0f, 1.0f, 0.0f};
    const Quaternion est_sensor_up = quat_mul(quat_mul(q_inv, world_up), q);

    // Error vector: cross product of measured and estimated up directions.
    //   error = measured x estimated  (in sensor frame)
    const float ex = ay * est_sensor_up.z - az * est_sensor_up.y;
    const float ey = az * est_sensor_up.x - ax * est_sensor_up.z;
    const float ez = ax * est_sensor_up.y - ay * est_sensor_up.x;

    // Integral error accumulation (drift correction). Clamp to avoid windup
    // during sustained rotations (e.g. a long continuous turn).
    mahony_error_x += ex * dt;
    mahony_error_y += ey * dt;
    mahony_error_z += ez * dt;
    constexpr float MAHONY_INTEGRAL_LIMIT = 1.0f;
    if (mahony_error_x >  MAHONY_INTEGRAL_LIMIT) mahony_error_x =  MAHONY_INTEGRAL_LIMIT;
    if (mahony_error_x < -MAHONY_INTEGRAL_LIMIT) mahony_error_x = -MAHONY_INTEGRAL_LIMIT;
    if (mahony_error_y >  MAHONY_INTEGRAL_LIMIT) mahony_error_y =  MAHONY_INTEGRAL_LIMIT;
    if (mahony_error_y < -MAHONY_INTEGRAL_LIMIT) mahony_error_y = -MAHONY_INTEGRAL_LIMIT;
    if (mahony_error_z >  MAHONY_INTEGRAL_LIMIT) mahony_error_z =  MAHONY_INTEGRAL_LIMIT;
    if (mahony_error_z < -MAHONY_INTEGRAL_LIMIT) mahony_error_z = -MAHONY_INTEGRAL_LIMIT;

    // Corrected gyro = raw gyro (rad/s) + proportional + integral terms
    float gx = gyro_raw.x * GYRO_TO_RAD_PER_SEC + MAHONY_KI * mahony_error_x;
    float gy = gyro_raw.y * GYRO_TO_RAD_PER_SEC + MAHONY_KI * mahony_error_y;
    float gz = gyro_raw.z * GYRO_TO_RAD_PER_SEC + MAHONY_KI * mahony_error_z;

    gx += MAHONY_KP * ex;
    gy += MAHONY_KP * ey;
    gz += MAHONY_KP * ez;

    // Integrate angular velocity into quaternion (exponential map).
    const float angle = sqrtf(gx*gx + gy*gy + gz*gz);
    if (angle < 1e-9f) return;

    const float s = sinf(angle * 0.5f * dt) / angle;
    const Quaternion dq{cosf(angle * 0.5f * dt), gx * s, gy * s, gz * s};

    // RIGHT-multiply: the correction dq is expressed in the sensor (body) frame,
    // so q = q * dq. (Left-multiplying applies it in the world frame and makes
    // the filter rotate the wrong way, so gravity correction cannot converge.)
    g_world_orientation = quat_mul(g_world_orientation, dq);

    // Normalize to prevent numerical drift
    g_world_orientation = quat_normalize(g_world_orientation);
}

static inline void integrate_orientation_from_gyro(const Vector3 &gyro, float dt) {
    // Simple gyro integration (for non-World modes like Local and Player)
    // The raw gyro units are device-specific; use a small scale.
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

// Separate gyro-space transform helpers to keep each mode self-contained

// Mode 0: TRADITIONAL
// Description: Original gyro_axis behavior. No transformation. No quaternion fusion.
// This preserves the exact legacy behavior for backward compatibility.
// Motion is based on raw gyro values and the gyro_axis setting.
static Vector3 apply_gyro_traditional(const Vector3 &gyro) {
    // Preserve exact original behaviour: raw gyro passthrough
    return gyro;
}

// Mode 1: YAW_ROLL
// Description: Horizontal aiming uses controller yaw (turn), vertical uses controller roll (tilt sideways).
// Useful for players who naturally tilt the controller left/right for aim refinement.
// Pitch (forward/back) is ignored.
// Coordinate mapping: {roll, yaw, 0} -> {pitch_out, horiz_out, unused}
static Vector3 apply_gyro_yaw_roll(const Vector3 &gyro) {
    // gyro layout: x = pitch(15), y = yaw(17), z = roll(19)
    return {gyro.z, gyro.y, 0};
}

// Mode 2: LOCAL_SPACE
// Description: Angular velocity expressed in controller-local axes.
// Motion follows the physical controller orientation regardless of where it points.
// A tilt "forward" (in sensor frame) always aims in the same direction relative to the controller.
// Useful for: feeling controller-centric, like moving a virtual head mounted on the device.
// Coordinate system: Sensor-local frame (controller X/Y/Z axes)
static Vector3 apply_gyro_local_space(const Vector3 &gyro, const Quaternion &orientation) {
    // Rotate gyro from world frame into controller-local frame using inverse orientation.
    // local_gyro = inverse(orientation) * gyro
    Quaternion inv = quat_inverse(orientation);
    return quat_rotate(inv, gyro);
}

// Mode 3: PLAYER_SPACE
// Description: Angular velocity relative to a player reference direction (captured at calibration).
// Player holds the controller in a comfortable neutral position before aiming.
// All motion is relative to that stored direction.
// Useful for: body-relative aiming without world reference (like holding a rifle on your shoulder).
// Coordinate system: Player body frame (relative to start orientation)
static Vector3 apply_gyro_player_space(const Vector3 &gyro, const Quaternion &orientation) {
    // Compute rotation relative to the stored player reference quaternion.
    // rel_orientation = current * inverse(reference)
    // Then extract the relative rotation as a vector.
    if (!g_player_ref_set) return {0,0,0};
    const Quaternion rel = quat_mul(orientation, quat_inverse(g_player_reference));
    const float vx = rel.x, vy = rel.y, vz = rel.z;
    const float vm = sqrtf(vx*vx + vy*vy + vz*vz);
    if (vm < 1e-8f) return {0,0,0};
    const float angle = 2.0f * asinf(fminf(1.0f, vm));
    const float ax = vx / vm, ay = vy / vm, az = vz / vm;
    const float scale = 5000.0f; // converts radians -> raw-ish units
    const int32_t horiz = (int32_t)(ay * angle * scale);
    const int32_t pitch = (int32_t)(ax * angle * scale);
    return {pitch, horiz, 0};
}

// Mode 4: WORLD_SPACE
// Description: Earth/player-relative aiming. The aiming axes are anchored to a FIXED
// calibration reference captured when gyro aiming starts or when the player presses
// "Calibrate World Space" (command 0x67). After that, no matter how the controller is
// physically rotated (flat, vertical, even upside down), the output axes stay locked
// to the game's axes: up movement aims up, left aims left, right aims right, down aims
// down. Useful for: absolute aiming where camera up is always screen-up.
//
// Coordinate systems:
//   Local space:  controller sensor frame -- axes glued to the controller. A raw gyro
//                 reading (raw_vec) is ALREADY expressed in this frame.
//   World space:  fixed player frame independent of controller rotation:
//                 X = player left/right, Y = player up/down, Z = forward/back.
//
// Why a FROZEN reference instead of the live fused orientation:
//   Rate-based aiming maps the sensor-frame angular velocity into the game axes with
//     gyro_world = q * gyro_local * inverse(q)
//   If q is the LIVE Mahony orientation, physically rotating the controller rotates
//   the mapping with it: "tilt forward" stops meaning "aim up" and starts meaning
//   "aim left/right" once the controller is turned 90 degrees. The spec's test case B
//   (rotate controller 90 deg, tilt forward, must STILL aim up) requires the frame to
//   NOT follow the controller, so we freeze q at the calibration pose.
//
// Transformation formula (rate-based, uses the frozen reference):
//   gyro_world = g_world_reference * gyro_local * inverse(g_world_reference)
//
// Drift: the output path is a fixed rotation of the raw gyro -- it contains no
// integration, so it cannot drift. The Mahony filter (accelerometer gravity
// correction + quaternion normalization + integral error term) is only used to
// obtain an accurate g_world_reference at calibration time.
static Vector3 apply_gyro_world_space(const Vector3 &gyro) {
    // Anchor: the calibration reference if captured, otherwise identity (raw gyro
    // passthrough). The player should press "Calibrate World Space" (0x67) or
    // re-activate aiming to re-anchor to the current hold pose.
    const Quaternion anchor = g_world_ref_set ? g_world_reference : quat_identity();
    // gyro_world = anchor * gyro_local * inverse(anchor)
    return quat_rotate(anchor, gyro);
}

// Dispatcher kept for compatibility with earlier callsites
static Vector3 apply_gyro_space(Vector3 gyro, const Quaternion &orientation, uint8_t mode) {
    switch (mode) {
        case GYRO_SPACE_TRADITIONAL: return apply_gyro_traditional(gyro);
        case GYRO_SPACE_YAW_ROLL:    return apply_gyro_yaw_roll(gyro);
        case GYRO_SPACE_LOCAL:       return apply_gyro_local_space(gyro, orientation);
        case GYRO_SPACE_PLAYER:      return apply_gyro_player_space(gyro, orientation);
        case GYRO_SPACE_WORLD:       return apply_gyro_world_space(gyro);
        default: return gyro;
    }
}

static inline void __not_in_flash_func(apply_gyro_stick)(uint8_t *d) {
    const auto &cfg = get_config();
    if (cfg.gyro_mode == 0) return;

    auto rd16 = [&](int off) -> int32_t {
        return (int16_t)((uint16_t)d[off] | ((uint16_t)d[off + 1] << 8));
    };

    // Raw sensor reads (preserve original mapping):
    // Gyro: pitch (AngularVelocityX) @ 15, yaw (AngularVelocityY) @ 19,
    //       roll (AngularVelocityZ) @ 17
    // Accelerometer (authoritative layout in utils.h USBGetStateData):
    //       AccX @ 21, AccY @ 23, AccZ @ 25  (int16 LE)
    // NOTE: the accelerometer MUST be read from 21/23/25. Reading it from 9/11/13
    // (button/counter bytes) fed garbage into the Mahony filter and broke World Space.
    const int32_t raw_pitch = rd16(15);
    const int32_t raw_yaw = rd16(17);
    const int32_t raw_roll = rd16(19);
    const int32_t acc_x = rd16(21);   // AccX
    const int32_t acc_y = rd16(23);   // AccY
    const int32_t acc_z = rd16(25);   // AccZ

    // Time step based on polling rate (approx): 250Hz=0.004s, 500Hz=0.002s, realtime=0.001s
    float dt = 0.004f;
    if (cfg.polling_rate_mode == 1) dt = 0.002f;
    if (cfg.polling_rate_mode == 2) dt = 0.001f;

    Vector3 raw_vec{ raw_pitch, raw_yaw, raw_roll };
    // accel_vec is assembled as { AccX, AccZ, AccY } so that its Y component reads
    // +1g when the controller lies flat face-up (the DualSense AccZ axis points out
    // of the face). This matches world_up = (0, +1, 0) used by the Mahony filter,
    // so the filter converges to identity at rest in the neutral pose.
    Vector3 accel_vec{ acc_x, acc_z, acc_y };

    // World Space: run the Mahony AHRS filter on EVERY report (even while the
    // activation gate is closed) so g_world_orientation is always gravity-converged
    // and the reference captured when aiming starts is accurate.
    if (cfg.gyro_space_mode == GYRO_SPACE_WORLD) {
        mahony_update(raw_vec, accel_vec, dt);
    }

    // Activation schemes (industry set: ADS-gated, always-on, touch-enable, ratchet):
    //   1 = only while L2 (aim) held past ~12%
    //   2 = always on
    //   3 = only while the touchpad is touched (Steam 'touch to enable' style)
    //   4 = always on, touching the touchpad PAUSES gyro (ratchet: re-center like
    //       lifting a mouse)
    //   5 = R2 held, 6 = L1 held, 7 = R1 held
    const bool touch = !(d[32] & 0x80);            // touchpad finger 1 down
    const bool gate_ok = !((cfg.gyro_mode == 1 && d[4] < 30) ||   // L2 held (aim)
                           (cfg.gyro_mode == 3 && !touch) ||
                           (cfg.gyro_mode == 4 && touch) ||
                           (cfg.gyro_mode == 5 && d[5] < 30) ||   // R2 held
                           (cfg.gyro_mode == 6 && !(d[8] & 0x01)) || // L1 held
                           (cfg.gyro_mode == 7 && !(d[8] & 0x02))); // R1 held
    if (!gate_ok) {
        // Gate closed: mark the previous frame as not-allowed so the reference
        // quaternions are re-captured every time aiming (re)starts.
        g_prev_gyro_allowed = false;
        return;
    }
    const bool gyro_allowed = true;

    // Local, Player: simple gyro integration (only while aiming is active, so the
    // accumulated orientation stays relative to activation start).
    if (cfg.gyro_space_mode != GYRO_SPACE_TRADITIONAL && cfg.gyro_space_mode != GYRO_SPACE_WORLD) {
        integrate_orientation_from_gyro(raw_vec, dt);
    }

    // If PLAYER_SPACE and activation just started, capture reference
    if (cfg.gyro_space_mode == GYRO_SPACE_PLAYER && !g_prev_gyro_allowed) {
        g_player_reference = g_orientation;
        g_player_ref_set = true;
    }

    // If WORLD_SPACE and activation just started, capture world reference
    if (cfg.gyro_space_mode == GYRO_SPACE_WORLD && !g_prev_gyro_allowed) {
        g_world_reference = g_world_orientation;
        g_world_ref_set = true;
    }
    g_prev_gyro_allowed = gyro_allowed;

    // Apply gyro space transform
    const Vector3 transformed = apply_gyro_space(raw_vec, g_orientation, cfg.gyro_space_mode);

    // Apply small deadzone as before but on transformed values
    int32_t pitch = transformed.x;
    int32_t horiz;
    if (cfg.gyro_space_mode == GYRO_SPACE_TRADITIONAL) {
        // Preserve legacy axis selection only for Traditional mode
        horiz = (cfg.gyro_axis == 1) ? transformed.z : transformed.y;
    } else {
        // For space modes, ignore old gyro_axis mapping and use the 'yaw-like'
        // horizontal component from the transform consistently.
        horiz = transformed.y;
    }

    if (horiz > -12 && horiz < 12) horiz = 0;
    if (pitch > -12 && pitch < 12) pitch = 0;
    if (horiz == 0 && pitch == 0) return;

    // Update diagnostic horizontal magnitude from the chosen axis
    { extern volatile uint16_t g_diag_gyro; int32_t ah = horiz < 0 ? -horiz : horiz; g_diag_gyro = (ah > 65535) ? 65535 : (uint16_t)ah; }

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

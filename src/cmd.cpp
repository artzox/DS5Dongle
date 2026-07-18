//
// Created by awalol on 2026/5/4.
//

#include "cmd.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "bt.h"
#include "config.h"
#include "wake.h"
#include "device/usbd.h"
#include "pico/time.h"
#include "pico/bootrom.h"
#include "audio.h"

// spk_active (main.cpp) + audio_mic_active() (audio.cpp) are surfaced in the
// 0xf9 command response so the config UI can display the real gated mic/speaker
// state, reflecting the disable_mic / disable_speaker settings.
extern bool spk_active;
extern std::unordered_map<uint8_t, std::vector<uint8_t> > feature_data;

template<typename T>
static bool read_config_value(T &value, uint8_t const *buffer, uint16_t bufsize) {
    if (bufsize < sizeof(T)) {
        return false;
    }
    memcpy(&value, buffer, sizeof(T));
    return true;
}

// Firmware version, reported via read-only fields 0x7D/0x7E/0x7F so the portal
// can display which build is flashed. Bump on every released build.
constexpr uint8_t FW_VER_MAJOR = 1;
constexpr uint8_t FW_VER_MINOR = 12;
constexpr uint8_t FW_VER_PATCH = 0;

template<typename T>
static bool write_config_value(uint8_t *buffer, uint16_t bufsize, T value) {
    if (bufsize < sizeof(T)) {
        return false;
    }
    memcpy(buffer, &value, sizeof(T));
    return true;
}

static bool set_field_in(Config_body &new_config, uint8_t field_id, uint8_t const *buffer, uint16_t bufsize) {

    switch (field_id) {
        case 0x01: {
            float value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.haptics_gain = value;
            break;
        }
        case 0x02: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.speaker_volume = value;
            break;
        }
        case 0x03: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.headset_volume = value;
            break;
        }
        case 0x04: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.sync_spk_headset_volume = value;
            break;
        }
        case 0x05: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.speaker_gain = value;
            break;
        }
        case 0x06: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.inactive_time = value;
            break;
        }
        case 0x07: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.disable_inactive_disconnect = value;
            break;
        }
        case 0x08: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.disable_pico_led = value;
            break;
        }
        case 0x09: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.polling_rate_mode = value;
            break;
        }
        case 0x0a: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.audio_buffer_length = value;
            break;
        }
        case 0x0b: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.controller_mode = value;
            break;
        }
        case 0x0c: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.lock_volume = value;
            break;
        }
        case 0x0d: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.disable_usb_sn = value;
            break;
        }
        case 0x0e: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.ps_shortcut_enabled = value;
            break;
        }
        case 0x0f: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.disable_mic = value;
            break;
        }
        case 0x10: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.disable_speaker = value;
            break;
        }
        case 0x11: {
            uint8_t value{};
            if (!read_config_value(value, buffer, bufsize)) return false;
            new_config.enable_wake = value;
            break;
        }
        case 0x12: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.auto_haptics_enable=v; break; }
        case 0x13: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.auto_haptics_gain=v; break; }
        case 0x14: { uint16_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.auto_haptics_lowpass_hz=v; break; }
        case 0x15: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.auto_mute_replace=v; break; }
        case 0x16: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.auto_mute_mix=v; break; }
        case 0x17: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.auto_haptics_gate=v; break; }
        case 0x18: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.auto_haptics_slope=v; break; }
        case 0x19: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.lightbar_off=v; break; }
        case 0x1a: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.auto_haptics_smooth=v; break; }
        case 0x1b: { uint16_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.bt_flush_timeout=v; break; }
        case 0x1c: { uint16_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.bt_qos_latency_us=v; break; }
        case 0x1d: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.rumble_haptic_strength=v; break; }
        case 0x1e: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.effect_leak_volume=v; break; }
        case 0x1f: { uint16_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.effect_leak_hp_hz=v; break; }
        case 0x23: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.effect_leak_sensitivity=v; break; }
        case 0x24: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.effect_leak_decay=v; break; }
        case 0x25: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.effect_leak_attack=v; break; }
        case 0x26: { uint16_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.effect_leak_output_hp_hz=v; break; }
        case 0x27: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.r2t_mode=v; break; }
        case 0x28: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.r2t_on_press=v; break; }
        case 0x29: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.r2t_strength=v; break; }
        case 0x2a: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.r2t_frequency=v; break; }
        case 0x2b: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_mode=v; break; }
        case 0x2c: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_strength=v; break; }
        case 0x2d: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_threshold=v; break; }
        case 0x2e: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_start_pos=v; break; }
        case 0x2f: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.gyro_mode=v; break; }
        case 0x30: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.gyro_sens=v; break; }
        case 0x31: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.gyro_axis=v; break; }
        case 0x32: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.gyro_invert=v; break; }
        case 0x33: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.haptics_aa=v; break; }
        case 0x34: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.synth_force=v; break; }
        case 0x39: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_pushback=v; break; }
        case 0x3a: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_pushback_src=v; break; }
        case 0x3b: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_pushback_freq=v; break; }
        case 0x40: { uint16_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.effect_leak_lp_hz=v; break; }
        case 0x41: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.effect_leak_hold=v; break; }
        case 0x42: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_l2_mode=v; break; }
        case 0x43: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_l2_strength=v; break; }
        case 0x45: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_l2_threshold=v; break; }
        case 0x46: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_l2_start_pos=v; break; }
        case 0x47: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_l2_pushback=v; break; }
        case 0x48: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_l2_pushback_freq=v; break; }
        case 0x49: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_l2_kick_style=v; break; }
        case 0x4a: { uint16_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.ah_xover_hz=v; break; }
        case 0x4b: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.ah_low_gain=v; break; }
        case 0x4c: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.ah_high_gain=v; break; }
        case 0x4d: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_shape=v; break; }
        case 0x4e: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_strength_b=v; break; }
        case 0x4f: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_detent_pos=v; break; }
        case 0x50: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_l2_shape=v; break; }
        case 0x51: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_l2_strength_b=v; break; }
        case 0x52: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_l2_detent_pos=v; break; }
        case 0x53: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_deadzone=v; break; }
        case 0x54: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_l2_deadzone=v; break; }
        case 0x55: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.mix_native_level=v; break; }
        case 0x56: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.effect_leak_max_burst=v; break; }
        case 0x44: { uint8_t v{}; if(!read_config_value(v,buffer,bufsize))return false; new_config.at_kick_style=v; break; }
        default:
            printf("[CMD] Unknown config field id: 0x%02X\n", field_id);
            return false;
    }

    return true;
}

static bool set_config_field(uint8_t field_id, uint8_t const *buffer, uint16_t bufsize) {
    Config_body new_config = get_config();
    if (!set_field_in(new_config, field_id, buffer, bufsize)) return false;
    set_config(reinterpret_cast<const uint8_t *>(&new_config), sizeof(new_config));
    return true;
}

static bool get_config_field(uint8_t field_id, uint8_t *buffer, uint16_t bufsize) {
    const Config_body &config = get_config();

    switch (field_id) {
        case 0x00:
            return write_config_value(buffer, bufsize, config.config_version);
        case 0x01:
            return write_config_value(buffer, bufsize, config.haptics_gain);
        case 0x02:
            return write_config_value(buffer, bufsize, config.speaker_volume);
        case 0x03:
            return write_config_value(buffer, bufsize, config.headset_volume);
        case 0x04:
            return write_config_value(buffer, bufsize, config.sync_spk_headset_volume);
        case 0x05:
            return write_config_value(buffer, bufsize, config.speaker_gain);
        case 0x06:
            return write_config_value(buffer, bufsize, config.inactive_time);
        case 0x07:
            return write_config_value(buffer, bufsize, config.disable_inactive_disconnect);
        case 0x08:
            return write_config_value(buffer, bufsize, config.disable_pico_led);
        case 0x09:
            return write_config_value(buffer, bufsize, config.polling_rate_mode);
        case 0x0a:
            return write_config_value(buffer, bufsize, config.audio_buffer_length);
        case 0x0b:
            return write_config_value(buffer, bufsize, config.controller_mode);
        case 0x0c:
            return write_config_value(buffer, bufsize, config.lock_volume);
        case 0x0d:
            return write_config_value(buffer, bufsize, config.disable_usb_sn);
        case 0x0e:
            return write_config_value(buffer, bufsize, config.ps_shortcut_enabled);
        case 0x0f:
            return write_config_value(buffer, bufsize, config.disable_mic);
        case 0x10:
            return write_config_value(buffer, bufsize, config.disable_speaker);
        case 0x11:
            return write_config_value(buffer, bufsize, config.enable_wake);
        case 0x12: return write_config_value(buffer, bufsize, config.auto_haptics_enable);
        case 0x13: return write_config_value(buffer, bufsize, config.auto_haptics_gain);
        case 0x14: return write_config_value(buffer, bufsize, config.auto_haptics_lowpass_hz);
        case 0x15: return write_config_value(buffer, bufsize, config.auto_mute_replace);
        case 0x16: return write_config_value(buffer, bufsize, config.auto_mute_mix);
        case 0x17: return write_config_value(buffer, bufsize, config.auto_haptics_gate);
        case 0x18: return write_config_value(buffer, bufsize, config.auto_haptics_slope);
        case 0x19: return write_config_value(buffer, bufsize, config.lightbar_off);
        case 0x1a: return write_config_value(buffer, bufsize, config.auto_haptics_smooth);
        case 0x1b: return write_config_value(buffer, bufsize, config.bt_flush_timeout);
        case 0x1c: return write_config_value(buffer, bufsize, config.bt_qos_latency_us);
        case 0x1d: return write_config_value(buffer, bufsize, config.rumble_haptic_strength);
        case 0x1e: return write_config_value(buffer, bufsize, config.effect_leak_volume);
        case 0x1f: return write_config_value(buffer, bufsize, config.effect_leak_hp_hz);
        case 0x23: return write_config_value(buffer, bufsize, config.effect_leak_sensitivity);
        case 0x24: return write_config_value(buffer, bufsize, config.effect_leak_decay);
        case 0x25: return write_config_value(buffer, bufsize, config.effect_leak_attack);
        case 0x26: return write_config_value(buffer, bufsize, config.effect_leak_output_hp_hz);
        case 0x27: return write_config_value(buffer, bufsize, config.r2t_mode);
        case 0x28: return write_config_value(buffer, bufsize, config.r2t_on_press);
        case 0x29: return write_config_value(buffer, bufsize, config.r2t_strength);
        case 0x2a: return write_config_value(buffer, bufsize, config.r2t_frequency);
        case 0x2b: return write_config_value(buffer, bufsize, config.at_mode);
        case 0x2c: return write_config_value(buffer, bufsize, config.at_strength);
        case 0x2d: return write_config_value(buffer, bufsize, config.at_threshold);
        case 0x2e: return write_config_value(buffer, bufsize, config.at_start_pos);
        case 0x2f: return write_config_value(buffer, bufsize, config.gyro_mode);
        case 0x30: return write_config_value(buffer, bufsize, config.gyro_sens);
        case 0x31: return write_config_value(buffer, bufsize, config.gyro_axis);
        case 0x32: return write_config_value(buffer, bufsize, config.gyro_invert);
        case 0x33: return write_config_value(buffer, bufsize, config.haptics_aa);
        case 0x34: return write_config_value(buffer, bufsize, config.synth_force);
        case 0x39: return write_config_value(buffer, bufsize, config.at_pushback);
        case 0x3a: return write_config_value(buffer, bufsize, config.at_pushback_src);
        case 0x3b: return write_config_value(buffer, bufsize, config.at_pushback_freq);
        case 0x40: return write_config_value(buffer, bufsize, config.effect_leak_lp_hz);
        case 0x41: return write_config_value(buffer, bufsize, config.effect_leak_hold);
        case 0x42: return write_config_value(buffer, bufsize, config.at_l2_mode);
        case 0x43: return write_config_value(buffer, bufsize, config.at_l2_strength);
        case 0x45: return write_config_value(buffer, bufsize, config.at_l2_threshold);
        case 0x46: return write_config_value(buffer, bufsize, config.at_l2_start_pos);
        case 0x47: return write_config_value(buffer, bufsize, config.at_l2_pushback);
        case 0x48: return write_config_value(buffer, bufsize, config.at_l2_pushback_freq);
        case 0x49: return write_config_value(buffer, bufsize, config.at_l2_kick_style);
        case 0x4a: return write_config_value(buffer, bufsize, config.ah_xover_hz);
        case 0x4b: return write_config_value(buffer, bufsize, config.ah_low_gain);
        case 0x4c: return write_config_value(buffer, bufsize, config.ah_high_gain);
        case 0x4d: return write_config_value(buffer, bufsize, config.at_shape);
        case 0x4e: return write_config_value(buffer, bufsize, config.at_strength_b);
        case 0x4f: return write_config_value(buffer, bufsize, config.at_detent_pos);
        case 0x50: return write_config_value(buffer, bufsize, config.at_l2_shape);
        case 0x51: return write_config_value(buffer, bufsize, config.at_l2_strength_b);
        case 0x52: return write_config_value(buffer, bufsize, config.at_l2_detent_pos);
        case 0x53: return write_config_value(buffer, bufsize, config.at_deadzone);
        case 0x54: return write_config_value(buffer, bufsize, config.at_l2_deadzone);
        case 0x55: return write_config_value(buffer, bufsize, config.mix_native_level);
        case 0x56: return write_config_value(buffer, bufsize, config.effect_leak_max_burst);
        case 0x44: return write_config_value(buffer, bufsize, config.at_kick_style);
        case 0x3c: { extern volatile uint8_t g_diag_at_env; return write_config_value(buffer, bufsize, (uint8_t)g_diag_at_env); }
        case 0x35: { extern volatile uint16_t g_diag_gyro; return write_config_value(buffer, bufsize, (uint16_t)g_diag_gyro); }
        case 0x36: { extern volatile uint8_t g_diag_synth; return write_config_value(buffer, bufsize, (uint8_t)g_diag_synth); }
        case 0x37: { extern volatile uint16_t g_diag_ch01_peak; return write_config_value(buffer, bufsize, (uint16_t)g_diag_ch01_peak); }
        case 0x38: { extern volatile uint16_t g_diag_ch23_peak; return write_config_value(buffer, bufsize, (uint16_t)g_diag_ch23_peak); }
        // Read-only firmware version (no write handlers on purpose).
        case 0x7d: return write_config_value(buffer, bufsize, FW_VER_MAJOR);
        case 0x7e: return write_config_value(buffer, bufsize, FW_VER_MINOR);
        case 0x7f: return write_config_value(buffer, bufsize, FW_VER_PATCH);
        case 0x20: { extern volatile uint16_t g_diag_bytes_read; return write_config_value(buffer, bufsize, (uint16_t)g_diag_bytes_read); }
        case 0x21: { extern volatile uint8_t g_diag_actual_ch; return write_config_value(buffer, bufsize, (uint8_t)g_diag_actual_ch); }
        case 0x22: { int8_t rssi = 0; bt_get_signal_strength(&rssi); return write_config_value(buffer, bufsize, (uint8_t)rssi); }
        default:
            printf("[CMD] Unknown config field id: 0x%02X\n", field_id);
            return false;
    }
}

void pico_cmd_set(uint8_t cmd_id, uint8_t const *buffer, uint16_t bufsize) {
    // 0x01 update config field in variable: field_id + typed value
    // 0x02 write config to flash
    // 0x03 reconnect tinyusb device;
    // 0x04 query config field: field_id (0x00 = config_version)

    switch (cmd_id) {
        case 0x01: {
#if ENABLE_VERBOSE
            printf("[CMD] Enter config set func\n");
#endif
            bool success = false;
            if (bufsize < 1) {
                printf("[CMD] Config set missing field id\n");
            } else {
                const uint8_t field_id = buffer[0];
                success = set_config_field(field_id, buffer + 1, bufsize - 1);
                if (!success) {
                    printf("[CMD] Config set failed, field id: 0x%02X\n", field_id);
                }
            }
            uint8_t buf[63]{};
            buf[0] = 0x66;
            buf[1] = 0x01;
            buf[2] = success ? 0x00 : 0x01;
            feature_data[0x81].assign(buf, buf + sizeof(buf));
            break;
        }
        case 0x02: {
            printf("[CMD] Enter config save func\n");
            config_save();
            break;
        }
        case 0x03: {
            printf("[CMD] Enter tud reconnect func\n");
            wake_note_usb_reconnect(); // this disconnect is intentional, not a host sleep
            tud_disconnect();
            sleep_ms(150);
            tud_connect();
            break;
        }
        case 0x04: {
            printf("[CMD] get config field\n");
            uint8_t buf[63]{};
            buf[0] = 0x66;
            buf[1] = 0x04;
            if (bufsize < 1) {
                printf("[CMD] Config get missing field id\n");
                buf[2] = 0xff;
            } else {
                const uint8_t field_id = buffer[0];
                buf[2] = field_id;
                if (!get_config_field(field_id, buf + 3, sizeof(buf) - 3)) {
                    printf("[CMD] Config get failed, field id: 0x%02X\n", field_id);
                }
            }
            feature_data[0x81].assign(buf, buf + sizeof(buf));
            break;
        }
        case 0x05: {
            printf("[CMD] get firmware version\n");
            uint8_t buf[63]{};
            buf[0] = 0x66;
            buf[1] = 0x05;
            const auto len = std::min(strlen(PICO_PROGRAM_VERSION_STRING), sizeof(buf) - 2);
            memcpy(buf + 2, PICO_PROGRAM_VERSION_STRING, len);
            feature_data[0x81].assign(buf, buf + sizeof(buf));
            break;
        }
        case 0x06: {
            printf("[CMD] get signal strength\n");
            uint8_t buf[63]{};
            buf[0] = 0x66;
            buf[1] = 0x06;
            // [-128,0]
            int8_t rssi = 0;
            bt_get_signal_strength(&rssi);
            buf[2] = rssi;
            // byte 3: real audio gating state, for the config UI to display.
            //   bit7 = valid marker
            //   bit0 = controller mic actually streaming (host opened it AND !disable_mic)
            //   bit1 = controller speaker actually driven (host opened it AND !disable_speaker)
            uint8_t flags = 0x80;
            if (audio_mic_active() && !get_config().disable_mic) flags |= 0x01;
            if (spk_active && !get_config().disable_speaker) flags |= 0x02;
            buf[3] = flags;
            feature_data[0x81].assign(buf, buf + sizeof(buf));
            break;
        }
        case 0x07: {
            // Reboot into BOOTSEL (USB mass-storage bootloader) so the dongle can be
            // reflashed from the host without the physical BOOTSEL button. The
            // controller's enumeration is unchanged -- this is just a host command.
            // (awalol ships this commented out for security; enabled for personal use.)
            printf("[CMD] Reboot to BOOTSEL (USB bootloader)\n");
            sleep_ms(50);
            reset_usb_boot(0, 0); // noreturn
            break;
        }
        case 0x08: {
            // Save current config to profile slot. Payload: [slot_idx, name...(<=16)]
            // A slot save erases+programs a whole flash sector with interrupts
            // disabled, stalling USB for a moment. Post a PENDING reply (status
            // 0xFE) first so the host can never read a stale reply from an
            // earlier command as this one's result; the real reply overwrites
            // it when the work is done.
            printf("[CMD] save profile slot\n");
            { uint8_t pend[63]{}; pend[0]=0x66; pend[1]=0x08; pend[2]=0xFE;
              if (bufsize >= 1) pend[3] = buffer[0];
              feature_data[0x81].assign(pend, pend + sizeof(pend)); }
            uint8_t buf[63]{};
            buf[0] = 0x66; buf[1] = 0x08; buf[2] = 0x01; // default: fail
            if (bufsize >= 1 && buffer[0] < SLOT_COUNT) {
                const uint8_t idx = buffer[0];
                const uint8_t nlen = (bufsize > 1) ? (uint8_t)std::min<uint16_t>(bufsize - 1, SLOT_NAME_LEN) : 0;
                if (slot_save(idx, buffer + 1, nlen)) buf[2] = 0x00;
                buf[3] = idx;
            }
            feature_data[0x81].assign(buf, buf + sizeof(buf));
            break;
        }
        case 0x09: {
            // Activate profile slot. Payload: [slot_idx]
            // Reply: 0x66 0x09 status idx needs_reenum
            // needs_reenum=1 means the caller should send cmd 0x03 (USB reconnect)
            // for descriptor-level settings to take effect.
            printf("[CMD] activate profile slot\n");
            { uint8_t pend[63]{}; pend[0]=0x66; pend[1]=0x09; pend[2]=0xFE;
              if (bufsize >= 1) pend[3] = buffer[0];
              feature_data[0x81].assign(pend, pend + sizeof(pend)); }
            uint8_t buf[63]{};
            buf[0] = 0x66; buf[1] = 0x09; buf[2] = 0x01;
            if (bufsize >= 1 && buffer[0] < SLOT_COUNT) {
                bool reenum = false;
                if (slot_activate(buffer[0], reenum)) {
                    buf[2] = 0x00;
                    buf[4] = reenum ? 0x01 : 0x00;
                }
                buf[3] = buffer[0];
            }
            feature_data[0x81].assign(buf, buf + sizeof(buf));
            break;
        }
        case 0x0a: {
            // Read profile slot info. Payload: [slot_idx]
            // Reply: 0x66 0x0a status idx valid cfg_version name[16]
            printf("[CMD] read profile slot info\n");
            { uint8_t pend[63]{}; pend[0]=0x66; pend[1]=0x0a; pend[2]=0xFE;
              if (bufsize >= 1) pend[3] = buffer[0];
              feature_data[0x81].assign(pend, pend + sizeof(pend)); }
            uint8_t buf[63]{};
            buf[0] = 0x66; buf[1] = 0x0a; buf[2] = 0x01;
            if (bufsize >= 1 && buffer[0] < SLOT_COUNT) {
                uint8_t valid = 0, ver = 0;
                uint8_t name[SLOT_NAME_LEN]{};
                if (slot_info(buffer[0], name, valid, ver)) {
                    buf[2] = 0x00;
                    buf[4] = valid;
                    buf[5] = ver;
                    memcpy(buf + 6, name, SLOT_NAME_LEN);
                }
                buf[3] = buffer[0];
            }
            feature_data[0x81].assign(buf, buf + sizeof(buf));
            break;
        }

        case 0x0b: {
            // BULK set config fields. Payload: [n, then n x (fid, len, value_bytes)].
            // All entries land in ONE config copy followed by ONE live apply - a
            // full-profile apply drops from ~60 feature-report round-trips (each a
            // USB transaction + portal-side settle) to a handful of packed chunks.
            // Reply: 0x66 0x0b status applied_count (0x00 = every entry applied).
            uint8_t buf[63]{}; buf[0] = 0x66; buf[1] = 0x0b; buf[2] = 0x01;
            if (bufsize >= 1) {
                Config_body nc = get_config();
                const uint8_t n = buffer[0];
                uint8_t applied = 0; uint16_t off = 1; bool ok = true;
                for (uint8_t i2 = 0; i2 < n && ok; ++i2) {
                    if ((uint16_t)(off + 2) > bufsize) { ok = false; break; }
                    const uint8_t fid = buffer[off];
                    const uint8_t len = buffer[off + 1];
                    if (len == 0 || (uint16_t)(off + 2 + len) > bufsize) { ok = false; break; }
                    if (set_field_in(nc, fid, buffer + off + 2, len)) applied++;
                    else ok = false;
                    off += 2 + len;
                }
                if (ok && applied == n) buf[2] = 0x00;
                buf[3] = applied;
                if (applied) set_config(reinterpret_cast<const uint8_t *>(&nc), sizeof(nc));
            }
            feature_data[0x81].assign(buf, buf + sizeof(buf));
            break;
        }
        case 0x0c: {
            // BULK get config fields. Payload: [n, fid...]. Reply: 0x66 0x0c 0x00 n
            // then n x (fid, len, value_bytes) - the read-side twin of 0x0b, so a
            // full portal read is a few packets instead of ~60.
            uint8_t buf[63]{}; buf[0] = 0x66; buf[1] = 0x0c; buf[2] = 0x01;
            if (bufsize >= 1) {
                const uint8_t n = buffer[0];
                uint16_t out = 4; uint8_t done = 0; bool ok = true;
                for (uint8_t i2 = 0; i2 < n && ok; ++i2) {
                    if ((uint16_t)(1 + i2) >= bufsize) { ok = false; break; }
                    const uint8_t fid = buffer[1 + i2];
                    uint8_t tmp[8]{};
                    if (!get_config_field(fid, tmp, sizeof(tmp))) { ok = false; break; }
                    // get_config_field writes the raw value; length by field type:
                    // reuse its convention - u8=1, u16=2, f32=4. Determine via a
                    // conservative probe: write_config_value zero-fills, so track
                    // known u16/f32 ids explicitly.
                    uint8_t len = 1;
                    switch (fid) {
                        // u16 fields (must match the portal FIELDS table)
                        case 0x00: case 0x14: case 0x1b: case 0x1c:
                        case 0x1f: case 0x26: case 0x40: case 0x4a: len = 2; break;
                        case 0x01: len = 4; break; // haptics_gain f32
                        default: len = 1; break;
                    }
                    if ((uint16_t)(out + 2 + len) > sizeof(buf)) { ok = false; break; }
                    buf[out] = fid; buf[out + 1] = len;
                    memcpy(buf + out + 2, tmp, len);
                    out += 2 + len;
                    done++;
                }
                if (ok) buf[2] = 0x00;
                buf[3] = done;
            }
            feature_data[0x81].assign(buf, buf + sizeof(buf));
            break;
        }
    }
}

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

template<typename T>
static bool write_config_value(uint8_t *buffer, uint16_t bufsize, T value) {
    if (bufsize < sizeof(T)) {
        return false;
    }
    memcpy(buffer, &value, sizeof(T));
    return true;
}

static bool set_config_field(uint8_t field_id, uint8_t const *buffer, uint16_t bufsize) {
    Config_body new_config = get_config();

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
        default:
            printf("[CMD] Unknown config field id: 0x%02X\n", field_id);
            return false;
    }

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
    }
}

//
// Created by awalol on 2026/5/4.
//

#ifndef DS5_BRIDGE_CONFIG_H
#define DS5_BRIDGE_CONFIG_H

#include <cstdint>

struct __attribute__((packed)) Config_body {
    uint8_t config_version; // Config Version
    float haptics_gain; // [1.0,2.0]
    uint8_t speaker_volume; // [0,127]
    uint8_t headset_volume; // [0,127] // max 0x7f
    uint8_t sync_spk_headset_volume; // bool: 0 disable,1 enable
    uint8_t speaker_gain; // [0,7]
    uint8_t inactive_time; // [5,60] min
    uint8_t disable_inactive_disconnect; // bool: 0 disable,1 enable
    uint8_t disable_pico_led; // bool
    uint8_t polling_rate_mode; // 0: 250Hz, 1: 500Hz, 2: real-time
    uint8_t audio_buffer_length; // [16,128]
    uint8_t controller_mode; // 0: DS5, 1: DSE, 2: Auto
    uint8_t lock_volume; // 0: disable,1: enable
    uint8_t disable_usb_sn; // 0: disable,1: enable
    uint8_t ps_shortcut_enabled; // 0: disabled, 1: enabled (Xbox Game Bar via HID keyboard)
    uint8_t disable_mic; // bool: 0 enable (default), 1 disable controller mic
    uint8_t disable_speaker; // bool: 0 enable (default), 1 disable speaker/headset
    uint8_t enable_wake; // bool: 0 disabled (default), 1 wake host on PS press (USB remote wakeup)
    uint8_t auto_haptics_enable;
    uint8_t auto_haptics_gain;
    uint16_t auto_haptics_lowpass_hz;
    uint8_t auto_mute_replace;
    uint8_t auto_mute_mix;
    uint8_t auto_haptics_gate; // noise gate threshold [0-100], 0=off; suppresses dialog/quiet content
    uint8_t auto_haptics_slope; // LP filter slope in dB/oct: 6, 12, or 24
    uint8_t lightbar_off; // bool: force the RGB lightbar off (e.g. blue glow in Xbox360/DS4 mode)
    uint8_t auto_haptics_smooth; // 0-100: response smoothing (release/decay), higher=smoother
    uint16_t bt_flush_timeout; // ACL automatic flush timeout in 0.625ms units; 0=off (infinite)
    uint16_t bt_qos_latency_us; // QoS requested link latency in microseconds; 0=off
    uint8_t rumble_haptic_strength; // [0-100] strength of converted DS4Windows rumble blended in Mix mode
    uint8_t effect_leak_volume; // [0-100] volume of high-passed effect leak through speaker when auto-muted; 0=off
    uint16_t effect_leak_hp_hz; // high-pass cutoff (Hz) for the effect leak detection band
    uint8_t effect_leak_sensitivity; // [0-100] transient detection sensitivity; higher=more eager (more leaks through)
    uint8_t effect_leak_decay; // [0-100] fade-out length after a transient; higher=longer/more gradual tail
    uint8_t effect_leak_attack; // [0-100] gate open speed; higher=more immediate (less delay)
    uint16_t effect_leak_output_hp_hz; // output high-pass cutoff (Hz) — protects speaker from low-freq popping
    // Rumble-to-trigger: express the game's rumble as trigger Vibration (effect 0x26).
    uint8_t r2t_mode;       // 0=off, 1=left trigger only, 2=right trigger only, 3=both
    uint8_t r2t_on_press;   // bool: 0=vibrate regardless of trigger position, 1=only when trigger pressed
    uint8_t r2t_strength;   // [0-100] amplitude multiplier applied to the rumble value
    uint8_t r2t_frequency;  // vibration frequency parameter for the 0x26 effect [1-255], ~tactile buzz
    // Adaptive triggers Stage 1: L2-gated R2 resistance. When L2 is pressed past the
    // threshold (aiming), R2 gets a constant resistance (Feedback effect 0x21).
    uint8_t at_mode;        // 0=off, 1=L2 gates R2 resistance
    uint8_t at_strength;    // [0-100] resistance intensity (mapped to 0-8 effect strength)
    uint8_t at_threshold;   // [0-255] how far L2 must be pressed to arm R2 resistance
    uint8_t at_start_pos;   // [0-9] trigger position where R2 resistance begins
    // Gyro -> right-stick aiming: adds controller angular velocity onto the right
    // stick in the input report, so ANY game gets gyro aim with no PC software.
    uint8_t gyro_mode;      // 0=off, 1=L2-held, 2=always, 3=touchpad-touch enables, 4=always but touch pauses (ratchet)
    uint8_t gyro_sens;      // [1-100] sensitivity (50 = raw/40 per report)
    uint8_t gyro_axis;      // horizontal source: 0=yaw (turn), 1=roll (tilt sideways)
    uint8_t gyro_invert;    // bit0 = invert X, bit1 = invert Y
    uint8_t haptics_aa;     // native-haptics smoothing: 1=off (raw/gritty), 2=light 1-pole ~2.4kHz (default), 3=strong 2-pole ~1.3kHz
    uint8_t synth_force;    // 0=yield to game trigger effects (default), 1=force r2t/at even if a game/app sends effects
};

struct __attribute__((packed)) Config {
    uint32_t magic;
    uint32_t crc32; // Config_body crc32, only calc and verify when save
    uint16_t size;  // Config_body size
    Config_body body;
};

void config_default();
void config_load();
bool config_save();
Config_body& get_config();
void set_config(const uint8_t *new_config, const uint16_t len);
void config_valid();
void set_config(const Config_body &new_config);
void set_gain(uint8_t value);
extern bool is_dse;

#endif //DS5_BRIDGE_CONFIG_H

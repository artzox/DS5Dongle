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
    // Adaptive triggers Stage 2: push-back kick (recoil). While resistance is
    // engaged, the vibration envelope momentarily raises the resistance strength,
    // pressing the trigger back against the finger - recoil on top of resistance.
    uint8_t at_pushback;     // [0-100] kick strength; 0=off (Stage 1 behavior unchanged)
    uint8_t at_pushback_src; // envelope source: 0=rumble only, 1=audio haptics only, 2=both (max)
    uint8_t at_pushback_freq;// [10-200] vibration frequency of the kick thump; lower = heavier knock (default 35)
    // Effect-leak band-pass window + gate hold (v1.2.0). The output high-pass
    // (effect_leak_output_hp_hz) forms the LOW wall of the window; this low-pass
    // is the HIGH wall. Both are 12 dB/oct. Sound outside the window never leaks,
    // which is what makes the leak selective instead of "thin treble = crackle".
    uint16_t effect_leak_lp_hz; // output low-pass cutoff (Hz); default 3500
    uint8_t  effect_leak_hold;  // [0-100] min gate-open hold after a transient (x5 = 0-500 ms); stops flutter ("missing and poppy")
    // Per-trigger adaptive-trigger modes (v1.2.1). Each trigger has its own mode;
    // strength/threshold/start-position/kick parameters are shared. "Gated" means
    // armed by the OPPOSITE trigger passing at_threshold (R2 gated = L2 arms it,
    // L2 gated = R2 arms it), with the same hysteresis as before.
    // Fully independent per-trigger adaptive triggers (v1.3.1). The at_* fields
    // above (mode/strength/threshold/start/pushback/freq) are R2's; the at_l2_*
    // fields below are L2's own complete set. Only at_pushback_src (the kick
    // envelope source) is shared — it's one signal. Per-trigger kick strength 0
    // simply disables the kick on that trigger.
    uint8_t  at_kick_style;      // R2 kick delivery: 0=vibration thump (0x26), 1=bow snap (0x22) — the
                                 // bow's snap force physically presses the trigger back (sharper; feel
                                 // varies with hold depth).
    uint8_t  at_l2_mode;         // L2: 0=off (default), 1=gated (R2 arms), 2=always on
    uint8_t  at_l2_strength;     // L2 resistance strength [0-100]
    uint8_t  at_l2_threshold;    // R2 press depth that arms L2 in gated mode [1-255]
    uint8_t  at_l2_start_pos;    // L2 resistance start zone [0-9]
    uint8_t  at_l2_pushback;     // L2 kick strength [0-100]; 0 = no kick on L2
    uint8_t  at_l2_pushback_freq;// L2 kick thump frequency [10-200]
    uint8_t  at_l2_kick_style;   // L2 kick delivery: 0=thump, 1=bow snap
    // Auto-haptics frequency split (v1.5.0). 0 = OFF (default): the single-band
    // path is byte-identical to previous firmware. When set (30-200 Hz), the
    // haptics band is divided at the crossover: LOW band (below - impacts,
    // explosions, engine weight) and HIGH band (crossover..LP cutoff - where
    // music bass lines and voice fundamentals live), each enveloped separately
    // and weighted by its own gain before the shared gate + carrier. Typical
    // use: keep low at 100, drop high to tame music/dialog-driven buzz.
    uint16_t ah_xover_hz;   // 0=off, else 30-200 Hz crossover
    uint8_t  ah_low_gain;   // [0-100] weight of the low band (default 100)
    uint8_t  ah_high_gain;  // [0-100] weight of the high band (default 100)
    // Adaptive-trigger resistance SHAPES (v1.7.0). The DualSense feedback effect
    // (0x21) supports 10 independent travel zones with 3-bit strength each - the
    // controller evaluates trigger position in hardware. Shapes program that
    // zone table:
    //   0 = Constant: start_pos..9 at Strength A (pre-1.7.0 behavior)
    //   1 = Ramp: linear A -> B across start_pos..9 (racing: light->heavy gas,
    //       or heavy->light brake bite - direction is just A vs B)
    //   2 = Two-stage detent: base Strength A with a WALL of Strength B at the
    //       detent zone - a tactile bump marking half-press (fire) from
    //       full-press (alt-fire)
    uint8_t  at_shape;          // R2 shape 0-2
    uint8_t  at_strength_b;     // R2 strength B [0-100] (ramp end / detent wall)
    uint8_t  at_detent_pos;     // R2 detent zone 0-9 (shape 2)
    uint8_t  at_l2_shape;       // L2 shape 0-2
    uint8_t  at_l2_strength_b;  // L2 strength B [0-100]
    uint8_t  at_l2_detent_pos;  // L2 detent zone 0-9 (shape 2)
    // Trigger activation dead zone (v1.8.0): below the configured zone the HOST
    // sees the trigger untouched (analog 0, digital bit cleared) - the game's
    // action registers only once the pull reaches the zone, aligning early-firing
    // games with the resistance/detent/bow feel. Internal effects (gating, kick,
    // shapes) always see the RAW trigger. 0 = off, 1-9 = first registered zone.
    uint8_t  at_deadzone;       // R2: 0=off, 1-9 first zone the host sees
    uint8_t  at_l2_deadzone;    // L2: 0=off, 1-9 first zone the host sees
    // Mix-mode native passthrough level (v1.10.0). In Mix, ch3/4 pass through to
    // the actuators UNSCALED by Intensity/split/gate - correct for real native
    // haptics, but with ds5audio's `duplicate` mapping ch3/4 carry a copy of the
    // game audio, drowning the adjustable derived part. This fader scales the
    // passthrough per profile: 100 = classic native Mix, 0 = derived+rumble only
    // (auto-haptics own the actuators; the ds5audio --map choice stops mattering).
    uint8_t  mix_native_level;  // [0-100] ch3/4 contribution in Mix (default 100)
    // Effect leak MAX BURST (v1.12.0): cap on how long one gate opening may last
    // (x5 ms, 0 = off/unlimited). Transients (gunshots, impacts) end within the
    // cap naturally; SUSTAINED content (dialogue, music) used to hold the gate
    // open and duplicate the room audio - now it gets cut at the cap and the
    // gate stays closed (refractory) until the signal genuinely falls, so one
    // sustained sound = one short accent, not a stream. Turns the leak into a
    // percussion layer that punctuates instead of duplicating.
    uint8_t  effect_leak_max_burst; // x5 ms, 0=off, e.g. 30 = 150 ms bursts
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

// --- Profile slots -----------------------------------------------------------
// Named copies of Config_body stored in their own flash sector (the sector
// below the active-config sector). Saving a slot is a rare manual portal
// action; activating one at game launch is a single atomic command instead of
// a 30-field write.
constexpr uint8_t SLOT_COUNT = 16;       // v1.9.0: 16 (was 8) - 8 per flash sector
constexpr uint8_t SLOTS_PER_SECTOR = 8;  // 512-byte stride in a 4 KB sector
constexpr uint8_t SLOT_NAME_LEN = 16;
bool slot_save(uint8_t idx, const uint8_t *name, uint8_t name_len); // current config.body -> slot
bool slot_activate(uint8_t idx, bool &needs_reenum);                // slot -> active config + flash
bool slot_info(uint8_t idx, uint8_t name_out[SLOT_NAME_LEN], uint8_t &valid, uint8_t &cfg_version);
Config_body& get_config();
void set_config(const uint8_t *new_config, const uint16_t len);
void config_valid();
void set_config(const Config_body &new_config);
void set_gain(uint8_t value);
extern bool is_dse;

#endif //DS5_BRIDGE_CONFIG_H

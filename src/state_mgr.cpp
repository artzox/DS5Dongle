//
// Created by awalol on 2026/5/15.
//
// Auto-haptics / DS4Windows modifications (c) 2026 artzox — MIT License.
// The state_set-in-RAM relocation that enables haptic actuation at stock clocks
// originates from loteran's auto-haptics work on the DS5Dongle. With thanks.
//

#include "state_mgr.h"

#include <cstddef>
#include <cstring>

#include "pico.h"
#include "pico/time.h"
#include "config.h"
#include "utils.h"

namespace {
    constexpr size_t kAudioControlOffset = offsetof(SetStateData, MuteLightMode) - sizeof(uint8_t);
    constexpr size_t kMuteControlOffset = offsetof(SetStateData, RightTriggerFFB) - sizeof(uint8_t);
    constexpr size_t kMotorPowerLevelOffset = offsetof(SetStateData, HostTimestamp) + sizeof(uint32_t);
    constexpr size_t kAudioControl2Offset = kMotorPowerLevelOffset + sizeof(uint8_t);
    constexpr size_t kHapticLowPassFilterOffset = offsetof(SetStateData, LightFadeAnimation) - 2 * sizeof(uint8_t);
    constexpr size_t kPlayerIndicatorsOffset = offsetof(SetStateData, LedRed) - sizeof(uint8_t);
}

static constexpr uint8_t state_init_data[63] = {
    0xfd, 0xf7, 0x0, 0x0,
    0x0, 0x0, // Headphones, Speaker
    0xff, 0x9, 0x0, 0x0F, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xa,
    0x7, 0x0, 0x0, 0x2, 0x1,
    0x00,
    0xff, 0xd7, 0x00 // RGB LED: R, G, B (Nijika Color!)✨
};

SetStateData state{};
// Live synthesis diagnostics, readable via config field 0x36:
//   bit0 game-owns-L2, bit1 game-owns-R2, bit2 synth-owns-L2, bit3 synth-owns-R2, bit4 at-engaged
volatile uint8_t g_diag_synth = 0;
volatile uint8_t g_diag_at_env = 0; // live push-back envelope (0-255), diag field 0x3c
// Synthesized-trigger bookkeeping (file scope so the staleness watchdog can see it).
static bool synth_owned_l2 = false, synth_owned_r2 = false, at_engaged = false, at_engaged_l2 = false;
// Host-report cache for the synth tick. The controller only ever receives a
// state report when we compose+send one; historically that happened ONLY on host
// output reports. Games that send reports solely when rumble changes go silent
// between actions - so a trigger that engaged during the action stayed engaged on
// the controller forever (stuck resistance), and gated engagement from live
// trigger movement could never transmit either. state_synth_tick() (main loop)
// replays the cached host intent with live trigger positions and lets main push
// the result whenever it actually changed.
static uint8_t  g_host_cache[sizeof(SetStateData)];
static uint8_t  g_host_cache_size = 0;
static uint32_t g_last_host_ms = 0;

static bool state_update_apply(const uint8_t *data, uint8_t size);

bool state_synth_tick() {
    if (!g_host_cache_size) return false;                  // no host intent cached yet
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - g_last_host_ms < 50) return false;           // host actively driving
    alignas(4) static uint8_t copy[sizeof(SetStateData)];
    memcpy(copy, g_host_cache, g_host_cache_size);
    if (now - g_last_host_ms >= 300) {
        // Stale rumble must not keep synthesizing (old watchdog semantics): zero
        // the cached rumble so r2t and the rumble-fed kick decay naturally, while
        // AT gating keeps re-evaluating from LIVE trigger positions.
        auto *u = reinterpret_cast<SetStateData *>(copy);
        u->EnableRumbleEmulation = 0;
        u->EnableImprovedRumbleEmulation = 0;
        u->RumbleEmulationRight = 0;
        u->RumbleEmulationLeft = 0;
    }
    return state_update_apply(copy, g_host_cache_size);
}

void state_init() {
    memcpy(&state, state_init_data, sizeof(state));
    state.VolumeSpeaker = get_config().speaker_volume;
    state.VolumeHeadphones = get_config().headset_volume;
    set_volume(get_config().speaker_volume, get_config().headset_volume);
    set_gain(get_config().speaker_gain);
}

void __not_in_flash_func(state_set)(uint8_t *data, const uint8_t size) {
    if (size > 63) {
        printf("[StateMgr] Warning: State Set over 63 bytes\n");
    }
    memcpy(data, &state, size);
    // Force the RGB lightbar off in Replace mode (when the toggle is enabled).
    // Replace = pure audio-derived haptics, typically used in emulated/non-native
    // scenarios where the game never sends DualSense lightbar commands, so it
    // sits at a default glow (blue in DS4Windows). In native (auto-haptics Off)
    // or Mix, the lightbar is left as-is so games can control it (passthrough).
    if (get_config().lightbar_off &&
        get_config().auto_haptics_enable == 2 &&
        size > 46) {
        data[1] |= 0x04;   // AllowLedColor
        data[44] = 0x00;   // LedRed
        data[45] = 0x00;   // LedGreen
        data[46] = 0x00;   // LedBlue
    }
}

bool state_update(const uint8_t *data, const uint8_t size) {
    if (size <= sizeof(SetStateData)) {
        memcpy(g_host_cache, data, size);
        g_host_cache_size = size;
        g_last_host_ms = to_ms_since_boot(get_absolute_time());
    }
    return state_update_apply(data, size);
}

static bool state_update_apply(const uint8_t *data, const uint8_t size) {
    if (size > sizeof(SetStateData)) {
        printf(
            "[StateMgr] Error: SetStateData max %u bytes, request %u\n",
            static_cast<unsigned>(sizeof(SetStateData)),
            size
        );
        return false;
    }

    // Snapshot before applying, to detect whether the controller-facing output
    // actually changed (upstream "Fix stuck rumble"). When the speaker is active,
    // main.cpp skips re-sending state for efficiency, which swallowed rumble
    // start/stop and left motors stuck on. Report the change so a genuine rumble
    // change is still sent even while audio plays.
    SetStateData old_state = state;

    SetStateData update{};
    memcpy(&update, data, size);

    auto *state_bytes = reinterpret_cast<uint8_t *>(&state);
    const auto copy_if_allowed = [&](const bool allowed, const size_t offset, const size_t length) {
        const size_t end = offset + length;
        // offset/length are byte ranges in SetStateData. Skip the copy if the
        // sender did not allow this field, or if a short report does not contain it.
        if (!allowed || end < offset || end > sizeof(state) || end > size) {
            return;
        }

        memcpy(state_bytes + offset, data + offset, length);
    };
    /*auto set_bit = [](uint8_t &byte, const int bit, const bool value) {
        byte = (byte & ~(1 << bit)) | (value << bit);
    };*/

    state.EnableRumbleEmulation = update.EnableRumbleEmulation;
    state.UseRumbleNotHaptics = update.UseRumbleNotHaptics;
    state.EnableImprovedRumbleEmulation = update.EnableImprovedRumbleEmulation;
    // Track the game's trigger-FFB Allow bits from the incoming report each cycle.
    // These are bitfields in report byte 0 (next to the rumble/haptic-mode flags);
    // if we let a stale value linger it corrupts byte 0. The synthesis block below
    // may override these, but the baseline must follow the host so that when neither
    // the game nor synthesis wants the triggers, byte 0 returns to a clean state.
    state.AllowRightTriggerFFB = update.AllowRightTriggerFFB;
    state.AllowLeftTriggerFFB = update.AllowLeftTriggerFFB;
    // When auto-haptics is enabled (Mix/Replace), we drive the haptic ACTUATORS
    // from audio. Forcing UseRumbleNotHaptics=true (e.g. because DS4Windows sends
    // rumble) switches the controller to rumble MOTORS and silences the actuator
    // path our auto-haptics needs. So only force rumble mode when auto-haptics
    // is OFF (pure native rumble->DualSense conversion).
    const bool auto_haptics_on = get_config().auto_haptics_enable > 0;
    // When auto-haptics is on, capture the incoming rumble-emulation motor values
    // so the audio path can blend them into the actuator signal (rather than using
    // the controller's separate rumble-motor mode, which conflicts with actuator
    // haptics). Lets us MIX converted rumble + audio-derived haptics, both through
    // the actuators, with independent strength control.
    extern volatile uint8_t g_rumble_l, g_rumble_r;
    g_rumble_l = update.RumbleEmulationLeft;
    g_rumble_r = update.RumbleEmulationRight;
    if (!auto_haptics_on && (update.RumbleEmulationLeft > 0 || update.RumbleEmulationRight > 0)) {
        // why doesn't ninja gaiden 4 enable UseRumbleNotHaptics
        state.UseRumbleNotHaptics = true;
    }
    if (auto_haptics_on) {
        // Force actuator (haptic) mode so derived haptics play even while a game
        // is sending rumble through DS4Windows.
        state.UseRumbleNotHaptics = false;
        state.EnableRumbleEmulation = false;
        state.EnableImprovedRumbleEmulation = false;
    }
    if (state.EnableRumbleEmulation ||
        state.UseRumbleNotHaptics ||
        state.EnableImprovedRumbleEmulation) {
        state.RumbleEmulationLeft = update.RumbleEmulationLeft;
        state.RumbleEmulationRight = update.RumbleEmulationRight;
    }

    if (!get_config().lock_volume && update.AllowHeadphoneVolume) {
        get_config().headset_volume = update.VolumeHeadphones;
        state.VolumeHeadphones = update.VolumeHeadphones;
    }
    if (!get_config().lock_volume && update.AllowSpeakerVolume) {
        get_config().speaker_volume = update.VolumeSpeaker;
        state.VolumeSpeaker = update.VolumeSpeaker;
    }
    copy_if_allowed(
        update.AllowMicVolume,
        offsetof(SetStateData, VolumeMic),
        sizeof(update.VolumeMic)
    );
    copy_if_allowed(
        update.AllowAudioControl,
        kAudioControlOffset,
        sizeof(uint8_t)
    );

    copy_if_allowed(
        update.AllowMuteLight,
        offsetof(SetStateData, MuteLightMode),
        sizeof(update.MuteLightMode)
    );

    copy_if_allowed(
        update.AllowAudioMute,
        kMuteControlOffset,
        sizeof(uint8_t)
    );

    copy_if_allowed(
        update.AllowRightTriggerFFB,
        offsetof(SetStateData, RightTriggerFFB),
        sizeof(update.RightTriggerFFB)
    );
    copy_if_allowed(
        update.AllowLeftTriggerFFB,
        offsetof(SetStateData, LeftTriggerFFB),
        sizeof(update.LeftTriggerFFB)
    );

    // --- Synthesized trigger effects ---
    // Two features share this block:
    //   1) Rumble->trigger vibration (r2t): express the game's rumble as trigger
    //      Vibration (0x26) so any rumbling game produces trigger buzz.
    //   2) Adaptive triggers Stage 1 (at): L2-gated R2 resistance (Feedback 0x21) —
    //      while L2 (aim) is held past a threshold, R2 gets constant resistance.
    // Both YIELD to the game: if the game drives a trigger, we leave it alone. Games
    // often send their trigger effect ONCE (Allow bit set) and then stop setting the
    // bit, so a per-cycle check alone would stomp the native effect on the next
    // packet — we therefore latch the last time the game drove each trigger and
    // yield for a few seconds after.
    {
        const auto &cfg = get_config();

        bool at_kick_diag = false; // KICK burst active this cycle (diag bit 32)
        // -- game-ownership recency latch --
        static uint32_t last_game_r2_ms = 0, last_game_l2_ms = 0;
        static bool game_ever_r2 = false, game_ever_l2 = false;
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        // A game "actively drives" a trigger only if the Allow bit comes WITH a real
        // effect payload. Input layers (Steam Input, DS4Windows-style wrappers, some
        // games) set the Allow bits in EVERY output report with an empty/Off payload
        // — the old check latched that as permanent ownership, which made r2t/at
        // appear completely dead. 0x00 = no effect, 0x05 = Off: neither is "driving".
        const bool game_r2_now = update.AllowRightTriggerFFB &&
            update.RightTriggerFFB[0] != 0x00 && update.RightTriggerFFB[0] != 0x05;
        const bool game_l2_now = update.AllowLeftTriggerFFB &&
            update.LeftTriggerFFB[0] != 0x00 && update.LeftTriggerFFB[0] != 0x05;
        if (game_r2_now) { last_game_r2_ms = now_ms; game_ever_r2 = true; }
        if (game_l2_now) { last_game_l2_ms = now_ms; game_ever_l2 = true; }
        constexpr uint32_t GAME_FFB_YIELD_MS = 3000;
        bool game_owns_r2 = game_r2_now ||
            (game_ever_r2 && (now_ms - last_game_r2_ms) < GAME_FFB_YIELD_MS);
        bool game_owns_l2 = game_l2_now ||
            (game_ever_l2 && (now_ms - last_game_l2_ms) < GAME_FFB_YIELD_MS);
        // Escape hatch: force the synthesized effects regardless of what the
        // game/app sends (portal: "Force override").
        if (cfg.synth_force) { game_owns_r2 = false; game_owns_l2 = false; }

        // -- helpers --
        // Pack a uniform 3-bit value into the per-zone field: 10 zones x 3 bits =
        // 30 bits, little-endian from byte 3 (zone i's field starts at bit 3*i).
        auto pack_zones = [](uint8_t *ffb, uint16_t zones, uint8_t val3) {
            uint32_t bits = 0;
            for (int z = 0; z <= 9; ++z)
                if (zones & (1u << z)) bits |= (uint32_t)(val3 & 0x7u) << (3 * z);
            ffb[3] = (uint8_t)(bits & 0xFF);
            ffb[4] = (uint8_t)((bits >> 8) & 0xFF);
            ffb[5] = (uint8_t)((bits >> 16) & 0xFF);
            ffb[6] = (uint8_t)((bits >> 24) & 0xFF);
        };
        // pos = current analog position of THIS trigger (0-255). When on_press is
        // set we only emit vibration while the trigger is actually pulled past a
        // threshold — the zone bitmap alone does NOT gate on position (the 0x26
        // effect buzzes continuously whenever amplitude>0 regardless of pull), which
        // is why "on press" used to buzz constantly. Gating on pos is the real fix.
        auto write_vibration = [&](uint8_t *ffb, uint8_t rumble, uint8_t pos) {
            uint16_t amp = (uint16_t)rumble * (uint16_t)cfg.r2t_strength / 100u;
            if (amp > 255) amp = 255;
            for (int i = 0; i < 11; ++i) ffb[i] = 0;
            // On-press gate: below ~25% pull, emit Off so the trigger stays quiet
            // until the user actually presses it.
            if (cfg.r2t_on_press && pos < 64) { ffb[0] = 0x05; return; }
            if (amp == 0) { ffb[0] = 0x05; return; }
            ffb[0] = 0x26; // Vibration
            // Full-travel zones now (position gating handles "on press"); this keeps
            // the buzz feeling consistent once engaged rather than only in a sub-band.
            const uint16_t zones = 0x03FF;
            ffb[1] = (uint8_t)(zones & 0xFF);
            ffb[2] = (uint8_t)((zones >> 8) & 0x03);
            pack_zones(ffb, zones, (uint8_t)(amp * 7u / 255u));
            ffb[9] = cfg.r2t_frequency;
        };

        // -- what does synthesis want this cycle? --
        const bool r2t_wants_l2 = (cfg.r2t_mode == 1 || cfg.r2t_mode == 3);
        const bool r2t_wants_r2 = (cfg.r2t_mode == 2 || cfg.r2t_mode == 3);

        // at: gated resistance with hysteresis so hovering at the threshold doesn't
        // chatter the trigger motor. r2t has priority conflict resolution below.
        // Per-trigger modes (v1.2.1): each trigger has its own independent mode —
        //   at_mode    = R2: 0=off, 1=gated (L2 arms it), 2=always on
        //   at_l2_mode = L2: 0=off, 1=gated (R2 arms it), 2=always on
        // "Gated" arms when the OPPOSITE trigger passes at_threshold, with the same
        // release hysteresis. Any combination is valid: L2 always + R2 gated,
        // L2 always + R2 off, both gated (each armed by the other), etc.
        extern volatile uint8_t g_l2_pos, g_r2_pos;
        auto at_engage = [&](uint8_t mode, uint8_t gate_pos, uint8_t thr, uint8_t strength, bool &engaged) -> bool {
            if (mode == 0 || strength == 0) { engaged = false; return false; }
            if (mode == 2) { engaged = true; return true; }     // always on
            const uint8_t rel = (thr > 15) ? (uint8_t)(thr - 15) : 1; // mode 1: gated + hysteresis
            if (!engaged && gate_pos >= thr)     engaged = true;
            else if (engaged && gate_pos < rel)  engaged = false;
            return engaged;
        };
        const bool at_wants_r2 = at_engage(cfg.at_mode > 2 ? 0 : cfg.at_mode, g_l2_pos, cfg.at_threshold, cfg.at_strength, at_engaged);
        const bool at_wants_l2 = at_engage(cfg.at_l2_mode > 2 ? 0 : cfg.at_l2_mode, g_r2_pos, cfg.at_l2_threshold, cfg.at_l2_strength, at_engaged_l2);

        // -- Stage 2 kick: computed ONCE per cycle (shared envelope + burst state),
        // then written to whichever target trigger(s) are engaged. See the Stage 2
        // comment below for the mechanism (vibration burst instead of static force).
        bool at_kick_now = false;      // shared burst state (envelope is one signal)
        uint8_t at_kick_r2_amp3 = 0;   // per-trigger kick amplitudes from their own strengths
        uint8_t at_kick_l2_amp3 = 0;
        const bool r2_kick_cfg = at_wants_r2 && cfg.at_pushback > 0;
        const bool l2_kick_cfg = at_wants_l2 && cfg.at_l2_pushback > 0;
        if (r2_kick_cfg || l2_kick_cfg) {
            uint16_t env = 0; // vibration envelope, 0-255
            if (cfg.at_pushback_src == 0 || cfg.at_pushback_src == 2) {
                const uint8_t hr = update.EnableRumbleEmulation ? update.RumbleEmulationRight : 0;
                const uint8_t hl = update.EnableRumbleEmulation ? update.RumbleEmulationLeft  : 0;
                extern volatile uint8_t g_rumble_l, g_rumble_r; // converted-rumble path
                const uint8_t g = (g_rumble_l > g_rumble_r) ? g_rumble_l : g_rumble_r;
                const uint8_t h = (hr > hl) ? hr : hl;
                env = (h > g) ? h : g;
            }
            if (cfg.at_pushback_src >= 1) {
                extern volatile uint16_t g_diag_ch01_peak; // live auto-haptics input peak (0-32767)
                uint16_t a = (uint16_t)(g_diag_ch01_peak >> 7); // -> 0-255
                if (a > 255) a = 255;
                if (a > env) env = a;
            }
            g_diag_at_env = (uint8_t)env;
            static bool     kick_on = false;
            static uint32_t kick_hold_until_ms = 0;
            if (!kick_on && env >= 32) { kick_on = true; kick_hold_until_ms = now_ms + 45; }
            else if (kick_on && env < 16 && (int32_t)(now_ms - kick_hold_until_ms) >= 0) kick_on = false;
            if (kick_on) {
                auto scale3 = [&](uint8_t strength) -> uint8_t {
                    uint32_t a = ((uint32_t)env * strength * 7u + 12750u) / 25500u;
                    return (uint8_t)((a > 7) ? 7 : a);
                };
                if (r2_kick_cfg) at_kick_r2_amp3 = scale3(cfg.at_pushback);
                if (l2_kick_cfg) at_kick_l2_amp3 = scale3(cfg.at_l2_pushback);
                at_kick_now = (at_kick_r2_amp3 > 0) || (at_kick_l2_amp3 > 0);
            }
        } else if (at_wants_r2 || at_wants_l2) {
            g_diag_at_env = 0;
        }

        // -- AT effect writer: kick burst (Vibration) while hot, else constant
        // resistance (Feedback). Written identically to each target trigger.
        auto write_at = [&](uint8_t *ffb, uint8_t strength, uint8_t start_pos,
                            uint8_t kick_amp3, uint8_t kick_style, uint8_t kick_freq) {
            for (int i = 0; i < 11; ++i) ffb[i] = 0;
            if (at_kick_now && kick_amp3 > 0) {
                if (kick_style == 1) {
                    // Bow snap (0x22): start/end zone pair + strength/snap-force
                    // pair. With the trigger already held past the end zone, the
                    // snap force presses the trigger back against the finger for
                    // the duration of the burst — a mechanical recoil rather than
                    // a buzz. Layout (Nielk1 factory recipe): bytes 1-2 = 16-bit
                    // zone mask (1<<start | 1<<end); byte 3 = ((strength-1)&7) |
                    // (((snapForce-1)&7)<<3), both 1-8 on the wire as value-1.
                    ffb[0] = 0x22;
                    uint8_t bstart = (start_pos > 7) ? 7 : start_pos;
                    uint8_t bend = (uint8_t)(bstart + 4); if (bend > 8) bend = 8;
                    const uint16_t zones = (uint16_t)((1u << bstart) | (1u << bend));
                    ffb[1] = (uint8_t)(zones & 0xFF);
                    ffb[2] = (uint8_t)((zones >> 8) & 0xFF);
                    uint8_t wire = (uint8_t)(((uint16_t)strength * 7u + 50u) / 100u);
                    if (wire > 7) wire = 7;               // strength-1 (draw resistance)
                    ffb[3] = (uint8_t)((wire & 0x07) | ((kick_amp3 & 0x07) << 3)); // snapForce-1 = envelope
                } else {
                    ffb[0] = 0x26;                    // Vibration: the recoil thump
                    const uint16_t vz = 0x03FF;       // full travel
                    ffb[1] = (uint8_t)(vz & 0xFF);
                    ffb[2] = (uint8_t)((vz >> 8) & 0x03);
                    pack_zones(ffb, vz, kick_amp3);
                    ffb[9] = kick_freq;               // low = heavier knock
                }
            } else {
                ffb[0] = 0x21; // Feedback (constant resistance)
                const uint8_t start = (start_pos > 9) ? 0 : start_pos;
                uint16_t zones = 0;
                for (int z = start; z <= 9; ++z) zones |= (1u << z);
                ffb[1] = (uint8_t)(zones & 0xFF);
                ffb[2] = (uint8_t)((zones >> 8) & 0x03);
                uint8_t wire = (uint8_t)(((uint16_t)strength * 7u + 50u) / 100u);
                if (wire > 7) wire = 7;
                pack_zones(ffb, zones, wire);
            }
        };

        // -- release helper: remove OUR footprint from the persistent state.
        // Clears the FFB bytes we wrote and drops our claim. It clears the Allow bit
        // ONLY if the game isn't currently driving that trigger (the top-of-function
        // sync already set state.Allow from the host; if the game owns it we must
        // leave that bit + the copied game FFB intact). When neither game nor synth
        // wants the trigger, this returns byte 0 to its clean native value — which is
        // what fixes native haptics breaking, since the Allow bits share byte 0 with
        // the rumble/haptic-mode flags.
        auto release_left = [&](bool game_has_it) {
            if (!synth_owned_l2) return;
            for (int i = 0; i < 11; ++i) state.LeftTriggerFFB[i] = 0;
            if (!game_has_it) state.AllowLeftTriggerFFB = 0;
            synth_owned_l2 = false;
        };
        auto release_right = [&](bool game_has_it) {
            if (!synth_owned_r2) return;
            for (int i = 0; i < 11; ++i) state.RightTriggerFFB[i] = 0;
            if (!game_has_it) state.AllowRightTriggerFFB = 0;
            synth_owned_r2 = false;
        };

        // -- LEFT trigger --
        // Priority mirrors R2: game > resistance/kick (if targeted) > vibration.
        if (game_owns_l2) {
            release_left(true);      // game drives it: drop our claim, keep its effect
        } else if (at_wants_l2) {
            write_at(state.LeftTriggerFFB, cfg.at_l2_strength, cfg.at_l2_start_pos,
                     at_kick_l2_amp3, cfg.at_l2_kick_style, cfg.at_l2_pushback_freq);
            at_kick_diag = at_kick_diag || (at_kick_l2_amp3 > 0);
            state.AllowLeftTriggerFFB = 1;
            synth_owned_l2 = true;
        } else if (r2t_wants_l2) {
            // Only trust the rumble bytes when the host says they're valid — apps send
            // reports for other purposes (LED, volume) where these offsets carry stale
            // data (phantom trigger buzz otherwise).
            write_vibration(state.LeftTriggerFFB,
                update.EnableRumbleEmulation ? update.RumbleEmulationLeft : 0, g_l2_pos);
            state.AllowLeftTriggerFFB = 1;
            synth_owned_l2 = true;
        } else {
            release_left(false);     // feature off / not wanted: release once, then idle
        }

        // -- RIGHT trigger --
        // Priority: resistance WINS over vibration while engaged, so "vibration on
        // both + L2-gated resistance" gives the natural combo — triggers buzz with
        // rumble normally, R2 stiffens the moment you aim, vibration resumes on release.
        if (game_owns_r2) {
            release_right(true);
        } else if (at_wants_r2) {
            // -- Stage 2: push-back kick (recoil) --
            // A static change in Feedback strength is barely perceptible under a
            // finger that already holds the trigger, so the kick is delivered as
            // a LOW-FREQUENCY VIBRATION BURST instead: while the vibration
            // envelope is hot, the trigger slot temporarily switches from Feedback
            // (resistance) to Vibration (recoil thump) and drops back to
            // resistance as the burst fades. Hysteresis (on >= 32, off < 16) plus
            // a 45 ms minimum burst stop mode chatter around the threshold.
            // at_pushback == 0 leaves Stage 1 byte-identical. The envelope and
            // burst state are computed once per cycle above (shared with L2).
            write_at(state.RightTriggerFFB, cfg.at_strength, cfg.at_start_pos,
                     at_kick_r2_amp3, cfg.at_kick_style, cfg.at_pushback_freq);
            at_kick_diag = at_kick_diag || (at_kick_r2_amp3 > 0);
            state.AllowRightTriggerFFB = 1;
            synth_owned_r2 = true;
        } else if (r2t_wants_r2) {
            write_vibration(state.RightTriggerFFB,
                update.EnableRumbleEmulation ? update.RumbleEmulationRight : 0, g_r2_pos);
            state.AllowRightTriggerFFB = 1;
            synth_owned_r2 = true;
        } else {
            release_right(false);
        }

        g_diag_synth = (uint8_t)((game_owns_l2 ? 1 : 0) | (game_owns_r2 ? 2 : 0) |
                                 (synth_owned_l2 ? 4 : 0) | (synth_owned_r2 ? 8 : 0) |
                                 (at_engaged ? 16 : 0) | (at_kick_diag ? 32 : 0));
    }

    copy_if_allowed(
        update.AllowMotorPowerLevel,
        kMotorPowerLevelOffset,
        sizeof(uint8_t)
    );
    copy_if_allowed(
        !get_config().lock_volume && update.AllowAudioControl2,
        kAudioControl2Offset,
        sizeof(uint8_t)
    );
    if (!get_config().lock_volume && update.AllowAudioControl2) {
        get_config().speaker_gain = update.SpeakerCompPreGain;
    }
    copy_if_allowed(
        update.AllowHapticLowPassFilter,
        kHapticLowPassFilterOffset,
        sizeof(uint8_t)
    );

    copy_if_allowed(
        update.AllowColorLightFadeAnimation,
        offsetof(SetStateData, LightFadeAnimation),
        sizeof(update.LightFadeAnimation)
    );
    copy_if_allowed(
        update.AllowLightBrightnessChange,
        offsetof(SetStateData, LightBrightness),
        sizeof(update.LightBrightness)
    );
    copy_if_allowed(
        update.AllowPlayerIndicators,
        kPlayerIndicatorsOffset,
        sizeof(uint8_t)
    );
    copy_if_allowed(
        update.AllowLedColor,
        offsetof(SetStateData, LedRed),
        sizeof(update.LedRed) * 3
    );

    bool changed = false;
    if (memcmp(&old_state, &state, 32) != 0) {
        changed = true;
    } else if (memcmp(reinterpret_cast<const uint8_t *>(&old_state) + 36,
                      reinterpret_cast<const uint8_t *>(&state) + 36,
                      sizeof(SetStateData) - 36) != 0) {
        changed = true;
    }
    return changed;
}

void set_volume(const uint8_t value) {
    // printf("[StateMgr] SetVolume: %u\n",value);
    if (get_config().sync_spk_headset_volume) {
        state.VolumeSpeaker = value;
        get_config().speaker_volume = value;
    }
    state.VolumeHeadphones = value;
    get_config().headset_volume = value;
}

void set_volume(const uint8_t speaker, const uint8_t headset) {
    state.VolumeSpeaker = speaker;
    state.VolumeHeadphones = headset;
}

void set_gain(const uint8_t value) {
    state.SpeakerCompPreGain = value;
    state.BeamformingEnable = true;
}

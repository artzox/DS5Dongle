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
// Effect-capture history (monitor): last N distinct trigger effects each trigger
// received from the game, newest at [0]. Read by the portal effect monitor.
static constexpr int HIST_DEPTH = 6;
static uint8_t g_eff_hist_r2[HIST_DEPTH][11] = {};
static uint8_t g_eff_hist_l2[HIST_DEPTH][11] = {};
// Force one state send even if bytes are unchanged (bypasses change-suppression
// for the weapon-break re-arm: a fresh identical re-send re-arms the break
// without the Off->wall drop that caused the perceptible click).
static bool g_ce_force_send = false;
// -- Timeline recorder (v1.14.0): when armed, records EVERY trigger-FFB change
// (including Off) per trigger WITH the duration the previous state was held.
// The dedup'd history above loses all timing; this captures the real rhythm
// (e.g. GoW's switch-then-HOLD) so replay needs no rate dial-in.
static constexpr int TL_MAX = 16;
static bool     g_tl_armed = false;
static uint8_t  g_tl_eff[2][TL_MAX][11] = {};
static uint16_t g_tl_dt[2][TL_MAX] = {};
static uint8_t  g_tl_count[2] = {};
static uint32_t g_tl_last_change[2] = {};
void tl_set_armed(bool on) {
    if (on) {
        g_tl_count[0] = g_tl_count[1] = 0;
        g_tl_last_change[0] = g_tl_last_change[1] = 0;
        g_tl_armed = true;
    } else {
        // finalize the open (last) entry's duration on stop
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        for (int t = 0; t < 2; ++t)
            if (g_tl_count[t] > 0 && g_tl_last_change[t] != 0) {
                uint32_t d = now - g_tl_last_change[t];
                g_tl_dt[t][g_tl_count[t] - 1] = (d > 0xFFFF) ? 0xFFFF : (uint16_t)d;
            }
        g_tl_armed = false;
    }
}
bool tl_read(uint8_t trig, uint8_t idx, uint8_t out[11], uint16_t *dt, uint8_t *count) {
    if (trig > 1) return false;
    *count = g_tl_count[trig];
    if (idx >= g_tl_count[trig]) return false;
    memcpy(out, g_tl_eff[trig][idx], 11);
    *dt = g_tl_dt[trig][idx];
    return true;
}
bool get_effect_history(uint8_t trig, uint8_t slot, uint8_t out[11]) {
    if (slot >= HIST_DEPTH) return false;
    const uint8_t (*h)[11] = (trig == 0) ? g_eff_hist_r2 : g_eff_hist_l2;
    memcpy(out, h[slot], 11);
    return true;
}

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
        // Effect-capture history: store the last few DISTINCT trigger effects each
        // trigger received (dedup consecutive identical), newest at [0].
        {
            auto push_hist = [](uint8_t (*hist)[11], const uint8_t *eff) {
                if (memcmp(hist[0], eff, 11) == 0) return;
                for (int k = HIST_DEPTH - 1; k > 0; k--) memcpy(hist[k], hist[k-1], 11);
                memcpy(hist[0], eff, 11);
            };
            if (game_r2_now) push_hist(g_eff_hist_r2, update.RightTriggerFFB);
            if (game_l2_now) push_hist(g_eff_hist_l2, update.LeftTriggerFFB);
            // Timeline recorder: while armed, log EVERY change (incl. Off) with the
            // held-duration of the previous state — timing is the whole point here.
            if (g_tl_armed) {
                const uint32_t tnow = to_ms_since_boot(get_absolute_time());
                auto tl_push = [&](int t, const uint8_t *eff, bool allow) {
                    if (!allow) return;
                    const uint8_t n = g_tl_count[t];
                    if (n > 0 && memcmp(g_tl_eff[t][n-1], eff, 11) == 0) return; // unchanged
                    if (n > 0) {
                        uint32_t d = tnow - g_tl_last_change[t];
                        g_tl_dt[t][n-1] = (d > 0xFFFF) ? 0xFFFF : (uint16_t)d;
                    }
                    if (n < TL_MAX) {
                        memcpy(g_tl_eff[t][n], eff, 11);
                        g_tl_dt[t][n] = 0;               // open until next change/stop
                        g_tl_count[t] = n + 1;
                        g_tl_last_change[t] = tnow;
                    }
                };
                tl_push(0, update.RightTriggerFFB, update.AllowRightTriggerFFB);
                tl_push(1, update.LeftTriggerFFB,  update.AllowLeftTriggerFFB);
            }
        }
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
        extern volatile uint8_t g_l2_pos, g_r2_pos, g_l1_btn, g_r1_btn;

        // -- Custom captured-effect action (v1.14.0): plays raw captured effect
        // states with NO fidelity loss. While the trigger is engaged (per condition
        // + threshold, dead-zone-compatible), write state A or B directly. For a
        // 2-state action, cycle A<->B at the configured rate (reproduces the game's
        // own rapid alternation - the pump/pull-through feel). on-press/on-release
        // fire a one-shot (a brief window). Positions ride inside each state's bytes.
        static float ce_phase_r2 = 0.0f, ce_phase_l2 = 0.0f;
        static uint8_t ce_last_r2_pos = 0, ce_last_l2_pos = 0;
        static uint32_t ce_oneshot_r2_until = 0, ce_oneshot_l2_until = 0;
        // Weapon-break re-arm: a 0x25 break is CONSUMED when pushed through, so to
        // feel it on the next press it must be re-armed with a FRESH re-send. Identical
        // bytes are change-suppressed (won't re-arm), so when the trigger returns toward
        // rest we emit ONE cycle of Off (0x05) to break the suppression; the next cycle's
        // break write is then seen as new and re-arms. Only for break-type states.
        static bool ce_rearm_pulse_r2 = false, ce_rearm_pulse_l2 = false;
        static uint8_t  ce_tl_idx_r2 = 0, ce_tl_idx_l2 = 0;
        static uint32_t ce_tl_next_r2 = 0, ce_tl_next_l2 = 0;
        // Two-wall sequencer stage: 0 = lower wall armed, 1 = upper wall armed.
        static uint8_t  ce_seq_stage_r2 = 0, ce_seq_stage_l2 = 0;
        static bool ce_broke_r2 = false, ce_broke_l2 = false;
        bool ce_active_r2 = false, ce_active_l2 = false;
        const uint8_t *ce_write_r2 = nullptr, *ce_write_l2 = nullptr;
        const uint8_t *ce_seq_r2 = nullptr, *ce_seq_l2 = nullptr;
        {
            const uint32_t now_ms = g_last_host_ms;
            auto zpos = [](uint8_t z) -> uint8_t { return (uint8_t)(((uint16_t)z * 51u) / 2u); };
            // Pick the state to write for a given trigger's action this cycle.
            // Pick the state to write this cycle. TWO modes, per assignment:
            //  - TIMELINE (any dt > 0): hold each state for its recorded duration,
            //    advance, loop. Reproduces asymmetric rhythms (switch-then-HOLD)
            //    verbatim - no rate dial-in.
            //  - RATE (all dt == 0): A<->B toggle at the rate slider (states 0/1) -
            //    the mode whose ~25Hz blend "stacks" mechanical pairs nicely.
            auto pick_state = [&](const uint8_t (*states)[11], const uint16_t *dt,
                                  uint8_t count, uint8_t rate, float &phase,
                                  uint8_t &tl_idx, uint32_t &tl_next) -> const uint8_t* {
                if (count < 2) return states[0];
                bool timeline = false;
                for (uint8_t i = 0; i < count; ++i) if (dt[i] > 0) { timeline = true; break; }
                const uint32_t nowm = to_ms_since_boot(get_absolute_time());
                if (timeline) {
                    if (tl_next == 0) { tl_idx = 0; tl_next = nowm + (dt[0] ? dt[0] : 1); }
                    if (nowm >= tl_next) {
                        tl_idx = (uint8_t)((tl_idx + 1) % count);
                        tl_next = nowm + (dt[tl_idx] ? dt[tl_idx] : 1);
                    }
                    return states[tl_idx];
                }
                // rate mode: toggle states 0/1 each half-cycle. rate 1..100 -> ~2..40 Hz.
                float hz = 2.0f + (rate / 100.0f) * 38.0f;
                phase += hz * 0.004f; // ~4ms/cycle
                if (phase >= 1.0f) phase -= 1.0f;
                return (phase < 0.5f) ? states[0] : states[1];
            };
            // Decode a wall state's zone span (bytes 1-2 bitmap) -> lowest/highest set zone.
            auto wall_zones = [](const uint8_t *st, uint8_t &zlo, uint8_t &zhi) {
                const uint16_t zb = (uint16_t)st[1] | ((uint16_t)(st[2] & 0x03) << 8);
                zlo = 2; zhi = 8;
                if (zb) {
                    zlo = 0; while (zlo < 10 && !(zb & (1u << zlo))) zlo++;
                    zhi = 9; while (zhi > 0 && !(zb & (1u << zhi))) zhi--;
                }
            };
            // N-STAGE POSITIONAL SEQUENCER (2..5 MECHANICAL states - walls AND
            // resistances - with dt=0 and distinct start zones): sort by start zone;
            // arm each next stage just before the pull reaches its region (start-12,
            // so the armed region is AHEAD of the finger = click-free, and the
            // previous wall keeps its full span); reset to stage 0 when the trigger
            // returns below the re-arm zone. Reproduces e.g. R&C's three-stage
            // wall -> wall -> end-resistance, or a resistance BEFORE a wall.
            // Returns the state to write, or nullptr if this set isn't sequencable
            // (any vibration state, timeline durations, or ambiguous zones).
            auto run_sequencer = [&](const uint8_t (*states)[11], const uint16_t *dt,
                                     uint8_t count, uint8_t pos, uint8_t rearm_pos,
                                     uint8_t &stage, bool &rearm_pulse) -> const uint8_t* {
                (void)dt;
                if (count < 2 || count > 5) return nullptr;
                uint8_t order[5]; uint8_t zlo[5];
                for (uint8_t i = 0; i < count; ++i) {
                    const uint8_t t = states[i][0];
                    const bool mech = (t == 0x21 || t == 0x01 || t == 0x25 || t == 0x23);
                    if (!mech) return nullptr;
                    // NOTE: recorded durations are IGNORED for mechanical sets - they are
                    // game-state/performance timing, not effect structure (taxonomy). This
                    // also rescues timeline-saved files: loading one with durations used to
                    // drop the set into the timeline stepper, which swapped wall/resistance
                    // on multi-second timers - felt as "works sometimes" plus phantom clicks
                    // at rest as states toggled under the finger. Mechanical => positional.
                    uint8_t hi_;
                    wall_zones(states[i], zlo[i], hi_); (void)hi_;
                    order[i] = i;
                }
                for (uint8_t a = 1; a < count; ++a)
                    for (uint8_t b = a; b > 0 && zlo[order[b]] < zlo[order[b-1]]; --b) {
                        uint8_t tmp = order[b]; order[b] = order[b-1]; order[b-1] = tmp;
                    }
                for (uint8_t a = 1; a < count; ++a)
                    if (zlo[order[a]] == zlo[order[a-1]]) return nullptr;
                if (stage >= count) stage = 0;
                while (stage + 1 < count) {
                    int sw = (int)zpos(zlo[order[stage + 1]]) - 12;
                    if (sw < 1) sw = 1;
                    if (pos >= (uint8_t)sw) { stage++; rearm_pulse = true; }
                    else break;
                }
                if (stage != 0 && pos <= rearm_pos) { stage = 0; rearm_pulse = true; }
                return states[order[stage]];
            };
            // R2
            if (cfg.ce_r2_enable && !game_owns_r2) {
                const uint8_t thr = zpos(cfg.ce_r2_thresh);
                const bool past = g_r2_pos >= thr;
                const bool was_past = ce_last_r2_pos >= thr;
                if (cfg.ce_r2_condition == 0) {              // while held / always-armed
                    // Assert continuously so a re-press finds the wall already in place.
                    // In while-held mode the thresh field is the RE-ARM ZONE: walls
                    // re-arm/reset when the trigger returns to or below it (0 = only at
                    // full release, finger off = click-free; the game re-arms on release).
                    ce_active_r2 = true;
                    const uint8_t rearm_pos = zpos(cfg.ce_r2_thresh);
                    const uint8_t *seq = run_sequencer(cfg.ce_r2_states, cfg.ce_r2_dt,
                                                       cfg.ce_r2_state_count, g_r2_pos,
                                                       rearm_pos, ce_seq_stage_r2,
                                                       ce_rearm_pulse_r2);
                    if (seq) {
                        ce_seq_r2 = seq;
                    } else if (cfg.ce_r2_state_count < 2 &&
                               (cfg.ce_r2_states[0][0] == 0x25 || cfg.ce_r2_states[0][0] == 0x23)) {
                        // Single wall: break-through at its END zone; re-arm with a fresh
                        // identical send when the trigger returns to the re-arm zone.
                        uint8_t zlo0, zhi0;
                        wall_zones(cfg.ce_r2_states[0], zlo0, zhi0); (void)zlo0;
                        if (g_r2_pos >= zpos(zhi0)) ce_broke_r2 = true;
                        if (ce_broke_r2 && g_r2_pos <= rearm_pos) {
                            ce_rearm_pulse_r2 = true;
                            ce_broke_r2 = false;
                        }
                    }
                } else if (cfg.ce_r2_condition == 1) {       // on press (rising cross)
                    if (past && !was_past) ce_oneshot_r2_until = now_ms + 250;
                    if (now_ms < ce_oneshot_r2_until) ce_active_r2 = true;
                } else {                                     // on release (falling cross)
                    if (!past && was_past) ce_oneshot_r2_until = now_ms + 250;
                    if (now_ms < ce_oneshot_r2_until) ce_active_r2 = true;
                }
                if (ce_active_r2)
                    ce_write_r2 = pick_state(cfg.ce_r2_states, cfg.ce_r2_dt,
                                             cfg.ce_r2_state_count, cfg.ce_r2_rate,
                                             ce_phase_r2, ce_tl_idx_r2, ce_tl_next_r2);
                if (ce_seq_r2) ce_write_r2 = ce_seq_r2;   // sequencer overrides the picker
            }
            if (!ce_active_r2) { ce_tl_next_r2 = 0; ce_tl_idx_r2 = 0; }
            ce_last_r2_pos = g_r2_pos;
            // L2
            if (cfg.ce_l2_enable && !game_owns_l2) {
                const uint8_t thr = zpos(cfg.ce_l2_thresh);
                const bool past = g_l2_pos >= thr;
                const bool was_past = ce_last_l2_pos >= thr;
                if (cfg.ce_l2_condition == 0) {              // while held / always-armed
                    ce_active_l2 = true;                     // (see R2 note; thresh = re-arm zone)
                    const uint8_t rearm_pos = zpos(cfg.ce_l2_thresh);
                    const uint8_t *seq = run_sequencer(cfg.ce_l2_states, cfg.ce_l2_dt,
                                                       cfg.ce_l2_state_count, g_l2_pos,
                                                       rearm_pos, ce_seq_stage_l2,
                                                       ce_rearm_pulse_l2);
                    if (seq) {
                        ce_seq_l2 = seq;
                    } else if (cfg.ce_l2_state_count < 2 &&
                               (cfg.ce_l2_states[0][0] == 0x25 || cfg.ce_l2_states[0][0] == 0x23)) {
                        uint8_t zlo0, zhi0;
                        wall_zones(cfg.ce_l2_states[0], zlo0, zhi0); (void)zlo0;
                        if (g_l2_pos >= zpos(zhi0)) ce_broke_l2 = true;
                        if (ce_broke_l2 && g_l2_pos <= rearm_pos) { ce_rearm_pulse_l2 = true; ce_broke_l2 = false; }
                    }
                } else if (cfg.ce_l2_condition == 1) {
                    if (past && !was_past) ce_oneshot_l2_until = now_ms + 250;
                    if (now_ms < ce_oneshot_l2_until) ce_active_l2 = true;
                } else {
                    if (!past && was_past) ce_oneshot_l2_until = now_ms + 250;
                    if (now_ms < ce_oneshot_l2_until) ce_active_l2 = true;
                }
                if (ce_active_l2)
                    ce_write_l2 = pick_state(cfg.ce_l2_states, cfg.ce_l2_dt,
                                             cfg.ce_l2_state_count, cfg.ce_l2_rate,
                                             ce_phase_l2, ce_tl_idx_l2, ce_tl_next_l2);
                if (ce_seq_l2) ce_write_l2 = ce_seq_l2;
            }
            if (!ce_active_l2) { ce_tl_next_l2 = 0; ce_tl_idx_l2 = 0; }
            ce_last_l2_pos = g_l2_pos;
        }
        // gate_pos = analog arming source (opposite trigger) for mode 1;
        // shoulder = digital arming source (opposite shoulder) for mode 3.
        auto at_engage = [&](uint8_t mode, uint8_t gate_pos, uint8_t shoulder,
                             uint8_t thr, uint8_t strength, bool &engaged) -> bool {
            if (mode == 0 || strength == 0) { engaged = false; return false; }
            if (mode == 2) { engaged = true; return true; }     // always on
            if (mode == 3) { engaged = (shoulder != 0); return engaged; } // shoulder-gated (digital)
            const uint8_t rel = (thr > 15) ? (uint8_t)(thr - 15) : 1; // mode 1: opposite-trigger gated + hysteresis
            if (!engaged && gate_pos >= thr)     engaged = true;
            else if (engaged && gate_pos < rel)  engaged = false;
            return engaged;
        };
        // R2 armed by L2 (analog, mode 1) or L1 (digital, mode 3); L2 by R2 or R1.
        // A SHAPED trigger is "on" if EITHER strength is nonzero: ramp A=0->B is
        // "free at rest, building resistance", not disabled; detent A=0 is a pure
        // bump. Only Constant (shape 0) keeps the strength==0 = off convention.
        const uint8_t at_eff_r2 = (cfg.at_shape == 0 || cfg.at_shape == 3) ? cfg.at_strength
            : ((cfg.at_strength > cfg.at_strength_b) ? cfg.at_strength : cfg.at_strength_b);
        const uint8_t at_eff_l2 = (cfg.at_l2_shape == 0 || cfg.at_l2_shape == 3) ? cfg.at_l2_strength
            : ((cfg.at_l2_strength > cfg.at_l2_strength_b) ? cfg.at_l2_strength : cfg.at_l2_strength_b);
        const bool at_wants_r2 = at_engage(cfg.at_mode > 3 ? 0 : cfg.at_mode, g_l2_pos, g_l1_btn, cfg.at_threshold, at_eff_r2, at_engaged);
        const bool at_wants_l2 = at_engage(cfg.at_l2_mode > 3 ? 0 : cfg.at_l2_mode, g_r2_pos, g_r1_btn, cfg.at_l2_threshold, at_eff_l2, at_engaged_l2);

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
                            uint8_t kick_amp3, uint8_t kick_style, uint8_t kick_freq,
                            uint8_t shape, uint8_t strength_b, uint8_t detent_pos) {
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
            } else if (shape == 3) {
                // Weapon break (0x25): a rigid wall from the start position to the
                // break point, then a hardware-sharp SNAP-THROUGH release - the
                // classic semi-auto shot break (tension -> clean give -> free).
                // Distinct from the two-stage detent (a bump with force on both
                // sides): here the resistance ENDS at the break. Field reuse:
                // start position = wall start (hw range 2-7), Detent zone = break
                // point (> start, hw range 3-8), Strength A = wall force.
                // Pairs naturally with the Activation dead zone set at the break
                // zone: the shot then registers exactly at the snap.
                ffb[0] = 0x25;
                uint8_t ws = (start_pos > 9) ? 2 : start_pos;
                if (ws < 2) ws = 2; if (ws > 7) ws = 7;
                uint8_t we = detent_pos;
                if (we <= ws) we = (uint8_t)(ws + 1);
                if (we > 8) we = 8;
                const uint16_t zpair = (uint16_t)((1u << ws) | (1u << we));
                ffb[1] = (uint8_t)(zpair & 0xFF);
                ffb[2] = (uint8_t)((zpair >> 8) & 0xFF);
                uint8_t w = (uint8_t)(((uint16_t)strength * 7u + 50u) / 100u);
                ffb[3] = (w > 7) ? 7 : w;
            } else {
                // Resistance with per-zone SHAPES. The 0x21 Feedback effect gives
                // each of the 10 travel zones its own 3-bit strength, evaluated
                // against trigger position by the CONTROLLER HARDWARE - so ramps
                // and detents cost nothing at runtime and feel perfectly smooth.
                ffb[0] = 0x21;
                const uint8_t start = (start_pos > 9) ? 0 : start_pos;
                uint16_t zones = 0;
                uint8_t zs[10] = {0};
                auto wire100 = [](uint16_t s100) -> uint8_t {
                    uint8_t w = (uint8_t)((s100 * 7u + 50u) / 100u);
                    return (w > 7) ? 7 : w;
                };
                for (int z = start; z <= 9; ++z) {
                    uint16_t s100;
                    switch (shape) {
                        default: // 0 = Constant (pre-1.7.0 behavior)
                            s100 = strength;
                            break;
                        case 1: { // Ramp: linear A -> B across start..9
                            const int span = 9 - start;
                            s100 = (span == 0) ? strength_b
                                : (uint16_t)(strength + ((int)strength_b - (int)strength) * (z - start) / span);
                            break;
                        }
                        case 2: // Two-stage detent: base A, WALL of B at detent zone
                            s100 = (z == detent_pos) ? strength_b : strength;
                            break;
                    }
                    // Strength 0 in a shaped trigger EXCLUDES the zone from the
                    // bitmap: the 3-bit wire value is force level 1..8, so the only
                    // true zero is leaving the zone out. Ramp A=0 = genuinely free
                    // at rest; detent A=0 = pure bump with free travel elsewhere.
                    if (shape != 0 && s100 == 0) continue;
                    zones |= (1u << z);
                    zs[z] = wire100(s100);
                }
                ffb[1] = (uint8_t)(zones & 0xFF);
                ffb[2] = (uint8_t)((zones >> 8) & 0x03);
                uint32_t bits = 0;
                for (int z = 0; z <= 9; ++z)
                    if (zones & (1u << z)) bits |= (uint32_t)(zs[z] & 0x7u) << (3 * z);
                ffb[3] = (uint8_t)(bits & 0xFF);
                ffb[4] = (uint8_t)((bits >> 8) & 0xFF);
                ffb[5] = (uint8_t)((bits >> 16) & 0xFF);
                ffb[6] = (uint8_t)((bits >> 24) & 0xFF);
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
        } else if (ce_active_l2 && ce_write_l2) {
            // Custom captured effect owns the trigger: write the raw state verbatim.
            memcpy(state.LeftTriggerFFB, ce_write_l2, 11);
            if (ce_rearm_pulse_l2) {
                g_ce_force_send = true;   // fresh identical re-send = gentle re-arm
                ce_rearm_pulse_l2 = false;
            }
            state.AllowLeftTriggerFFB = 1;
            synth_owned_l2 = true;
        } else if (cfg.ce_l2_enable) {
            // Enabled but idle: own the trigger exclusively, don't leak slider/r2t.
            release_left(false);
        } else if (at_wants_l2) {
            write_at(state.LeftTriggerFFB, cfg.at_l2_strength, cfg.at_l2_start_pos,
                     at_kick_l2_amp3, cfg.at_l2_kick_style, cfg.at_l2_pushback_freq,
                     cfg.at_l2_shape, cfg.at_l2_strength_b, cfg.at_l2_detent_pos);
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
        } else if (ce_active_r2 && ce_write_r2) {
            memcpy(state.RightTriggerFFB, ce_write_r2, 11);
            if (ce_rearm_pulse_r2) {
                // Re-arm: identical bytes, but force the send so the controller
                // receives a fresh effect write (re-arms the break) with NO Off
                // drop in between - avoids the click of wall-drop-then-restore.
                g_ce_force_send = true;
                ce_rearm_pulse_r2 = false;
            }
            state.AllowRightTriggerFFB = 1;
            synth_owned_r2 = true;
        } else if (cfg.ce_r2_enable) {
            // Custom effect is ENABLED but not currently engaged (below threshold /
            // between presses): it owns this trigger exclusively, so DON'T fall
            // through to the slider adaptive-trigger (at_wants, incl. its kick/bow)
            // or r2t - those would leak a wall/bow on the next press before the
            // custom effect re-engages. Release the trigger to a clean/off state.
            release_right(false);
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
                     at_kick_r2_amp3, cfg.at_kick_style, cfg.at_pushback_freq,
                     cfg.at_shape, cfg.at_strength_b, cfg.at_detent_pos);
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
    if (g_ce_force_send) {
        changed = true;                 // re-arm: send identical bytes fresh
        g_ce_force_send = false;
    } else if (memcmp(&old_state, &state, 32) != 0) {
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

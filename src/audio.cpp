//
// Created by awalol on 2026/3/5.
//
// Audio auto-haptics modifications (c) 2026 artzox — MIT License.
// The audio-derived auto-haptics here were inspired by loteran's earlier
// auto-haptics work on the DS5Dongle; the DSP was rewritten, but loteran's
// state_set-in-RAM insight made stock-clock actuation possible. With thanks.
//

/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "audio.h"
#include "bt.h"
#include "resample.h"
#include "tusb.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "opus.h"
#include "utils.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#include "pico/flash.h"
#include "pico/util/queue.h"
#include "config.h"
#include "state_mgr.h"
#include "usb.h"

#define INPUT_CHANNELS    4
#define OUTPUT_CHANNELS   2
#define SAMPLE_SIZE       64
#define REPORT_SIZE       398
#define REPORT_ID         0x36
// #define VOLUME_GAIN       2
// #define BUFFER_LENGTH     48
#define MIC_CHANNELS      1
#define MIC_FRAMES        480
#define MIC_OPUS_SIZE     71   // bytes per opus-encoded mic frame from the DualSense

using std::clamp;
using std::max;

static WDL_Resampler resampler;
static uint8_t reportSeqCounter = 0;
static uint8_t packetCounter = 0;
static bool plug_headset = false;
static bool mic_active = false; // host has opened the mic IN interface (alt != 0)
alignas(8) static uint32_t audio_core1_stack[8192];
queue_t audio_fifo;
queue_t mic_fifo;
queue_t mic_decode_fifo;
// Diagnostic: live channel detection, read via config field 0x20.
volatile uint16_t g_diag_bytes_read = 0;
volatile uint16_t g_diag_ch01_peak = 0; // peak |sample| ch0-1 (DSP input)
volatile uint16_t g_diag_ch23_peak = 0; // peak |sample| ch2-3 (native actuators)
volatile uint8_t  g_diag_actual_ch  = 0;
// Incoming DS4Windows rumble-emulation motor values (0-255), blended into the
// actuator signal in Mix mode when auto-haptics is on.
volatile uint8_t g_rumble_l = 0;
volatile uint8_t g_rumble_r = 0;
// Latest L2 (left trigger) analog position from the controller's input report,
// used by the L2-gated R2 adaptive-trigger feature.
volatile uint8_t g_l2_pos = 0;
volatile uint8_t g_r2_pos = 0; // R2 (right trigger) analog position, for r2t on-press gating
volatile uint8_t g_l1_btn = 0; // L1 shoulder (digital), for shoulder-gated adaptive triggers
volatile uint8_t g_r1_btn = 0; // R1 shoulder (digital)
static uint8_t opus_buf[200];
critical_section_t opus_cs;

struct audio_raw_element {
    float data[512 * 2];
};
struct mic_element {
    uint8_t data[MIC_OPUS_SIZE];
};
struct mic_decode_element {
    int16_t data[MIC_FRAMES * MIC_CHANNELS];
    uint16_t len;
};

void set_headset(bool state) {
    plug_headset = state;
}

// Called from tud_audio_set_itf_cb when the host opens/closes the mic IN
// interface. Gates controller-mic streaming so it only runs while recording.
void set_mic_active(bool active) {
    mic_active = active;
    update_mic_status();
}

bool audio_mic_active() {
    return mic_active;
}

void update_mic_status() {
    uint8_t pkt[142]{};
    pkt[0] = 0x32;
    pkt[1] = reportSeqCounter << 4;
    reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
    pkt[2] = 0x11 | 0 << 6 | 1 << 7;
    pkt[3] = 7;
    pkt[4] = (mic_active && !get_config().disable_mic) ? 0b11111111 : 0b11111110;
    const auto buf_len = get_config().audio_buffer_length;
    pkt[5] = buf_len;
    pkt[6] = buf_len;
    pkt[7] = buf_len;
    pkt[8] = buf_len;
    pkt[9] = buf_len;
    pkt[10] = packetCounter++;
    bt_write(pkt,sizeof(pkt));
}

void __not_in_flash_func(audio_loop)() {
    // Mic playback: drain decoded mic PCM into the USB IN endpoint
    static mic_decode_element mic_pb{};
    if (queue_try_remove(&mic_decode_fifo, &mic_pb)) {
        // The controller mic is mono, but the USB descriptor presents a 2-channel
        // mic (matching the real DS5) so Windows doesn't conflict with its cached
        // DS5 audio format. Duplicate each mono sample into L and R.
        static int16_t mic_stereo[MIC_FRAMES * 2];
        const int mono_samples = mic_pb.len / 2;
        for (int i = 0; i < mono_samples; i++) {
            mic_stereo[2 * i] = mic_pb.data[i];
            mic_stereo[2 * i + 1] = mic_pb.data[i];
        }
        const uint16_t stereo_len = (uint16_t) (mono_samples * 2 * 2);
        uint16_t written = tud_audio_write(mic_stereo, stereo_len);
        if (written != stereo_len) {
            // Gated behind ENABLE_VERBOSE: when the host has not opened the mic
            // interface (the common case -- most games never do) tud_audio_write
            // short-writes every frame, so an unconditional log would flood
            // core0's hot path with the newlib formatting chain.
#if ENABLE_VERBOSE
            printf("[Audio] Warning: USB mic FIFO wrote %u/%u bytes\n", written, stereo_len);
#endif
        }
    }

    // 1. 读取 USB 音频数据
    if (!tud_audio_available()) return;

    int16_t raw[192];
    uint32_t bytes_read = tud_audio_read(raw, sizeof(raw)); // 每次读入 384 bytes
    // Detect channel count from packet size (loteran's approach): 4ch@48kHz ≈ 384
    // bytes/ms (Linux/PipeWire native haptic stream), 2ch@48kHz ≈ 192 bytes/ms
    // (Windows / DS4Windows / Stereo Mix — no dedicated haptic channels).
    const int actual_ch = (bytes_read > 250) ? 4 : 2;
    // Diagnostic: expose the live detection so the portal can show what's
    // actually happening (esp. under DS4Windows).
    extern volatile uint16_t g_diag_bytes_read;
    extern volatile uint8_t  g_diag_actual_ch;
    g_diag_bytes_read = (uint16_t)bytes_read;
    g_diag_actual_ch  = (uint8_t)actual_ch;
    int frames = bytes_read / (actual_ch * (int)sizeof(int16_t));
    if (frames == 0) {
        return;
    }
    // Diagnostic: peak |sample| on channels 0-1 (the DSP's input for auto-haptics
    // and effect-leak) and on channels 2-3 (native actuators). Exposed via fields
    // 0x37 (ch0-1) and 0x38 (ch2-3) so the portal can show whether real signal is
    // arriving on the DSP-source pair — distinguishes "loopback delivering silence
    // on 0-1" from "signal present but gated by config/mute".
    {
        extern volatile uint16_t g_diag_ch01_peak, g_diag_ch23_peak;
        int p01 = 0, p23 = 0;
        for (int i = 0; i < frames; ++i) {
            int a0 = raw[i * actual_ch];     if (a0 < 0) a0 = -a0;
            int a1 = raw[i * actual_ch + 1]; if (a1 < 0) a1 = -a1;
            if (a0 > p01) p01 = a0; if (a1 > p01) p01 = a1;
            if (actual_ch == 4) {
                int a2 = raw[i * actual_ch + 2]; if (a2 < 0) a2 = -a2;
                int a3 = raw[i * actual_ch + 3]; if (a3 < 0) a3 = -a3;
                if (a2 > p23) p23 = a2; if (a3 > p23) p23 = a3;
            }
        }
        g_diag_ch01_peak = (uint16_t)p01;
        g_diag_ch23_peak = (uint16_t)p23;
    }

    static float audio_buf[512 * 2];
    static uint audio_buf_pos = 0;
    // 2. 从4ch中提取ch3/ch4，转换为float输入重采样器
    WDL_ResampleSample *in_buf;
    int nframes = resampler.ResamplePrepare(frames, OUTPUT_CHANNELS, &in_buf);

    // const float audio_gain = mute[0] ? 0.0f : powf(10.0f, get_config().speaker_volume / 20.0f);
    const float haptics_gain = get_config().haptics_gain;
    const uint8_t auto_mode = get_config().auto_haptics_enable;
    const uint8_t aa_mode = get_config().haptics_aa; // native-haptics smoothing (1=off,2=light,3=strong)
    // Speaker-data mute: silence the speaker AUDIO CONTENT (not the volume) when
    // Windows mutes, or via auto-mute. Zeroing the speaker buffer leaves
    // VolumeSpeaker untouched, so the controller still scales HAPTICS normally
    // (it scales haptic output by speaker volume — lowering volume kills haptics).
    extern uint8_t mute[2];
    const bool mute_for_mode =
        (auto_mode == 2 && get_config().auto_mute_replace) ||
        (auto_mode == 1 && get_config().auto_mute_mix);
    const float spk_gain = (mute[0] || mute_for_mode) ? 0.0f : 1.0f;
    // Intensity maps 0-100 to a perceptual gain. The carrier makeup is folded in
    // here (modest, ~2.5x) instead of a fixed 12x that saturated the soft-clip and
    // crushed the whole usable range into the bottom few percent. Now the slider
    // spans weak->strong roughly linearly.
    // Intensity curve: the 90 Hz carrier sits in the actuator's most responsive
    // band, so the response to linear gain is very non-linear — low values already
    // actuate. Use a curve (power 1.5) so the slider has fine control low down AND
    // strong headroom up top. intensity 0..200 -> 0..1, curved, scaled to a usable
    // peak. ~100 = firm, 200 = very strong, low values = subtle.
    const float intensity_n = (get_config().auto_haptics_gain / 200.0f); // 0..1 (100->0.5)
    const float intensity_curved = intensity_n * sqrtf(intensity_n); // ^1.5: gentle low, strong high
    const float auto_gain = (auto_mode > 0)
        ? intensity_curved * 9.0f * haptics_gain : 0.0f;
    // Clamp cutoff defensively so a 0/garbage config value can't zero the filter
    // coefficient (which would silence auto-haptics entirely).
    uint16_t lp_fc = get_config().auto_haptics_lowpass_hz;
    if (lp_fc < 20 || lp_fc > 400) lp_fc = 60;
    static float lp_a = 0.0f; static uint16_t lp_fc_cached = 0;
    if (lp_fc != lp_fc_cached) {
        lp_fc_cached = lp_fc;
        lp_a = 1.0f - expf(-2.0f * (float)M_PI * (float)lp_fc / 48000.0f);
    }
    static float lp_l = 0.0f, lp_r = 0.0f;
    static float lp2_l = 0.0f, lp2_r = 0.0f; // cascade stages for steeper slopes
    static float lp3_l = 0.0f, lp3_r = 0.0f;
    static float lp4_l = 0.0f, lp4_r = 0.0f;
    // Frequency split (v1.5.0): xover 0 = OFF -> the block below is bypassed and
    // the single-band path is byte-identical to previous firmware. When ON, a
    // 12 dB/oct LP at the crossover carves the LOW band out of the input; the
    // HIGH band is the existing filtered signal minus the low band (i.e. the
    // crossover..LP-cutoff range). Each band gets its own envelope, weighted by
    // its gain, then summed into the SAME gate + carrier chain - so the felt
    // character is unchanged, only the per-band contribution is adjustable.
    uint16_t xover_fc = get_config().ah_xover_hz;
    const bool split_on = (auto_mode > 0) && xover_fc >= 30 && xover_fc <= 200;
    static float xo_a = 0.0f; static uint16_t xo_fc_cached = 0;
    if (split_on && xover_fc != xo_fc_cached) {
        xo_fc_cached = xover_fc;
        xo_a = 1.0f - expf(-2.0f * (float)M_PI * (float)xover_fc / 48000.0f);
    }
    static float xo1_l = 0.0f, xo2_l = 0.0f, xo1_r = 0.0f, xo2_r = 0.0f; // split LP stages
    static float envlo_l = 0.0f, envlo_r = 0.0f, envhi_l = 0.0f, envhi_r = 0.0f;
    const float split_lo_g = get_config().ah_low_gain  / 100.0f;
    const float split_hi_g = get_config().ah_high_gain / 100.0f;
    static float lp_h_l = 0.0f, lp_h_r = 0.0f; // LP memory for native haptic ch2/3 (Mix mode)
    static float env_l = 0.0f, env_r = 0.0f;
    constexpr float ENV_ATK = 0.15f;
    // Smoothing: maps 0-100 to envelope release rate. Lower release coefficient =
    // longer decay = smoother/more continuous. smooth=0 -> snappy (0.03),
    // smooth=100 -> very smooth (0.002). Lets you dial the feel manually.
    const uint8_t smooth_cfg = get_config().auto_haptics_smooth > 100 ? 50 : get_config().auto_haptics_smooth;
    const float ENV_REL = 0.030f - (smooth_cfg / 100.0f) * 0.028f;
    // Filter slope: number of cascaded 1-pole stages. 1 = 6 dB/oct, 2 = 12 dB/oct,
    // 4 = 24 dB/oct. Steeper = sharper voice/bass separation. Config stores the
    // dB/oct value (6/12/24); map to pole count.
    const uint8_t slope_db = get_config().auto_haptics_slope;
    const int n_poles = (slope_db >= 24) ? 4 : (slope_db >= 12) ? 2 : 1;
    // Effect leak (transient detection): when auto-mute would fully silence the
    // speaker, only open it briefly during sharp ONSETS — sudden level jumps like
    // impacts, shots, clinks — and keep it closed for sustained sound (dialog,
    // music, ambience). A plain high-pass leaks all sustained high-frequency
    // content (hiss, music treble); transient detection instead reacts to how
    // FAST the level rises, so only percussive effects pass. leak_vol 0 = off.
    const float leak_vol = get_config().effect_leak_volume / 100.0f;
    const bool leak_active = (mute_for_mode && !mute[0] && leak_vol > 0.0f);
    // Detection runs on a high-passed copy so low-frequency dialog onsets don't
    // trigger it. The configured "high-pass" value doubles as the detection band.
    static float hp_l = 0.0f, hp_r = 0.0f;          // high-pass state (for detection input)
    // Output BAND-PASS window (v1.2.0): the leak output passes through a 12 dB/oct
    // high-pass (low wall — speaker pop protection + dialog-fundamental rejection)
    // followed by a 12 dB/oct low-pass (high wall — kills the treble sizzle/click
    // that made the leak crackly). Only sound INSIDE the window ever leaks, so the
    // window placement IS the selectivity: e.g. 400-3500 Hz passes impacts and
    // effect bodies while rejecting both voice fundamentals and hiss/sparkle.
    static float ohp1_l = 0.0f, ohp2_l = 0.0f, ohp1_r = 0.0f, ohp2_r = 0.0f; // 2-stage HP
    static float olp1_l = 0.0f, olp2_l = 0.0f, olp1_r = 0.0f, olp2_r = 0.0f; // 2-stage LP
    const float ohp_fc = (float)get_config().effect_leak_output_hp_hz;
    const float ohp_a = 1.0f - expf(-2.0f * (float)M_PI * ohp_fc / 48000.0f);
    const float olp_fc = (float)get_config().effect_leak_lp_hz;
    const float olp_a = 1.0f - expf(-2.0f * (float)M_PI * olp_fc / 48000.0f);
    // Window make-up gain (v1.5.2): the band-pass walls overlap, so narrowing the
    // window used to also make the leak QUIETER (a 400-3500 window lost ~16 dB of
    // perceived level vs the pre-window firmware because the controller's tiny
    // speaker is loudest in the 3-8 kHz range the LP removes). Compensate by the
    // window's response at its geometric-center frequency so the volume slider
    // owns loudness and the window owns character. Clamped to x4 (+12 dB).
    static float leak_makeup = 1.0f;
    static uint32_t mk_key = 0;
    const uint32_t mk_now = ((uint32_t)ohp_fc << 16) | (uint32_t)olp_fc;
    if (mk_now != mk_key) {
        mk_key = mk_now;
        const float fc = sqrtf(ohp_fc * olp_fc);           // window center
        const float whp = fc / ohp_fc, wlp = fc / olp_fc;  // normalized freqs
        const float g1 = whp / sqrtf(1.0f + whp * whp);    // one HP pole @ center
        const float g2 = 1.0f / sqrtf(1.0f + wlp * wlp);   // one LP pole @ center
        float g = (g1 * g1) * (g2 * g2);                   // two poles each
        if (g < 0.25f) g = 0.25f;                          // clamp make-up to x4
        leak_makeup = 1.0f / g;
    }
    // Gate hold + hysteresis (v1.2.0): once a transient opens the gate, keep it
    // open a minimum time (hold) and only close when the level falls clearly below
    // the OPEN threshold (hysteresis). Without this the transient test flickers
    // when the envelope hovers at the threshold — the gate chatters open/closed,
    // which is exactly the "missing and poppy" artifact: hits get chopped short
    // (missing) and each re-open clicks (poppy).
    const uint32_t hold_samples = (uint32_t)get_config().effect_leak_hold * 240u; // x5 ms @48kHz
    static bool     gate_open = false;
    static uint32_t hold_left = 0;
    static float env_fast = 0.0f, env_slow = 0.0f;  // fast/slow level envelopes
    static float leak_gain = 0.0f;                   // smoothed open/close gain
    const float hp_fc = (float)get_config().effect_leak_hp_hz;
    const float hp_a = 1.0f - expf(-2.0f * (float)M_PI * hp_fc / 48000.0f);
    // Sensitivity: how much the fast envelope must exceed the slow one to count as
    // a transient. Higher sensitivity (config 0-100) -> lower ratio -> opens more
    // easily (more sound leaks). Maps 0->ratio 3.0 (very selective, only big hits)
    // .. 100->ratio 1.2 (eager, most onsets pass). Default ~50 -> ~2.1.
    const uint8_t sens_cfg = get_config().effect_leak_sensitivity > 100 ? 50 : get_config().effect_leak_sensitivity;
    const float TRANS_RATIO = 3.0f - (sens_cfg / 100.0f) * 1.8f;
    const float ENV_FAST_A  = 0.65f;  // fast envelope attack — quicker detection (less delay)
    const float ENV_SLOW_A  = 0.0020f;// slow envelope (~tracks the ongoing level)
    // Gate ramps: opening too fast creates a click at the onset (crackle); a few
    // ms ramp removes the click while staying responsive. Close SLOWLY so the hit
    // rings out and fades gradually instead of stopping abruptly. The close rate
    // (decay/fade-out length) is configurable: higher decay -> smaller coefficient
    // -> longer, more gradual tail. 0 -> ~0.004 (quick), 100 -> ~0.0004 (very long).
    const uint8_t decay_cfg = get_config().effect_leak_decay > 100 ? 40 : get_config().effect_leak_decay;
    // Attack (responsiveness): how fast the gate opens once a transient is
    // detected. Faster = more immediate hit (less perceived delay) but can add a
    // faint click on the sharpest onsets; slower = smoother but feels delayed.
    // Configurable 0-100: 0 -> 0.03 (smooth), 100 -> 0.40 (near-instant).
    const uint8_t atk_cfg = get_config().effect_leak_attack > 100 ? 50 : get_config().effect_leak_attack;
    const float GATE_OPEN   = 0.03f + (atk_cfg / 100.0f) * 0.37f;
    const float GATE_CLOSE  = 0.004f - (decay_cfg / 100.0f) * 0.0036f; // 0.004 .. 0.0004
    for (int i = 0; i < nframes; i++) {
 #if !DISABLE_SPEAKER_PROC       
        float spk_l_out, spk_r_out;
        if (leak_active) {
            const float in_l = raw[i * actual_ch]     / 32768.0f;
            const float in_r = raw[i * actual_ch + 1] / 32768.0f;
            // High-pass (signal - low-pass) — used ONLY for transient DETECTION,
            // not for output. Outputting the thin treble-only signal is what
            // caused the crackle; we detect on highs but play the full sound.
            hp_l += hp_a * (in_l - hp_l);
            hp_r += hp_a * (in_r - hp_r);
            const float hpf_l = in_l - hp_l;
            const float hpf_r = in_r - hp_r;
            const float mag = 0.5f * ((hpf_l<0?-hpf_l:hpf_l) + (hpf_r<0?-hpf_r:hpf_r));
            // Fast envelope reacts to onsets; slow envelope tracks the baseline.
            env_fast += ((mag > env_fast) ? ENV_FAST_A : ENV_FAST_A*0.25f) * (mag - env_fast);
            env_slow += ENV_SLOW_A * (mag - env_slow);
            // Gate state machine with hysteresis + minimum hold. OPEN on a clear
            // transient; once open, stay open at least `hold_samples`, then close
            // only when the level drops well below the open threshold (0.65x). The
            // gate makes one clean open/close per hit instead of chattering.
            if (!gate_open) {
                if (env_fast > env_slow * TRANS_RATIO + 0.0008f) {
                    gate_open = true;
                    hold_left = hold_samples;
                }
            } else {
                if (hold_left > 0) hold_left--;
                else if (env_fast <= env_slow * (TRANS_RATIO * 0.65f) + 0.0006f)
                    gate_open = false;
            }
            const float target = gate_open ? 1.0f : 0.0f;
            leak_gain += ((target > leak_gain) ? GATE_OPEN : GATE_CLOSE) * (target - leak_gain);
            // Band-pass the output: 12 dB/oct high-pass (two cascaded poles) removes
            // speaker-popping lows and voice fundamentals; 12 dB/oct low-pass removes
            // the treble sizzle that reads as crackle. Only the window leaks.
            ohp1_l += ohp_a * (in_l - ohp1_l);  float bp_l = in_l - ohp1_l;
            ohp2_l += ohp_a * (bp_l - ohp2_l);  bp_l -= ohp2_l;
            ohp1_r += ohp_a * (in_r - ohp1_r);  float bp_r = in_r - ohp1_r;
            ohp2_r += ohp_a * (bp_r - ohp2_r);  bp_r -= ohp2_r;
            olp1_l += olp_a * (bp_l - olp1_l);
            olp2_l += olp_a * (olp1_l - olp2_l);
            olp1_r += olp_a * (bp_r - olp1_r);
            olp2_r += olp_a * (olp1_r - olp2_r);
            spk_l_out = olp2_l * leak_gain * leak_vol * leak_makeup;
            spk_r_out = olp2_r * leak_gain * leak_vol * leak_makeup;
        } else {
            spk_l_out = raw[i * actual_ch]     / 32768.0f * spk_gain;
            spk_r_out = raw[i * actual_ch + 1] / 32768.0f * spk_gain;
        }
        audio_buf[audio_buf_pos++] = spk_l_out;
        audio_buf[audio_buf_pos++] = spk_r_out;
        if (audio_buf_pos == 512 * 2) {
            static audio_raw_element element{};
            memcpy(element.data, audio_buf, 512 * 2 * 4);
            if (queue_is_full(&audio_fifo)) {
                queue_try_remove(&audio_fifo,NULL);
            }
            if (!queue_try_add(&audio_fifo, &element)) {
                printf("[Audio] Warning: audio_fifo add failed\n");
            }
            audio_buf_pos = 0;
        }
#endif
        // Native haptic channels (ch2/3) only exist in 4ch mode. In 2ch mode
        // (Windows/DS4Windows) there are no dedicated haptic channels.
        float h_l = (actual_ch == 4) ? raw[i * 4 + 2] / 32768.0f * haptics_gain : 0.0f;
        float h_r = (actual_ch == 4) ? raw[i * 4 + 3] / 32768.0f * haptics_gain : 0.0f;

        // Auto-haptics runs when the user enables it (mix/replace). Unlike
        // loteran, 2ch input does NOT force it on — so with auto-haptics OFF you
        // still get pure native rumble->DualSense conversion (awalol behavior),
        // even under DS4Windows. Mode is authoritative.
        if (auto_mode > 0) {
            const float spk_l = raw[i * actual_ch    ] / 32768.0f;
            const float spk_r = raw[i * actual_ch + 1] / 32768.0f;
            lp_l += lp_a * (spk_l - lp_l);
            lp_r += lp_a * (spk_r - lp_r);
            // Cascade additional poles for steeper slopes. Each stage adds
            // 6 dB/oct. A single pole leaks male voice fundamentals (85-180 Hz)
            // when the cutoff is near there; more poles reject voice harder.
            float fl = lp_l, fr = lp_r;
            if (n_poles >= 2) {
                lp2_l += lp_a * (lp_l - lp2_l);
                lp2_r += lp_a * (lp_r - lp2_r);
                fl = lp2_l; fr = lp2_r;
                if (n_poles >= 4) {
                    lp3_l += lp_a * (lp2_l - lp3_l);
                    lp3_r += lp_a * (lp2_r - lp3_r);
                    lp4_l += lp_a * (lp3_l - lp4_l);
                    lp4_r += lp_a * (lp3_r - lp4_r);
                    fl = lp4_l; fr = lp4_r;
                }
            }
            const float abs_l = fl < 0.0f ? -fl : fl;
            const float abs_r = fr < 0.0f ? -fr : fr;
            env_l = (abs_l > env_l) ? env_l + ENV_ATK*(abs_l-env_l) : env_l + ENV_REL*(abs_l-env_l);
            env_r = (abs_r > env_r) ? env_r + ENV_ATK*(abs_r-env_r) : env_r + ENV_REL*(abs_r-env_r);
            float use_env_l = env_l, use_env_r = env_r;
            if (split_on) {
                // Low band: 12 dB/oct LP at the crossover on the raw input.
                xo1_l += xo_a * (spk_l - xo1_l); xo2_l += xo_a * (xo1_l - xo2_l);
                xo1_r += xo_a * (spk_r - xo1_r); xo2_r += xo_a * (xo1_r - xo2_r);
                const float lo_l = xo2_l,      lo_r = xo2_r;
                // High band: everything the main LP kept, minus the low band
                // (phase ripple from the subtraction is irrelevant - only the
                // band's ENVELOPE is used, never its waveform).
                const float hi_l = fl - lo_l,  hi_r = fr - lo_r;
                const float alo_l = lo_l < 0 ? -lo_l : lo_l, alo_r = lo_r < 0 ? -lo_r : lo_r;
                const float ahi_l = hi_l < 0 ? -hi_l : hi_l, ahi_r = hi_r < 0 ? -hi_r : hi_r;
                envlo_l = (alo_l > envlo_l) ? envlo_l + ENV_ATK*(alo_l-envlo_l) : envlo_l + ENV_REL*(alo_l-envlo_l);
                envlo_r = (alo_r > envlo_r) ? envlo_r + ENV_ATK*(alo_r-envlo_r) : envlo_r + ENV_REL*(alo_r-envlo_r);
                envhi_l = (ahi_l > envhi_l) ? envhi_l + ENV_ATK*(ahi_l-envhi_l) : envhi_l + ENV_REL*(ahi_l-envhi_l);
                envhi_r = (ahi_r > envhi_r) ? envhi_r + ENV_ATK*(ahi_r-envhi_r) : envhi_r + ENV_REL*(ahi_r-envhi_r);
                use_env_l = envlo_l * split_lo_g + envhi_l * split_hi_g;
                use_env_r = envlo_r * split_lo_g + envhi_r * split_hi_g;
            }
            // Noise gate with SMOOTHED gain: a hard gate flickers on/off when the
            // bass envelope hovers near the threshold, chopping the output (jagged
            // feel). Compute a target gate gain, then slew toward it so the gate
            // opens/closes smoothly instead of switching.
            const uint8_t gate_cfg = get_config().auto_haptics_gate;
            const float GATE_THRESH = (gate_cfg > 100 ? 0 : gate_cfg) * 0.001f; // 0-100 -> 0..0.1
            const float GATE_KNEE   = 0.04f; // wider knee = gentler transition
            static float gsm_l = 0.0f, gsm_r = 0.0f; // smoothed gate gain memory
            // Gate slew: ~5 ms open, ~60 ms close, at 48 kHz frame rate.
            constexpr float GATE_OPEN = 0.20f, GATE_CLOSE = 0.02f;
            float genv_l = use_env_l, genv_r = use_env_r;
            if (GATE_THRESH > 0.0f) {
                float tgt_l = (use_env_l - GATE_THRESH) / GATE_KNEE;
                float tgt_r = (use_env_r - GATE_THRESH) / GATE_KNEE;
                tgt_l = tgt_l < 0.0f ? 0.0f : (tgt_l > 1.0f ? 1.0f : tgt_l);
                tgt_r = tgt_r < 0.0f ? 0.0f : (tgt_r > 1.0f ? 1.0f : tgt_r);
                gsm_l += ((tgt_l > gsm_l) ? GATE_OPEN : GATE_CLOSE) * (tgt_l - gsm_l);
                gsm_r += ((tgt_r > gsm_r) ? GATE_OPEN : GATE_CLOSE) * (tgt_r - gsm_r);
                genv_l = use_env_l * gsm_l;
                genv_r = use_env_r * gsm_r;
            }
            // Bass envelope amplitude-modulates a 90 Hz carrier so the voice-coil
            // actuator can render it (a near-DC low-passed signal produces no
            // motion; the carrier puts energy in the actuator's responsive band).
            static float carrier_ph = 0.0f;
            carrier_ph += 2.0f * (float)M_PI * 90.0f / 48000.0f;
            if (carrier_ph > 2.0f*(float)M_PI) carrier_ph -= 2.0f*(float)M_PI;
            const float carrier = sinf(carrier_ph);
            float al = genv_l * carrier * auto_gain;
            float ar = genv_r * carrier * auto_gain;
            // Gentler limiting: the old al/(1+|al|) soft-clip crushed everything
            // above ~0.5 and capped the felt strength. tanh stays closer to linear
            // through the mid range and only compresses near the ±1.0 ceiling, so
            // high intensity actually reaches strong output instead of saturating.
            al = tanhf(al);
            ar = tanhf(ar);
            if (auto_mode == 2) {
                // Replace: derived only
                h_l = al; h_r = ar;
            } else {
                // Mix: native haptics (ch2/3) + derived. LP-filter the native
                // channels first — in VoiceMeeter/Windows 4ch setups ch2/3 often
                // mirror the full-band stereo (ch0/1), so passing them raw leaks
                // dialog/high-freq audio straight to the actuators. Filtering to
                // the same bass band keeps only the felt low-frequency content.
                lp_h_l += lp_a * (h_l - lp_h_l);
                lp_h_r += lp_a * (h_r - lp_h_r);
                // Converted rumble: DS4Windows sends motor intensities (0-255) for
                // Xbox360/DS4 emulation. Turn them into an actuator vibration using
                // the same 90 Hz carrier and blend in, scaled by a configurable
                // strength so native rumble doesn't overpower the audio haptics.
                const float rumble_str = get_config().rumble_haptic_strength / 100.0f;
                const float rl = (g_rumble_l / 255.0f) * carrier * rumble_str;
                const float rr = (g_rumble_r / 255.0f) * carrier * rumble_str;
                float m_l = lp_h_l + al + rl, m_r = lp_h_r + ar + rr;
                h_l = m_l / (1.0f + (m_l < 0.0f ? -m_l : m_l));
                h_r = m_r / (1.0f + (m_r < 0.0f ? -m_r : m_r));
            }
        }
        // Anti-aliasing for the 48k->3k haptic decimation below (CONFIGURABLE —
        // portal "Native Haptics" section). The unfiltered 16x decimation folds
        // content above 1.5 kHz back as gritty noise; filtering removes the grit
        // but also softens transient snap, so the strength is a preference:
        //   1 = off    — raw pre-1.0.4 texture (gritty but sharp)
        //   2 = light  — single pole ~2.4 kHz: kills most fold-back, keeps snap
        //   3 = strong — two poles ~1.3 kHz: maximum smoothness (can feel muted)
        if (aa_mode >= 2) {
            static float aa1_l = 0.0f, aa1_r = 0.0f, aa2_l = 0.0f, aa2_r = 0.0f;
            if (aa_mode == 2) {
                constexpr float A = 0.27f;   // ~2.4 kHz @ 48 kHz
                aa1_l += A * (h_l - aa1_l);
                aa1_r += A * (h_r - aa1_r);
                h_l = aa1_l;
                h_r = aa1_r;
            } else {
                constexpr float A = 0.157f;  // ~1.3 kHz @ 48 kHz
                aa1_l += A * (h_l - aa1_l);
                aa1_r += A * (h_r - aa1_r);
                aa2_l += A * (aa1_l - aa2_l);
                aa2_r += A * (aa1_r - aa2_r);
                h_l = aa2_l;
                h_r = aa2_r;
            }
        }
        in_buf[i * 2]     = static_cast<WDL_ResampleSample>(clamp(h_l, -1.0f, 1.0f));
        in_buf[i * 2 + 1] = static_cast<WDL_ResampleSample>(clamp(h_r, -1.0f, 1.0f));
    }

    // 3. 48kHz -> 3kHz 重采样
    static WDL_ResampleSample out_buf[SAMPLE_SIZE]; // 64 floats = 32帧 × 2ch
    const int out_frames = resampler.ResampleOut(out_buf, nframes, nframes / 4, OUTPUT_CHANNELS);

    static int8_t haptic_buf[SAMPLE_SIZE];
    static int haptic_buf_pos = 0;

    // 4. 转换为int8并缓冲，满64字节即组包发送
    for (int i = 0; i < out_frames; i++) {
        int val_l = static_cast<int>(out_buf[i * 2] * 127.0f);
        int val_r = static_cast<int>(out_buf[i * 2 + 1] * 127.0f);
        haptic_buf[haptic_buf_pos++] = (int8_t) clamp(val_l, -128, 127); // 似乎clamp有点多余？还是以防万一吧
        haptic_buf[haptic_buf_pos++] = (int8_t) clamp(val_r, -128, 127);

        if (haptic_buf_pos != SAMPLE_SIZE) {
            continue;
        }
        uint8_t pkt[REPORT_SIZE]{};
        pkt[0] = REPORT_ID;
        pkt[1] = reportSeqCounter << 4;
        reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
        pkt[2] = 0x11 | 0 << 6 | 1 << 7;
        pkt[3] = 7;
        // bit0 enables controller mic streaming. Gate it on the host actually
        // opening the mic IN interface (set_mic_active from tud_audio_set_itf_cb)
        // AND on the user not disabling the mic (config.disable_mic), so the
        // DualSense only streams mic audio -- and core1 only decodes it -- while
        // an app is recording. Other bits (speaker/haptics) stay enabled.
        pkt[4] = (mic_active && !get_config().disable_mic) ? 0b11111111 : 0b11111110;
        const auto buf_len = get_config().audio_buffer_length;
        pkt[5] = buf_len;
        pkt[6] = buf_len;
        pkt[7] = buf_len;
        pkt[8] = buf_len; // 这 4 个字节的作用未知，调整没有效果
        pkt[9] = buf_len; // audio buffer length 只有调整这个字节生效。
        pkt[10] = packetCounter++;
        // SetStateData
        pkt[11] = 0x10 | 0 << 6 | 1 << 7;
        pkt[12] = 63;
        state_set(pkt + 13,63);
        // Haptics Audio Data
        pkt[76] = 0x12 | 0 << 6 | 1 << 7;
        pkt[77] = SAMPLE_SIZE;
        memcpy(pkt + 78, haptic_buf, SAMPLE_SIZE);
#if !DISABLE_SPEAKER_PROC
        // Speaker Audio Data -- omitted entirely when the user disables the
        // speaker/headset (config.disable_speaker), so the controller's speaker
        // amp isn't driven (mirrors the Pico W no-speaker report).
        if (!get_config().disable_speaker) {
            pkt[142] = (plug_headset ? 0x16 : 0x13) | 0 << 6 | 1 << 7; // Speaker: 0x13
            // L Headset Mono: 0x14
            // L Headset R Speaker: 0x15
            // Headset: 0x16
            pkt[143] = 200;
            critical_section_enter_blocking(&opus_cs);
            memcpy(pkt + 144, opus_buf, 200);
            critical_section_exit(&opus_cs);
        }
#endif

        bt_write(pkt, sizeof(pkt));
        haptic_buf_pos = 0;
    }
}

void audio_init() {
    resampler.SetMode(true, 0, false);
    resampler.SetRates(48000, 3000);
    resampler.SetFeedMode(true);
    resampler.Prealloc(2, 24, 6);
    // Mic queues are read from audio_loop on core0 every iteration, so they
    // must exist regardless of the speaker-proc build flag.
    queue_init(&mic_fifo, sizeof(mic_element), 2);
    queue_init(&mic_decode_fifo, sizeof(mic_decode_element), 2);
#if !DISABLE_SPEAKER_PROC
    // Depth 2 (upstream change): with a 1-deep queue, any scheduling slip between
    // core0 (producer) and core1 (speaker_proc) dropped a whole 10.67 ms block via
    // the full->remove-oldest branch — an audible/tactile discontinuity. One frame
    // of slack absorbs the cross-core variance.
    queue_init(&audio_fifo, sizeof(audio_raw_element), 2);
    critical_section_init(&opus_cs);
    multicore_launch_core1_with_stack(core1_entry, audio_core1_stack, sizeof(audio_core1_stack));
#endif
}

static OpusEncoder *encoder;
static OpusDecoder *decoder; // mic decoder
static WDL_Resampler resampler_audio;

// Speaker path: USB OUT PCM (core0 audio_fifo) -> resample -> opus encode ->
// opus_buf for the haptics/speaker BT report. Non-blocking so core1 can also
// service the mic path. Kept in RAM to remove XIP miss latency from the loop.
static void __not_in_flash_func(speaker_proc)() {
    static audio_raw_element audio_element{};
    if (!queue_try_remove(&audio_fifo, &audio_element)) {
        return;
    }
    // 将 512 frames 重采样成 480 frames 以解决噪音问题。感谢 @Junhoo
    WDL_ResampleSample *in_buf;
    int nframes = resampler_audio.ResamplePrepare(512, 2, &in_buf);
    for (int i = 0; i < nframes * 2; i++) {
        in_buf[i] = audio_element.data[i];
    }
    static WDL_ResampleSample out_buf[480 * 2];
    resampler_audio.ResampleOut(out_buf, nframes, 480, 2);

    static uint8_t out[200];
    (void) opus_encode_float(encoder, out_buf, 480, out, 200);
    critical_section_enter_blocking(&opus_cs);
    memcpy(opus_buf, out, 200);
    critical_section_exit(&opus_cs);
}

// Mic path: opus packets from the controller (core0 mic_fifo) -> opus decode ->
// PCM into mic_decode_fifo for audio_loop to push to the USB IN endpoint.
static void __not_in_flash_func(mic_proc)() {
    static mic_element mic_packet{};
    if (!queue_try_remove(&mic_fifo, &mic_packet)) {
        return;
    }
    static int16_t decoded_data[MIC_FRAMES * MIC_CHANNELS];
    auto decoded_samples = opus_decode(decoder, mic_packet.data, MIC_OPUS_SIZE, decoded_data, MIC_FRAMES, false);
    if (decoded_samples <= 0) {
        // Gated behind ENABLE_VERBOSE: printf pulls the newlib formatting chain
        // (flash) onto core1's path. Release builds compile it out so core1's
        // audio loop stays fully RAM-resident (no XIP fetches on this core).
#if ENABLE_VERBOSE
        printf("[Audio] OpusDecoder decode failed: %d\n", decoded_samples);
#endif
        return;
    }
    static mic_decode_element decode_element{};
    decode_element.len = decoded_samples * MIC_CHANNELS * sizeof(int16_t);
    memcpy(decode_element.data, decoded_data, decode_element.len);
    if (queue_is_full(&mic_decode_fifo)) {
        queue_try_remove(&mic_decode_fifo, NULL);
    }
    queue_try_add(&mic_decode_fifo, &decode_element);
}

void __not_in_flash_func(core1_entry)() {
    // Register core1 as a flash-safe victim so core0's flash_safe_execute() really
    // parks this core while flash is accessed, instead of letting it fault on XIP.
    // Used by config_save() (flash erase/program) and the BOOTSEL poll (which briefly
    // floats QSPI CSn) - the latter makes polling BOOTSEL safe while audio streams on
    // core1. Requires PICO_FLASH_ASSUME_CORE1_SAFE=0.
    flash_safe_execute_core_init();
    int error = 0;
    encoder = opus_encoder_create(48000, 2,OPUS_APPLICATION_AUDIO, &error);
    if (error != 0) {
        printf("[Audio] OpusEncoder create failed\n");
        return;
    }
    opus_encoder_ctl(encoder,OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));
    opus_encoder_ctl(encoder,OPUS_SET_BITRATE(200 * 8 * 100));
    opus_encoder_ctl(encoder,OPUS_SET_VBR(false));
    opus_encoder_ctl(encoder,OPUS_SET_COMPLEXITY(0)); // max 4
    resampler_audio.SetMode(true, 0, false);
    resampler_audio.SetRates(51200, 48000);
    resampler_audio.SetFeedMode(true);
    resampler_audio.Prealloc(2, 512, 480);
    decoder = opus_decoder_create(48000, MIC_CHANNELS, &error);
    if (error != 0) {
        printf("[Audio] OpusDecoder create failed\n");
    }

    while (true) {
        speaker_proc();
        mic_proc();
    }
}

// data points at the opus mic payload, len is the bytes available there.
// In RAM (consistent with the BT-receive path) and validates len so a short
// or malformed report can't over-read past the packet buffer.
void __not_in_flash_func(mic_add_queue)(uint8_t *data, uint16_t len) {
    if (len < MIC_OPUS_SIZE) return;
    static mic_element mic_packet{};
    memcpy(mic_packet.data, data, MIC_OPUS_SIZE);
    if (queue_is_full(&mic_fifo)) {
        queue_try_remove(&mic_fifo, NULL);
    }
    queue_try_add(&mic_fifo, &mic_packet);
}

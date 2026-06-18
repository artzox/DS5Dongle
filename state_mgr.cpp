//
// Created by awalol on 2026/5/15.
//
// Auto-haptics / DS4Windows modifications (c) 2026 Serge — MIT License.
// The state_set-in-RAM relocation that enables haptic actuation at stock clocks
// originates from loteran's auto-haptics work on the DS5Dongle. With thanks.
//

#include "state_mgr.h"

#include <cstddef>
#include <cstring>

#include "pico.h"
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

void state_update(const uint8_t *data, const uint8_t size) {
    if (size > sizeof(SetStateData)) {
        printf(
            "[StateMgr] Error: SetStateData max %u bytes, request %u\n",
            static_cast<unsigned>(sizeof(SetStateData)),
            size
        );
        return;
    }

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

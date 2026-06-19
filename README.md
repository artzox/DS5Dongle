# DS5Dongle — Audio Auto-Haptics Edition

**Version 1.0.1-hotfix2**

A firmware modification for the [DS5Dongle](https://github.com/awalol/DS5Dongle)
(a Raspberry Pi Pico 2W-based wireless DualSense dongle) that adds **audio-derived
haptics** for games without native DualSense support, full **DS4Windows
compatibility**, **converted-rumble blending**, and a **controller-speaker effect
leak** for added immersion — all configurable from a web-based portal.

Built on **awalol/DS5Dongle v0.7.0**, which relocates the entire BT/USB/audio path
to RAM so native fine haptics and controller audio work without overclocking.

---

## What this adds over the base firmware

- **Audio-derived auto-haptics** — generates haptic feedback from game audio for
  titles that have no native DualSense haptics. Works over Bluetooth.
- **DS4Windows compatibility** — auto-haptics keep working under DS4Windows in
  passthrough, DualShock 4, and Xbox 360 emulation modes (the base firmware's
  rumble handling silenced the haptic actuators; this fixes it).
- **Three operating modes** — Off (native passthrough / rumble conversion), Mix
  (native + derived), Replace (derived only).
- **Converted-rumble blending** — in Mix mode, DS4Windows rumble (from emulated
  controllers) is converted to actuator vibration and blended with the audio
  haptics, with independent strength control.
- **Effect leak** — instead of fully muting the controller speaker, optionally
  pass sharp transient effects (shots, clinks, impacts) through the speaker via
  transient detection, so discrete effects come through while sustained dialog and
  music stay muted — like native DualSense games.
- **Extensive DSP tuning** — intensity, smoothness, noise gate, crossover cutoff,
  and selectable filter slope (6/12/24 dB/oct).
- **Lightbar control**, **live RSSI / signal display**, **reboot-to-bootloader**,
  and **experimental BT latency controls** (flush timeout, QoS).
- **Sectioned web configuration portal** with smart save (only reconnects when a
  setting that requires re-enumeration changed).

The **Wake PC on PS Button** feature (USB remote wakeup) is part of the awalol
v0.7.0 base and is exposed here as a portal toggle.

---

## Quick start

1. **Flash the firmware.** Hold the BOOTSEL button while plugging in the Pico 2W
   (or triple-click BOOTSEL on an already-running unit), then copy
   `ds5dongle-autohaptics-v1.0.1-hotfix2.uf2` to the `RPI-RP2` drive that appears.
   - **First time / after a settings-structure change:** flash `flash_nuke.uf2`
     first to clear old settings, then flash this firmware.
2. **Open the portal.** **Download** `ds5-config-portal.html` and open the
   downloaded file in Chrome or Edge. (WebHID needs a secure context — opening it
   directly from a website host or `file://` that the browser flags will fail with
   a permissions error. Downloading it and opening the local file works, as does
   serving it from `http://localhost`.)
3. **Connect.** Click *Connect* and select the DualSense.
4. **Configure.** Set *Auto Haptics Mode* and tune to taste, then *Save to Device*.

The firmware ships with safe stock defaults (auto-haptics off, standard buffer),
so a fresh flash works out of the box with no required configuration. For a tuned
auto-haptics + immersion setup, see **Suggested setup** below.

---

## Suggested setup

These are good starting values for using auto-haptics with audio passthrough and
the effect leak, tuned on a real DualSense over Bluetooth. They are *suggestions* —
adjust to taste — but they give a working, balanced configuration without trial and
error. (The firmware does not ship with these as defaults, so there's no flashing
surprise; apply them in the portal and save.)

**Auto-Haptics**
| Setting | Value |
|---|---|
| Mode | Off (switch to Mix or Replace per game) |
| Intensity (%) | 80 |
| Smoothness | 40 |
| Noise Gate | 20 |
| LP Cutoff (Hz) | 100 |
| Filter Slope | 12 dB/oct |
| Auto-mute Speaker (Replace) | Yes |
| Auto-mute Speaker (Mix) | Yes |
| Lightbar Off in Replace Mode | Yes |
| Converted Rumble Strength (Mix) | 50 |
| Effect Leak Volume (0=off) | 0 (raise to enable) |
| Effect Leak Sensitivity | 50 |
| Effect Leak Decay/Fade-out | 80 |
| Effect Leak Attack/Responsiveness | 50 |
| Effect Leak Output High-pass (Hz) | 1000 |
| Effect Leak Detection Band (Hz) | 2500 |

**Haptics & Audio**
| Setting | Value |
|---|---|
| Native Haptics Gain | 1.00 |
| Speaker Volume | 100 |
| Headset Volume | 100 |
| Speaker Gain | 2 |
| Sync Speaker & Headset Volume | Yes |
| Lock Volume | No |
| Disable Mic | No |
| Disable Speaker | No |

**Device & Connection**
| Setting | Value |
|---|---|
| Polling Rate | Real-time |
| Audio Buffer Length | 16 |
| Inactive Time (min) | 12 |
| Disable Inactive Disconnect | No |
| Disable Pico LED | No |
| Wake PC on PS Button | Yes |

**Advanced — BT Latency (experimental)**
| Setting | Value |
|---|---|
| BT Flush Timeout | Off (reliable) |
| BT QoS Latency | Off |

Notes on the suggested values:
- **Mode is set per game.** Leave it Off for games with good native DualSense
  haptics (full fidelity). Use **Replace** for games with no native haptics, or
  **Mix** for non-native games where you also want converted controller rumble.
- **Effect Leak Volume starts at 0 (off).** Raise it (e.g. 20–30) to enable the
  effect leak. With it on, **Audio Buffer Length 16** keeps latency low; the
  transient-detection leak no longer requires a deep buffer.
- **Detection Band 2500 Hz + Output High-pass 1000 Hz** make the leak selective to
  sharp effects and protect the small controller speaker from low-frequency popping.
- **Decay 80** gives effects a gradual, natural fade-out rather than an abrupt cut.

---

## Configuration reference

The portal is organized into four sections.

### Auto-Haptics
| Setting | Range | Default | Notes |
|---|---|---|---|
| Mode | Off / Mix / Replace | Off | Off = native/rumble passthrough; Mix = native + derived; Replace = derived only |
| Intensity (%) | 0–200 | 100 | Strength of the audio-derived haptics (curved response) |
| Smoothness | 0–100 | 40 | Higher = smoother/longer decay; lower = snappier |
| Noise Gate | 0–100 | 20 | Suppresses quiet content (dialog/ambience) below a threshold |
| LP Cutoff (Hz) | 30–200 | 60 | Crossover — only audio below this drives haptics |
| Filter Slope | 6 / 12 / 24 dB/oct | 12 | Steeper rejects voice above the cutoff more aggressively |
| Auto-mute Speaker (Replace) | on/off | on | Mute controller speaker in Replace mode |
| Auto-mute Speaker (Mix) | on/off | off | Mute controller speaker in Mix mode |
| Lightbar Off in Replace Mode | on/off | off | Kills the lightbar glow in Replace (e.g. blue in Xbox360 mode) |
| Converted Rumble Strength (Mix) | 0–100 | 50 | Strength of blended DS4Windows rumble in Mix mode |
| Effect Leak Volume | 0–100 | 0 (off) | Volume of the transient effect leak through the speaker when auto-muted |
| Effect Leak Sensitivity | 0–100 | 50 | How sudden a level jump counts as an effect (higher = more leaks through) |
| Effect Leak Decay/Fade-out | 0–100 | 40 | How gradually effects fade after triggering (~50 ms .. 500 ms) |
| Effect Leak Attack/Responsiveness | 0–100 | 50 | How fast the gate opens (higher = more immediate, less delay) |
| Effect Leak Output High-pass (Hz) | 50–2000 | 200 | Removes deep bass from the speaker output to stop popping |
| Effect Leak Detection Band (Hz) | 100–5000 | 800 | Frequency band the transient detector listens to |

### Haptics & Audio
| Setting | Range | Default | Notes |
|---|---|---|---|
| Native Haptics Gain | 1.0–2.0 | 1.0 | Multiplier on native haptic channels |
| Speaker Volume | 0–127 | 100 | Controller speaker volume (also scales haptic strength) |
| Headset Volume | 0–127 | 100 | Headset jack volume |
| Speaker Gain | 0–7 | 2 | Controller speaker gain stage |
| Sync Speaker & Headset Volume | on/off | on | Tie the two volumes together |
| Lock Volume | on/off | off | Ignore in-game volume changes |
| Disable Mic | on/off | off | Disable the controller microphone |
| Disable Speaker | on/off | off | Disable the controller speaker |

### Device & Connection
| Setting | Range | Default | Notes |
|---|---|---|---|
| Polling Rate | 250 / 500 / Real-time | Real-time | USB report rate |
| Audio Buffer Length | 16–128 | 64 | Lower = snappier haptics/lower latency; higher = more audio stability |
| Inactive Time (min) | 5–60 | 30 | Idle timeout before disconnect |
| Disable Inactive Disconnect | on/off | off | Never auto-disconnect when idle |
| Disable Pico LED | on/off | off | Turn off the Pico's onboard LED |
| Wake PC on PS Button | on/off | off | Assert USB remote wakeup on PS press to wake the host |

### Advanced — BT Latency (experimental)
| Setting | Default | Notes |
|---|---|---|
| BT Flush Timeout | Off | Drop stale packets instead of retransmitting. No clear benefit on a strong link; left in for tinkering. |
| BT QoS Latency | Off | Request a tighter poll interval. Inconclusive in testing; left in for tinkering. |

---

## Modes explained

- **Off** — The controller behaves as the base firmware: native DualSense haptics
  pass through unfiltered, and (with DS4Windows + a single isolated controller)
  Xbox360/DS4 rumble is converted to DualSense rumble. **Use this for games that
  already have good native DualSense haptics** — it gives full fidelity.
- **Mix** — Native haptic channels (low-passed) + audio-derived haptics +
  optional converted DS4Windows rumble. For games without native haptics where you
  also want emulated-controller rumble.
- **Replace** — Audio-derived haptics only. Cleanest option for non-native games.

---

## How it works (brief)

The DualSense haptic actuator is a voice coil that cannot render a near-DC signal,
so a naive low-pass of the audio produces no motion. This firmware instead uses the
bass **envelope** of the game audio to amplitude-modulate a 90 Hz carrier that sits
in the actuator's responsive band — turning "how much bass" into felt rumble. A
noise gate and a steep, configurable low-pass keep dialog and music from triggering
the haptics. Under DS4Windows, the firmware keeps the controller in actuator mode
(rather than letting rumble reports force it into motor mode) so the derived
haptics keep playing.

The **effect leak** uses transient detection: it tracks fast and slow envelopes of
the high-frequency content and opens the speaker only when the level jumps sharply
(an onset), so discrete impacts pass while sustained sound stays muted. The output
is high-passed to protect the small speaker from low-frequency popping.

---

## Notes & known behavior

- **Saving / PlayStation app:** Settings are written to flash and applied
  immediately. Most settings apply live with no reconnect; only a few that require
  USB re-enumeration (polling rate, audio buffer, mic/speaker enable, wake) trigger
  a brief reconnect on save. If you have the PlayStation accessory app open, after
  saving you may need to **wait 2–3 seconds** before reopening the setting to see
  the updated value, or simply **open it a second time** — the display lags
  slightly, but the settings are saved correctly.
- **Effect leak latency.** The effect leak goes through the controller's speaker
  audio pipeline (Opus codec over Bluetooth), which has inherent latency. Transient
  effects expose this more than continuous audio would. It is reduced as much as
  the codec allows but is not zero.
- **Bluetooth vs USB latency.** Native haptics over Bluetooth are slightly less
  tight than over USB — this is inherent to the BT transport (slot scheduling vs
  USB's fixed microframes) and is not tunable away in firmware. A strong link
  (check the RSSI display) keeps it as good as it gets.
- **WebHID requires Chrome/Edge** and a secure context (download the portal file or
  serve from localhost). Firefox and Safari do not support WebHID.
- **Always nuke after a structure change.** When updating to a firmware build with
  changed settings, flash `flash_nuke.uf2` first, then this firmware, to avoid
  stale/garbage settings.
- **Wake from sleep** depends on the host's sleep state. USB device remote-wakeup
  works from traditional S3 sleep; behavior under Modern Standby (S0 Low Power Idle)
  varies by system, and the device's "Allow this device to wake the computer"
  setting must be enabled in Windows Device Manager.
- **Wake and DS4Windows.** With wake enabled, the USB device stays on the bus after
  the controller powers off (this is required so a later button press can wake the
  host). As a side effect, the device may remain listed in DS4Windows after the
  controller disconnects. If you want the device to disappear cleanly from
  DS4Windows on disconnect, turn wake off. The two behaviors are inherently in
  tension because wake needs the persistent USB presence.
- **Wake and reconnection.** Because wake keeps the device on the bus, the firmware
  forces a clean USB re-enumeration when the controller reconnects, so the
  connection completes normally (this fixes a stuck, non-functional connection that
  could otherwise occur with wake enabled). Reconnection may occasionally take a
  moment longer as a result.

---

## Building from source

Requires the Pico SDK (2.x) with the Pico 2W board support.

```sh
git clone https://github.com/awalol/DS5Dongle.git
cd DS5Dongle
git checkout v0.7.0
# apply the changes:
git apply /path/to/ds5dongle-autohaptics.patch
# or copy the files from src/ over the originals

mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_BOARD=pico2_w
make -j
```

The resulting `ds5-bridge.uf2` is the firmware.

### Modified files
- `src/audio.cpp` — auto-haptics DSP (channel detection, filter cascade, envelope,
  noise gate, carrier modulation, converted-rumble blend, transient effect leak,
  speaker mute)
- `src/state_mgr.cpp` — DS4Windows rumble-mode fix, rumble value capture,
  lightbar-off-in-Replace, `state_set` in RAM
- `src/bt.cpp` — BT flush timeout / QoS controls, RSSI signal strength readout
- `src/config.h` / `src/config.cpp` — config fields, validation, defaults
- `src/cmd.cpp` — config field-ID read/write handlers, diagnostics, reboot-to-bootloader
- `src/main.cpp` / `src/state_mgr.h` — stuck-rumble fix (send state to the controller when it changes even while the speaker is active)

---

## Credits & license

This is a derivative of **[awalol/DS5Dongle](https://github.com/awalol/DS5Dongle)**,
which in turn builds on earlier community work on the DualSense dongle concept.

**Auto-haptics origin — thanks to loteran.** The audio-derived auto-haptics in this
project were inspired by **loteran's** earlier auto-haptics experiments on the
DS5Dongle. The single most important insight came from loteran: relocating
`state_set` to RAM, which is what allows the haptic actuators to fire at stock clock
speeds (150 MHz) instead of requiring an overclock. The DSP and supporting code here
were rewritten from scratch (carrier modulation, channel detection, the noise gate,
the DS4Windows handling, the effect leak, and the config protocol are new), but
loteran's groundwork is what made the whole feature possible. This release would not
exist without it — thank you.

Thanks also to the broader DS5Dongle contributors and to awalol for the complete
RAM relocation in v0.7.0 that keeps native haptics and controller audio working
without overclocking, and for the wake-on-PS-button implementation.

Licensed under the **MIT License** — see [LICENSE](LICENSE). The original awalol
copyright notice is preserved as required.

---

## Files in this release

- `ds5dongle-autohaptics-v1.0.1-hotfix2.uf2` — the firmware (flash this)
- `ds5-config-portal.html` — the web configuration portal (download and open)
- `src/` — the modified source files
- `ds5dongle-autohaptics.patch` — unified diff against awalol v0.7.0
- `LICENSE` — MIT license
- `README.md` — this file
- `CHANGELOG.md` — version history

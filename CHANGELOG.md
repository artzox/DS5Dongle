# Changelog

All notable changes to this project are documented here.

## [1.0.0] — 2026-06-18

First public release. Built on awalol/DS5Dongle v0.7.0.

### Added
- **Audio-derived auto-haptics** for games without native DualSense haptics, using
  bass-envelope amplitude modulation of a 90 Hz carrier so the voice-coil actuator
  physically actuates (a plain low-pass produces a near-DC signal the coil cannot
  render).
- **Three modes:** Off (native/rumble passthrough), Mix (native + derived),
  Replace (derived only).
- **DS4Windows compatibility:** auto-haptics now work under DS4Windows in
  passthrough, DualShock 4, and Xbox 360 modes. Fixed the base firmware forcing
  `UseRumbleNotHaptics`, which silenced the actuator path that auto-haptics needs.
- **Converted-rumble blending (Mix mode):** DS4Windows rumble from emulated
  controllers is converted to actuator vibration and blended with the audio
  haptics, with an independent strength control.
- **Effect leak:** optional high-passed, low-volume speaker passthrough so sharp
  transient effects come through quietly when the speaker would otherwise be muted.
- **DSP controls:** intensity (curved), smoothness, noise gate, low-pass crossover,
  and selectable filter slope (6/12/24 dB/oct).
- **Lightbar Off in Replace mode** to suppress the default glow (e.g. blue in
  Xbox 360 emulation).
- **Live RSSI / Bluetooth signal display** in the portal.
- **Reboot-to-bootloader** command from the portal.
- **Experimental BT latency controls:** automatic flush timeout and QoS setup
  (both default off; inconclusive in testing, retained for tinkering).
- **Web configuration portal:** sectioned UI, full field set, auto-reconnect and
  refresh on save, robust device-handle and read handling.

### Changed
- Channel detection (2ch vs 4ch) so DS4Windows/Windows stereo streams are handled
  correctly; mode remains authoritative (2ch input does not force auto-haptics on).
- Mix mode low-passes the native haptic channels before mixing to prevent full-band
  audio (e.g. duplicated stereo in VoiceMeeter setups) leaking to the actuators.
- Default audio buffer length is 64 (stable for native audio and the effect leak);
  can be lowered to 16 for snappier haptics-only use.

### Notes
- Native haptics over Bluetooth remain slightly less tight than over USB; this is
  inherent to the BT transport and not tunable in firmware.
- The effect leak requires audio buffer length 64 to avoid dropouts.
- After saving, the PlayStation accessory app may take 2–3 seconds (or a second
  reopen) to display an updated value, though the setting is saved immediately.

[1.0.0]: https://github.com/awalol/DS5Dongle

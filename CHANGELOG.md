# Changelog

All notable changes to this project are documented here.

## [1.0.1] — 2026-06-19

First public release. Built on awalol/DS5Dongle v0.7.0. Refines the initial 1.0.0
cut with the effect-leak rework, the wake toggle, and a batch of portal reliability
fixes.

### Added
- **Effect leak output high-pass** — removes deep bass from the speaker output to
  stop the small controller speaker from popping on low-frequency content (separate
  from the transient detection band).
- **Effect leak attack control** — configurable gate-open speed to trade immediacy
  against onset smoothness.
- **Effect leak decay control** — configurable fade-out length so effects ring out
  gradually instead of cutting off abruptly.
- Exposed the base firmware's **Wake PC on PS Button** (USB remote wakeup) as a
  portal toggle.

### Changed
- **Effect leak reworked to transient detection** — opens the speaker only on sharp
  onsets rather than passing all high-frequency content, so sustained dialog/music
  stays muted while discrete effects pass. Output is full-band (gated) to avoid the
  thin/crackly sound of the earlier high-pass-only approach.
- **Smart save** — the portal now only triggers a reconnect when a setting that
  requires USB re-enumeration changed (polling rate, audio buffer, mic/speaker
  enable, wake); all other settings apply live with no reconnect.

### Fixed
- **Portal save reliability** — background diagnostic/RSSI polling no longer
  collides with saves (which previously caused saves to fail after a few cycles).
- **Stale device-handle recovery** — saves re-acquire a fresh handle if the cached
  one went stale after a reconnect, instead of silently failing.
- **Refresh no longer blanks values to 0** — failed reads preserve the previous
  value and retry, with a settle delay after reconnect before reading.

## [1.0.0] — 2026-06-18

Initial internal build. Built on awalol/DS5Dongle v0.7.0.

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
- **Effect leak (transient detection):** optional speaker passthrough that opens
  only on sharp onsets (shots, clinks, impacts) and stays muted for sustained
  dialog/music. Configurable volume, sensitivity, attack, decay, detection band,
  and an output high-pass that protects the speaker from low-frequency popping.
- **DSP controls:** intensity (curved), smoothness, noise gate, low-pass crossover,
  and selectable filter slope (6/12/24 dB/oct).
- **Lightbar Off in Replace mode** to suppress the default glow (e.g. blue in
  Xbox 360 emulation).
- **Live RSSI / Bluetooth signal display** in the portal.
- **Reboot-to-bootloader** command from the portal.
- **Experimental BT latency controls:** flush timeout and QoS setup (both default
  off; inconclusive in testing, retained for tinkering).
- **Web configuration portal:** sectioned UI, full field set, robust device-handle
  and read handling, smart save (only reconnects when a setting that requires USB
  re-enumeration actually changed).
- Exposed the base firmware's **Wake PC on PS Button** (USB remote wakeup) as a
  portal toggle.

### Changed
- Channel detection (2ch vs 4ch) so DS4Windows/Windows stereo streams are handled
  correctly; mode remains authoritative (2ch input does not force auto-haptics on).
- Mix mode low-passes the native haptic channels before mixing to prevent full-band
  audio (e.g. duplicated stereo in VoiceMeeter setups) leaking to the actuators.

### Notes
- Native haptics over Bluetooth remain slightly less tight than over USB; this is
  inherent to the BT transport and not tunable in firmware.
- The effect leak inherits the controller speaker's Opus-over-Bluetooth pipeline
  latency; it is minimized but not zero, and transient effects expose it more than
  continuous audio.
- After saving, the PlayStation accessory app may take 2–3 seconds (or a second
  reopen) to display an updated value, though the setting is saved immediately.
- The firmware ships with stock defaults; see the README "Suggested setup" for a
  tuned auto-haptics + effect-leak configuration to apply in the portal.

[1.0.1]: https://github.com/awalol/DS5Dongle
[1.0.0]: https://github.com/awalol/DS5Dongle

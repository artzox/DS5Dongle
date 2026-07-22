# DS5Dongle — Audio Auto-Haptics Edition

**Version 1.13.3**

A firmware modification for the [DS5Dongle](https://github.com/awalol/DS5Dongle)
(a Raspberry Pi Pico 2W-based wireless DualSense dongle) that adds **audio-derived
haptics** for games without native DualSense support, full **DS4Windows
compatibility**, **converted-rumble blending**, and a **controller-speaker effect
leak** for added immersion — all configurable from a web-based portal.

> ⚠️ **Hardware requirement — Pico 2 W only.** The released `.uf2` is built for the
> **Raspberry Pi Pico 2 W** (RP2350). It will **not** run on the original
> **Raspberry Pi Pico W** (RP2040) — they are different chips (Cortex-M33 vs
> Cortex-M0+, different memory and no FPU on the RP2040), so a binary built for one
> cannot run on the other. This firmware's floating-point audio DSP (auto-haptics,
> the effect leak, resampling) is designed around the RP2350's FPU and larger RAM;
> the original Pico W is not supported. Make sure your board is a **Pico 2 W**
> before flashing.

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
- **Trigger-to-rumble (R2T)** — routes rumble into the trigger actuators as a
  tactile buzz, per trigger, with an "only while pressed" option, strength, and
  frequency. Useful for feeling rumble through the triggers in games that only
  send classic rumble.
- **Adaptive triggers (Stages 1 + 2)** — gated constant resistance for a light
  adaptive-trigger effect in games that don't drive the triggers themselves. R2
  and L2 are **fully independent**, each with its own mode, strength, threshold,
  start position and kick: gate a trigger by the opposite trigger (analog) or the
  opposite shoulder button (L1→R2, R1→L2, digital), or run it always-on. Stage 2
  adds a **push-back kick (recoil)**: while resistance is engaged, rumble/haptics
  bursts knock the trigger back against your finger — as a low-frequency vibration
  thump or a mechanical **bow-snap**, selectable per trigger — then resistance
  resumes.
- **Profile slots** — up to 8 complete configurations stored on the dongle
  itself. Save your setups once in the portal; switching later is a single
  instant command instead of a full profile write — used by the automation for
  per-game profiles (`Game = slot 3` in `profile-overrides.txt`), and applied
  atomically so a game launch can never leave the dongle half-configured.
- **Gyro-to-stick aiming** — maps controller motion onto the right stick for
  motion aiming, with selectable activation (always / while L2 held / touchpad
  touch / ratchet), sensitivity, horizontal axis source (yaw or roll), and
  per-axis invert.
- **Native-haptics anti-alias** — optional smoothing on the native haptic stream
  (off / light / strong) to tame gritty high-frequency actuator noise.
- **Sectioned web configuration portal** with smart save (only reconnects when a
  setting that requires re-enumeration changed).

The **Wake PC on PS Button** feature (USB remote wakeup) is part of the awalol
v0.7.0 base and is exposed here as a portal toggle. It is **off by default**; enable
it only if you want the controller's PS button to wake the PC from sleep. In this
release the controller disconnects cleanly from the host (and DS4Windows) whenever
the PC is awake, even with wake enabled — the device only stays on the USB bus while
the PC is actually asleep (where wake needs it).

> **Important — leave wake OFF.** Enabling wake changes the controller's USB
> descriptor (it advertises USB 2.1 with a BOS descriptor and adds a keyboard
> interface, which the wake mechanism requires). This has two consequences:
>
> 1. The altered descriptor no longer looks like a "pure" DualSense, so Steam Input
>    can stop recognizing it — games such as *Ratchet & Clank* may fall back to
>    Xbox-style rumble instead of native DualSense haptics, and the controller's
>    speaker audio can stop working.
> 2. Because enabling/disabling wake forces a USB re-enumeration, it disrupts the
>    portal's live connection — the auto-apply profiles (and config saves) can fail
>    when wake is toggled as part of a profile.
>
> These are fundamental to how wake works, not bugs. **Recommendation: pick wake on
> or off once and leave it there.** If you rely on native haptics or the auto-apply
> profiles, keep wake **off**. Switching wake per-game via a profile is *not*
> reliable because of consequence #2, so the automation does not attempt it.

---

## Quick start

1. **Flash the firmware.** *(Pico 2 W / RP2350 only — see the hardware note above;
   this will not run on the original Pico W.)* Hold the BOOTSEL button while
   plugging in the Pico 2W
   (or triple-click BOOTSEL on an already-running unit), then copy
   `ds5-v1.13.3.uf2` to the `RPI-RP2` drive that appears.
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
| Intensity (%) | 80 Scales the DERIVED auto-haptics only; in Mix mode native ch 2/3 pass through unscaled (see ds5audio `--map` note) |
| Smoothness | 40 |
| Noise Gate | 20 |
| LP Cutoff (Hz) | 100 |
| Frequency Split Crossover (Hz) | 0 (off) |
| Low Band Gain | 100 |
| High Band Gain | 100 |
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
| Effect Leak Output Low-pass (Hz) | 8000 |
| Effect Leak Gate Hold (x5 ms) | 20 |
| Effect Leak Max Burst (x5 ms) | 0 (off; try 30) |
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
| Wake PC on PS Button | Off (enable only if you want PS-button wake-from-sleep) |

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

The portal groups settings into the sections below.

### Auto-Haptics
| Setting | Range | Default | Notes |
|---|---|---|---|
| Mode | Off / Mix / Replace | Off | Off = native/rumble passthrough; Mix = native + derived; Replace = derived only |
| Intensity (%) | 0–200 | 100 | Strength of the audio-derived haptics (curved response) |
| Smoothness | 0–100 | 40 | Higher = smoother/longer decay; lower = snappier |
| Noise Gate | 0–100 | 20 | Suppresses quiet content (dialog/ambience) below a threshold |
| Native Passthrough in Mix (%) | 0–100 | 100 | MIX MODE ONLY: level of the native ch3/4 haptic stream mixed under the derived haptics. See "Choosing the passthrough level" below |
| LP Cutoff (Hz) | 30–200 | 60 | Upper edge of the haptics band — only audio **below** this drives haptics (content above never reaches the actuators) |
| Frequency Split Crossover (Hz) | 0 / 30–200 | 0 (off) | Divides the haptics band in two at this frequency; 0 = single-band (identical to pre-split firmware). Must sit **below** LP Cutoff |
| Low Band Gain | 0–100 | 100 | Contribution of content **below** the crossover (impacts, explosions, engine weight) |
| High Band Gain | 0–100 | 100 | Contribution of the crossover..cutoff range (music bass, voice fundamentals) — lower it to tame music/dialog-driven buzz |
| Filter Slope | 6 / 12 / 24 dB/oct | 12 | Steeper rejects voice above the cutoff more aggressively |

#### Choosing the passthrough level (Native Passthrough in Mix)

In **Mix** mode the actuators receive three components: the **native ch3/4
stream** (scaled by this fader), the **derived** auto-haptics (scaled by
Intensity, shaped by the frequency split and gate), and **converted rumble**
(its own strength knob). The fader exists because ch3/4 mean different things in
different setups:

| Scenario | Mode | Set passthrough to | Why |
|---|---|---|---|
| Native game, passthrough profile | Off | (ignored) | With auto-haptics Off, ch3/4 always pass at full — the game's own HD haptics. The fader has no effect in Off |
| Native game + auto-haptics on top | Mix | **100** | Classic Mix: the game's real haptics plus derived augmentation |
| Non-native game (DS4Windows / XB360 / DS4) | Mix | **0** (or taste) | ds5audio's default `duplicate` mapping copies the game audio onto ch3/4 — an uncontrollable shadow of the derived haptics. At 0, Intensity/split/gate control the whole output; game rumble stays on Converted Rumble Strength. With the fader at 0 the ds5audio `--map` choice no longer matters in Mix |
| Any game | Replace | (ignored) | Replace discards ch3/4 by definition — derived only |

Rule of thumb: **the fader answers "is there anything REAL on ch3/4?"** Native
game = yes, keep 100. Non-native = no (it's a duplicate), set 0.
| Auto-mute Speaker (Replace) | on/off | on | Mute controller speaker in Replace mode |
| Auto-mute Speaker (Mix) | on/off | off | Mute controller speaker in Mix mode |
| Lightbar Off in Replace Mode | on/off | off | Kills the lightbar glow in Replace (e.g. blue in Xbox360 mode) |
| Converted Rumble Strength (Mix) | 0–100 | 50 | Strength of blended DS4Windows rumble in Mix mode |
| Effect Leak Volume | 0–100 | 0 (off) | Volume of the transient effect leak through the speaker when auto-muted |
| Effect Leak Sensitivity | 0–100 | 50 | How sudden a level jump counts as an effect (higher = more leaks through) |
| Effect Leak Decay/Fade-out | 0–100 | 40 | How gradually effects fade after triggering (~50 ms .. 500 ms) |
| Effect Leak Attack/Responsiveness | 0–100 | 50 | How fast the gate opens (higher = more immediate, less delay) |
| Effect Leak Output High-pass (Hz) | 50–2000 | 200 | Low wall of the leak window (12 dB/oct) — removes deep bass that pops the speaker |
| Effect Leak Output Low-pass (Hz) | 500–12000 | 8000 | High wall of the leak window (12 dB/oct) — cuts treble sizzle/crackle. With the high-pass forms a band-pass "capture window": only sound inside it leaks. Automatic make-up gain keeps loudness constant as you move the walls |
| Effect Leak Gate Hold (×5 ms) | 0–100 | 20 (100 ms) | Minimum gate-open time per transient + hysteresis; stops the gate chattering (the "choppy/poppy" leak artifact) |
| Effect Leak Max Burst (×5 ms) | 0–100 | 0 (off) | MAXIMUM gate-open time: cuts sustained sounds (dialogue, music) at the cap with a no-retrigger refractory — one short accent instead of duplicating the room audio; shots end within the cap naturally. Try 30 (150 ms) and raise leak volume: the leak becomes punctuation, not a second speaker |
| Effect Leak Detection Band (Hz) | 100–5000 | 800 | Frequency band the transient detector listens to |

### Haptics & Audio
| Setting | Range | Default | Notes |
|---|---|---|---|
| Native Haptics Gain | 1.0–2.0 | 1.0 | Multiplier on native haptic channels |
| Native Haptics Anti-alias | Off / Light / Strong | Light | Smooths the native haptic stream (Off = raw/gritty; Strong = softest) |
| Speaker Volume | 0–127 | 100 | Controller speaker volume (also scales haptic strength) |
| Headset Volume | 0–127 | 100 | Headset jack volume |
| Speaker Gain | 0–7 | 2 | Controller speaker gain stage |
| Sync Speaker & Headset Volume | on/off | on | Tie the two volumes together |
| Lock Volume | on/off | off | Ignore in-game volume changes |
| Disable Mic | on/off | off | Disable the controller microphone |
| Disable Speaker | on/off | off | Disable the controller speaker |

### Trigger-to-Rumble (R2T)
Routes the rumble signal into the trigger actuators as a buzz.

| Setting | Range | Default | Notes |
|---|---|---|---|
| R2T Mode | Off / Left / Right / Both | Off | Which trigger(s) buzz from rumble |
| Only While Pressed | on/off | off | on = buzz only when the trigger is pulled past ~25%; off = buzz whenever there's rumble |
| Strength | 0–100 | 100 | Amplitude multiplier on the rumble value |
| Frequency | 1–255 | 60 | Buzz frequency of the trigger effect (higher = finer/tighter buzz) |

*Guide:* enable **Both** with **Only While Pressed = on** for a subtle "feel the
rumble in your triggers only when you're using them" effect. With **off**, the
triggers buzz continuously whenever the game sends rumble. If a game drives its own
trigger effects, R2T yields to it by default (see *Force Override* below).

### Adaptive Triggers (Stage 1: resistance, Stage 2: push-back kick)
L2-gated constant resistance on R2 — hold the aim trigger and R2 stiffens. On top
of that, the push-back kick delivers recoil: while resistance is engaged, each
rumble/haptics burst momentarily switches the kicking trigger(s) to a low-frequency vibration thump (or a Bow-effect snap, see Kick style)
that knocks the trigger back against your finger, then resistance resumes as the
burst fades (hysteresis prevents chatter at the threshold).

| Setting | Range | Default | Notes |
|---|---|---|---|
The R2 and L2 triggers each have their own independent section in the portal
with the full set of settings below. The only shared control is **Kick follows**
(the kick's envelope source), since it's a single signal both triggers listen to.

| Setting (per trigger) | Range | Default | Notes |
|---|---|---|---|
| Mode | Off / Gated by trigger / Always / Gated by shoulder | Off | "Gated by trigger" = the OPPOSITE trigger arms it, analog (L2 arms R2, R2 arms L2). "Gated by shoulder" = the opposite bumper arms it, digital (L1→R2, R1→L2). Any R2/L2 combination is valid |
| Resistance strength | 0–100 | 70 | Resistance intensity (mapped to the effect's 0–7 range) |
| Arming threshold | 1–255 | 30 | In "Gated by trigger" mode, how far the arming trigger must be pulled (~12% at default). Ignored in shoulder-gated mode (digital on/off) |
| Start position | 0–9 | 0 | Trigger-travel zone where resistance begins (0 = from the start) |
| Resistance shape | Constant / Ramp / Two-stage / Weapon break | Constant | Constant = flat Strength A. **Ramp** = linear A→B across the pull (racing: light→heavy gas, heavy→light brake). **Two-stage detent** = Strength A with a wall of Strength B at the detent zone — a tactile bump marking half-press from full-press (fire/alt-fire games). **Weapon break** = rigid wall then hardware snap-through at the break point — the semi-auto shot break (Strength B unused) |
| Strength B | 0–100 | 70 | Second strength: the ramp's end value, or the detent wall |
| — Strength 0 in shapes | | | In Ramp/Two-stage, a strength of 0 means genuinely FREE travel (zone excluded): Ramp A=0 = free at rest building to B; Detent A=0 = a pure bump with free travel around it |
| Detent zone / break point | 0–9 | 5 | Two-stage: the wall's zone. Weapon break: the snap-through point (hw 3–8, forced above start) |
| Activation dead zone | 0–9 | 0 (off) | Below this zone the GAME sees the trigger untouched (no analog, no press bit) — the shot registers only past the zone. Aligns hair-trigger games with the resistance/detent/bow feel; internal effects always see the raw trigger. Not for analog gas/brake inputs |
| Push-back kick strength | 0–100 | 0 (off) | Recoil intensity; scales the thump amplitude with the vibration envelope. 0 = no kick on that trigger (pure resistance) |
| Kick style | Thump / Bow snap | Thump | Thump = vibration buzz (0x26). Bow snap = mechanical push-back via the Bow effect (0x22): the snap force presses the trigger back against the finger — sharper recoil, experimental (feel varies with hold depth) |
| Kick thump frequency | 10–200 | 35 | Vibration frequency of the kick; lower = heavier knock, higher = buzzier (R2T's default buzz is 60 for comparison) |

| Shared setting | Range | Default | Notes |
|---|---|---|---|
| Kick follows | Rumble / Audio / Both | Both | Envelope source for **both** triggers' kicks: game rumble (incl. converted DS4Windows rumble), the auto-haptics audio envelope, or the strongest of the two |

*Guide:* **L2-gated** gives a shooter-style "aim to feel the trigger tension"
effect without needing native adaptive-trigger support. Resistance wins over R2T
vibration while engaged, so you can run **R2T Both + AT L2-gated** together: the
triggers buzz with rumble normally, and R2 stiffens the moment you aim. For
recoil, start around **kick 60–80, frequency 35, source Both**: aim, fire, and
each shot thumps R2. *Audio* as a source means even games with zero rumble kick
on gunfire via the auto-haptics envelope. The diagnostics box shows the live
**push-back envelope (0–255)** and a **KICK** flag — the kick fires at envelope
≥ 32, so if the number stays 0 while the game rumbles, the selected source isn't
producing signal.

### Gyro-to-Stick
Maps controller motion onto the right stick for motion aiming.

| Setting | Range | Default | Notes |
|---|---|---|---|
| Gyro Mode | Off / L2-held / Always / Touch-enables / Ratchet | Off | When motion aiming is active (see below) |
| Sensitivity | 1–100 | 50 | Motion-to-stick gain (50 ≈ raw) |
| Horizontal Axis | Yaw / Roll | Yaw | Yaw = turn the controller; Roll = tilt it sideways |
| Invert | X / Y / both | off | Per-axis inversion (bit0 = X, bit1 = Y) |

*Gyro modes:*
- **L2-held** — aim only while L2 is held (flick-stick-style precision on ADS).
- **Always** — motion aiming on at all times.
- **Touch-enables** — active only while a finger is on the touchpad.
- **Ratchet** — always on, but touching the touchpad *pauses* it so you can
  reposition (like lifting a mouse).

*Guide:* **L2-held + Yaw** is the most natural starting point for shooters — turn
the controller to fine-tune aim only when aiming down sights. Raise sensitivity if
the motion feels sluggish; use invert if the direction feels backwards.

### Trigger effects — shared
| Setting | Range | Default | Notes |
|---|---|---|---|
| Force Override | on/off | off | on = force R2T/AT even when a game/app is sending its own trigger effects (off = yield to the game) |

### Device & Connection
| Setting | Range | Default | Notes |
|---|---|---|---|
| Polling Rate | 250 / 500 / Real-time | Real-time | USB report rate |
| Audio Buffer Length | 16–128 | 64 | Lower = snappier haptics/lower latency; higher = more audio stability |
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
- **Speaker crackle / pops — set the host audio device to 48 kHz.** The firmware's
  speaker audio pipeline (USB audio in, Opus over Bluetooth) runs at **48 kHz**. If
  the dongle's audio output device in **Windows Sound settings → device →
  Properties → Advanced → Default Format** is set to any other rate (44.1 kHz, 96
  kHz, etc.), Windows resamples to feed the 48 kHz endpoint and the rate mismatch
  causes continuous crackle at every frequency — regardless of volume, Bluetooth
  signal, or audio buffer length. Set the format to **16-bit or 24-bit, 48000 Hz**
  and the crackle clears. This is the fix for nearly all speaker-crackle reports.
  (Speaker audio over Bluetooth is inherently marginal — Sony disables it entirely
  on a stock DualSense over BT — so a trace of imperfection can remain even at
  48 kHz; muting the speaker in Mix mode and using haptics only avoids the pipeline
  altogether.)
- **Game crashes at launch when the automation runs (or when any window appears)
  — switch the game to Borderless.** Some games — especially older engines and
  remasters (e.g. Nightdive's KEX titles) — run in EXCLUSIVE fullscreen and
  mishandle the device-lost event that any focus loss triggers, crashing with a
  D3D "invalid call" / render-target error. The automation's profile window (or a
  console window from running `python ds5audio.py` manually — use `pythonw` to
  avoid one) is enough to trip it, which makes the crash look automation-related
  when the real fragility is the display mode. Diagnostic: launch the game bare
  and alt-tab — if it crashes the same way, it's the game. Fix: set the game to
  **Borderless/Windowed** — no exclusive mode switches, focus changes become
  harmless, and the automation works unmodified (borderless has effectively no
  performance cost on modern Windows).
- **Bluetooth vs USB latency.** Native haptics over Bluetooth are slightly less
  tight than over USB — this is inherent to the BT transport (slot scheduling vs
  USB's fixed microframes) and is not tunable away in firmware. A strong link
  (check the RSSI display) keeps it as good as it gets.
- **WebHID requires Chrome/Edge** and a secure context (download the portal file or
  serve from localhost). Firefox and Safari do not support WebHID.
- **Firmware upgrades preserve your settings and slots.** Config and profile
  slots carry across a normal reflash, and settings from older layouts are
  migrated automatically on first boot (fields that didn't exist yet take safe
  defaults). You only need `flash_nuke.uf2` if the config appears corrupted or a
  release explicitly calls for it. After a layout change it's still worth opening
  the portal to check any new settings and re-saving slots that use them.
- **Wake from sleep** depends on the host's sleep state. USB device remote-wakeup
  works from traditional S3 sleep; behavior under Modern Standby (S0 Low Power Idle)
  varies by system, and the device's "Allow this device to wake the computer"
  setting must be enabled in Windows Device Manager. Connecting from sleep can take
  a few extra seconds (the controller's Bluetooth is powered off during sleep and
  must re-establish on wake); some variability here is inherent to the Bluetooth
  reconnect path.
- **Wake and DS4Windows.** With wake enabled, the controller still disconnects
  cleanly from the host (and DS4Windows) whenever the PC is awake — turning the
  controller off no longer leaves a phantom USB device behind. The device is kept on
  the USB bus only while the PC is actually suspended, so a button press can wake it;
  once the PC is awake again, normal clean-disconnect behavior applies. (This
  resolves the earlier limitation where wake kept the device permanently on the bus.)
- **Wake and native haptics (Steam Input).** Enabling wake alters the USB descriptor
  (USB 2.1 + BOS descriptor + an added keyboard interface — all required by remote
  wakeup). Because the device then no longer matches a plain DualSense fingerprint,
  Steam Input may treat it as a generic/XInput pad: games like *Ratchet & Clank* can
  revert to Xbox-style rumble instead of native DualSense haptics, and speaker audio
  may stop. There is no way to keep wake's descriptor changes *and* the exact
  DualSense fingerprint Steam Input wants — they conflict by design. Additionally,
  toggling wake forces a USB re-enumeration that disrupts the portal connection, so
  applying a wake change through an auto-apply profile is unreliable. **Recommended:
  choose wake on or off once and leave it — don't switch it per-game.** If you rely
  on native haptics or the auto-apply profiles, keep wake **off**.
- **Hub-induced suspends.** A brief USB suspend caused by a flaky hub (while the host
  is awake) no longer powers off the controller. The power-off is debounced so only a
  sustained suspend — a real sleep or shutdown — powers the controller off; transient
  hub blips are ridden through. A deliberate "Reconnect USB" from the portal is also
  exempted, so saving settings that reconnect does not drop the controller.

---

## Building from source

Requires the Pico SDK (2.x) with the Pico 2W board support.

> **Important:** pin TinyUSB to 0.20.0 inside the SDK (`cd pico-sdk/lib/tinyusb && git checkout 0.20.0`). The SDK's bundled 0.18.0 fails to build the audio interface (`TUD_AUDIO_EP_SIZE`). Also run `git submodule update --init --recursive` so `lib/WDL` and `lib/opus` are present.

```sh
git clone https://github.com/awalol/DS5Dongle.git
cd DS5Dongle
git checkout v0.7.0
# apply the changes:
git apply /path/to/ds5dongle-v1.0.9.patch
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
- `src/bt.cpp` — BT flush timeout / QoS controls, RSSI signal strength readout, clean controller disconnect when the host is awake (clears DS4Windows even with wake on), suspend-aware connect/disconnect
- `src/config.h` / `src/config.cpp` — config fields, validation, defaults
- `src/cmd.cpp` — config field-ID read/write handlers, diagnostics, reboot-to-bootloader
- `src/main.cpp` / `src/state_mgr.h` — stuck-rumble fix (send state to the controller when it changes even while the speaker is active)
- `src/wake.cpp` / `src/wake.h` — USB suspend/wake hardening: debounced controller power-off (ride out hub-induced suspends), and a grace window so a deliberate USB reconnect is not treated as a host sleep (ported from upstream PR #186)
- `src/usb.cpp` — suspend-callback gate so the controller's Bluetooth is left alone on a USB suspend when wake is off

---

## Credits & license

This is a derivative of **[awalol/DS5Dongle](https://github.com/awalol/DS5Dongle)**,
which in turn builds on earlier community work on the DualSense dongle concept.

**Auto-haptics origin — thanks to [@loteran](https://github.com/loteran).** The audio-derived auto-haptics in this
project were inspired by **[@loteran](https://github.com/loteran)'s** earlier auto-haptics experiments on the
DS5Dongle. The single most important insight came from loteran: relocating
`state_set` to RAM, which is what allows the haptic actuators to fire at stock clock
speeds (150 MHz) instead of requiring an overclock. The DSP and supporting code here
were rewritten from scratch (carrier modulation, channel detection, the noise gate,
the DS4Windows handling, the effect leak, and the config protocol are new), but
loteran's groundwork is what made the whole feature possible. This release would not
exist without it — thank you.

Thanks also to the broader DS5Dongle contributors and to awalol for the complete
RAM relocation in v0.7.0 that keeps native haptics and controller audio working
without overclocking, and for the wake-on-PS-button implementation. (**[@awalol](https://github.com/awalol)**)

**Upstream fixes incorporated.** The stuck-rumble fix is based on **mik9's** (GitHub handle to confirm) upstream
"Fix stuck rumble" commit. The USB suspend/wake hardening (debounced power-off to ride
out hub-induced suspends, plus the reconnect grace window) is based on
**[@up2urheadlights](https://github.com/up2urheadlights)'** upstream pull request [#186](https://github.com/awalol/DS5Dongle/pull/186). Both were adapted to the v0.7.0 base
used here. Thank you.

Licensed under the **MIT License** — see [LICENSE](LICENSE). The original awalol
copyright notice is preserved as required.

---

## Files in this release

- `ds5-v1.13.3.uf2` — the firmware (flash this; reports version 1.13.2)
- `ds5-config-portal.html` — the web configuration portal (download and open)
- `flash_nuke.uf2` — config-reset utility (run before flashing if coming from a
  different config layout)
- `src/` — the modified source files
- `ds5dongle-v1.0.9.patch` — unified diff against awalol v0.7.0 (up to fw 1.0.9)
- `LICENSE` — MIT license
- `README.md` — this file (docs version 1.13.2)
- `CHANGELOG.md` — version history
- `automation/` — **optional** Playnite integration (see below)

## Optional: Playnite automation

The `automation/` folder adds hands-off Playnite integration: it auto-applies a
profile per game and routes game audio to the dongle for audio-driven auto-haptics,
switching automatically between native-haptics games and everything else.

Quick start:
1. Install Python 3 and `pip install pyaudiowpatch numpy`.
2. Run `automation\ds5-setup.bat` — it detects its own folder and generates the
   Playnite scripts with correct paths, then prints the exact lines to paste into
   Playnite's script settings.
3. Run `automation\ds5-policy.bat` once (self-elevates). It pre-grants the dongle
   to the profile pages via browser policy, so automated applies never wait for a
   Connect click and the grant survives browser restarts. Fully reversible with
   `ds5-policy-remove.bat` (Edge/Chrome show "Managed by your organization" while
   the policy is installed).
4. To fill in your native-haptics game list, launch each such game once and copy
   the exact names from `ds5-automation.log` (each launch logs `game: '...'`) into
   `native-games.txt` — no need to type them from memory.
   Per-game custom profiles are supported too: save a profile into a dongle
   slot in the portal and add a `game = slot 3` rule to `profile-overrides.txt`
   (or the file-based form, `game = file.html`, for exported profiles).
5. See `automation\AUTOMATION-README.md` for the full walkthrough.

This is entirely optional — the firmware and config portal work on their own without
it. The automation just removes the manual steps if you use Playnite.

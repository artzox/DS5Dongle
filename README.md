# DS5Dongle — Audio Auto-Haptics Edition

https://appendinocom.github.io/DS5Dongle-Gyro-Focused/

**Version 1.18.18**

A firmware modification for the [DS5Dongle](https://github.com/awalol/DS5Dongle)
(a Raspberry Pi Pico 2W-based wireless DualSense dongle) that adds **audio-derived
haptics** for games without native DualSense support, full **DS4Windows
compatibility**, **converted-rumble blending**, a **controller-speaker effect
leak** for added immersion, and **custom captured trigger effects** — record a
real adaptive-trigger effect from a game that has one and replay it in games that
don't — all configurable from a web-based portal.

> ⚠️ **Hardware requirement — an RP2350 board.** Two are supported:
>
> - **Raspberry Pi Pico 2 W** — the released `.uf2` is built for this board. Flash
>   it and you're done.
> - **Waveshare RP2350B-Plus-W** (USB-C, 16 MB flash, RM2 wireless) — a prebuilt
>   `ds5-v1.18.17-waveshare.uf2` now ships with each release; flash that and you're
>   done. It is built against pico-sdk 2.2.0, as this board requires.
>   *It has not yet been confirmed on hardware by anyone — if you have this board,
>   a report either way is very welcome.* To build it yourself instead, one command:
>
>   ```
>   Windows:  powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1 -Variant waveshare
>   macOS:    ./tools/build-macos.sh --waveshare
>   ```
>
>   These fetch the right toolchain and SDK for you and write
>   `ds5-bridge-waveshare.uf2` into its own build directory. **This board needs
>   pico-sdk 2.2.0**, newer than the 2.1.1 used for the Pico 2 W build — and the
>   difference matters: against 2.1.1 this target still *compiles*, but the
>   resulting firmware does not work properly, because the RM2 wireless needs
>   2.2.0. If you build by hand with `boards/build_waveshare_rp2350b_plus_w.sh`,
>   it now checks your SDK version and refuses rather than handing you a
>   silently broken build.
>
> The original **Raspberry Pi Pico W** (RP2040) is **not** supported — it is a
> different chip (Cortex-M0+ vs Cortex-M33, less memory, no FPU), so a binary built
> for one cannot run on the other, and this firmware's floating-point audio DSP
> (auto-haptics, the effect leak, resampling) is designed around the RP2350's FPU
> and larger RAM. Check which board you have before flashing.

Built on **awalol/DS5Dongle v0.7.0**, which relocates the entire BT/USB/audio path
to RAM so native fine haptics and controller audio work without overclocking.

---

## Contents

- [What this adds over the base firmware](#what-this-adds-over-the-base-firmware)
- [Quick start](#quick-start)
- [Native-haptics games — required setup](#native-haptics-games--required-setup)
- [Suggested setup](#suggested-setup)
- [Configuration reference](#configuration-reference)
  - [Auto-Haptics & Speaker Effect Leak](#auto-haptics--speaker-effect-leak)
  - [General Haptics & Audio](#general-haptics--audio)
  - [Trigger-to-Rumble (R2T)](#trigger-to-rumble-r2t)
  - [Adaptive Triggers (Stage 1: resistance, Stage 2: push-back kick)](#adaptive-triggers-stage-1-resistance-stage-2-push-back-kick)
  - [Custom Captured Effects (new in 1.14.0)](#custom-captured-effects-new-in-1140)
  - [Trigger effects — shared](#trigger-effects--shared)
  - [Gyro-to-Stick](#gyro-to-stick)
  - [Device & Connection](#device--connection)
  - [Advanced — BT Latency (experimental)](#advanced--bt-latency-experimental)
- [Modes explained](#modes-explained)
- [Notes & known behavior](#notes--known-behavior)
- [Building from source](#building-from-source)
  - [Modified files](#modified-files)
- [Credits & license](#credits--license)
- [Keeping up to date](#keeping-up-to-date)
- [Files in this release](#files-in-this-release)
- [Optional: Playnite automation](#optional-playnite-automation)
- [Glossary](#glossary)
  - [Haptics and audio](#haptics-and-audio)
  - [Trigger effects](#trigger-effects)
  - [Device and connection](#device-and-connection)

---

## What this adds over the base firmware

- **Audio-derived auto-haptics** — generates haptic feedback from game audio for
  titles that have no native DualSense haptics. Works over Bluetooth.
- **Extensive DSP tuning** — intensity, smoothness, noise gate, crossover cutoff,
  and selectable filter slope (6/12/24 dB/oct) to get your auto-haptics as close as native as possible.
- **Three operating modes** — Off (native passthrough), Mix
  (native + derived), Replace (derived only)
- **DS4Windows compatibility** — auto-haptics keep working under DS4Windows in
  passthrough, DualShock 4, and Xbox 360 emulation modes (the base firmware's
  rumble handling silenced the haptic actuators; this fixes it).
- **Converted-rumble blending** — in Mix mode a game's rumble (whether from
  DS4Windows emulation or a native title sending motor values) is re-created on
  the actuators and blended with the audio haptics, with independent strength.
  The heavy and light motors are rendered at their own frequencies (60 Hz and
  160 Hz) and the rumble is injected after the limiter, so it keeps full
  authority instead of being squeezed out when the derived haptics are loud.
- **Effect leak** — instead of fully muting the controller speaker, optionally
  pass sharp transient effects (shots, clinks, impacts) through the speaker via
  transient detection, so discrete effects come through while sustained dialog and
  music stay muted — like native DualSense games.
- **Independent routing for every audio source** — "eat your cake and have it too"   for native dualsense games + auto-haptics mix mode. The derived haptics can be
  generated from either channel pair, the native passthrough can be filtered or
  passed raw, and the speaker and effect leak always stay on ch0/1. That makes
  it possible to run auto-haptics from the capture script while the controller
  speaker plays only the game's own native effects, or to add auto-haptics to a
  game that renders its own haptics without either one destroying the other.
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
- **Custom captured effects (new in 1.14.0)** — capture the *actual* adaptive-trigger
  effects a game sends (weapon walls, resistances, vibrations) and replay them on
  either trigger in games that have none. Effects are stored as the exact bytes the
  game sent — nothing is decoded into sliders, so the feel is the original one.
  Mechanical states are placed by the trigger positions encoded in their own bytes
  and played **in sequence as you pull** (e.g. wall → wall → end-resistance);
  vibrations can be captured **with their timing** and replayed to that rhythm.
  Effects can be saved and shared as JSON files.
- **Manual effect builder** — author trigger effects by hand when there is
  nothing to capture: stack weapon-break walls, bow snap-backs and vibrations,
  set their trigger zones and strengths, and the portal encodes them into the
  same bytes the firmware's own writers emit — so a hand-built effect is
  byte-identical to one a game would have sent. Built effects live alongside
  captured ones and save to the same JSON files.
- **Profile slots** — up to 24 complete configurations stored on the dongle
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
- **Slot backup and restore** — download every stored profile slot, names
  included, as one JSON file and write them all back later. Protects hard-won
  tuning against a flash wipe or a bad flash.
- **Wake from sleep with no phantom controller** — see below; the bridge keeps
  a wakeable presence on USB while the controller is off, under a separate USB
  identity so nothing appears as a controller that is not there.
- **Lightbar control**, **live RSSI / signal display**, **reboot-to-bootloader**,
  and **experimental BT latency controls** (flush timeout, QoS).
- **Live diagnostics** — signal strength, battery, audio stream state and
  channel count, push-back envelope, the rumble a game is actually sending
  (peak-held, with the rumble flags it requested), and a wake report covering
  suspends, resumes, wake attempts and the current USB identity.
- **Sectioned web configuration portal** with smart save (only reconnects when a
  setting that requires re-enumeration changed).

The **Wake PC on PS Button** feature (USB remote wakeup) originates in the awalol
v0.7.0 base and is exposed here as a portal toggle, substantially reworked. It is
**off by default**; enable it only if you want the controller's PS button to wake
the PC from sleep.

With wake enabled the bridge **stays on the USB bus whenever the controller is
switched off** — it has to, or it could never tell that the PC went to sleep — but
it does so under a **separate USB identity**, so nothing appears as a controller
that is not there. The details, and what you will see in Windows, are in the callout
below.

> **Wake is a per-profile setting — on for auto-haptics games, off for native
> ones.** Enabling wake changes the controller's USB descriptor (it advertises USB
> 2.1 with a BOS descriptor and adds a keyboard interface, which the wake mechanism
> requires). The altered descriptor no longer looks like a "pure" DualSense, so
> **games with native DualSense support can stop recognising it** — a game such as
> *Ratchet & Clank* may fall back to Xbox-style rumble instead of native haptics,
> and the controller's speaker audio can stop working.
>
> Because wake is one of the settings that requires re-enumeration, a profile
> that switches it takes the dongle off USB for a second or two. Anything that
> talks to the dongle right afterwards has to wait for it to come back — the
> Playnite automation handles this, see `automation/AUTOMATION-README.md`.
>
> That only matters for games that use the controller natively. For **non-native
> games driven by an auto-haptics profile**, nothing is relying on native
> recognition — the haptics are synthesised from game audio — so wake can safely
> stay **on** there.
>
> With wake **on**, the dongle stays present on USB even when the controller is
> switched off — it has to, or there would be nothing on the bus for the PC to
> suspend, and it could never tell the PC had slept. **This is what lets the PS
> button wake the PC even when you switched the controller off before putting the
> PC to sleep** — the usual case, and the one that fails outright if the dongle
> leaves the bus. In that state it appears under a **different USB identity** —
> different vendor and product IDs and its own name — so no controller shows up in
> DS4Windows or anywhere else; its normal identity returns when the controller
> reconnects.
>
> **What you will see while the controller is off.** Windows treats the idle
> identity as its own device, listed as **DS5Dongle (controller off)**. It is
> visible in `joy.cpl` (Game Controllers) and under **Settings → Bluetooth &
> devices → Devices**. This is expected and is not a phantom controller: it is
> the device the PC will be woken from. It also has its own *Allow this device to
> wake the computer* setting — usually granted automatically, and the portal's
> wake diagnostics will tell you if it was not.
>
> **First appearance switches your audio output.** Because the idle identity is a
> new device carrying the same audio interfaces, Windows discovers it as a fresh
> sound device and, by default, makes it the active output the first time it
> appears — so PC audio goes silent (or to the wrong endpoint) until you switch
> back. Set your normal output device again in the volume flyout or Sound
> settings; Windows remembers the choice, so this is a one-time correction rather
> than something that recurs.
>
> **DS4Windows does not pick it up, by design.** The idle identity does not match
> the controller IDs DS4Windows looks for, so it is not captured — which means
> DS4Windows stays free to auto-load profiles for any other controller you connect
> while the DualSense is off. If you would rather not see it at all, it can be
> hidden with **HidHide** like any other device, and it then disappears from games
> as well. Hiding it does not affect waking, which is handled by Windows at the USB
> level rather than by anything that reads the device.
>
> With wake **off** the dongle removes itself from USB entirely when the
> controller disconnects.
>
> `Wake PC on PS Button` is an ordinary configuration field, so every profile and
> every on-dongle slot carries its own value. The practical setup is:
>
> - **auto-haptics profiles → wake on**
> - **native-game profiles → wake off**
>
> and the automation switches it per game along with everything else.
>
> One caveat about *how* the switch is applied. Changing wake forces a USB
> re-enumeration. **Slot activation** (`slot N` in `profile-overrides.txt`) sends a
> single command and the firmware applies the whole configuration at once, which
> handles this cleanly. A **field-by-field `.html` profile** applies settings one at
> a time over the live connection, so a re-enumeration mid-apply can interrupt it —
> prefer slots for any profile that changes wake.

---

## Quick start

1. **Flash the firmware.** *(RP2350 only — Pico 2 W and Waveshare RP2350B-Plus-W
   each have their own prebuilt firmware, or build it yourself; this will not run
   on the original Pico W.)* Hold the BOOTSEL button while plugging in the board
   (or triple-click BOOTSEL on an already-running unit), then copy
   `ds5-v1.18.17.uf2` (Pico 2 W) or `ds5-v1.18.17-waveshare.uf2` (Waveshare) to the
   `RPI-RP2` drive that appears.
   - **You do not normally need `flash_nuke.uf2`** (the one supplied is built for
     the Pico 2 W). Settings and saved profile
     slots survive an upgrade — new options are appended to the stored layout, so
     old values keep their meaning and anything new lands on its default. Only
     run it if a release note explicitly says to, or if the portal shows settings
     that are clearly nonsense. **It erases every setting *and* all 24 profile
     slots**, so back up your slots first (*Slots* tab → *Back up all slots*).
2. **Open the portal.** **Download** `ds5-config-portal.html` and open the
   downloaded file in Chrome or Edge. (WebHID needs a secure context — opening it
   directly from a website host or `file://` that the browser flags will fail with
   a permissions error. Downloading it and opening the local file works, as does
   serving it from `http://localhost`.)
3. **Connect.** Click *Connect* and select the DualSense.
4. **Decide what you need — this determines whether there is anything left to do.**

   - **Playing games with native DualSense support?** Leave *Auto Haptics Mode* on
     **Off**, click *Save to Device*, and **you're finished.** Native games drive
     the controller's haptics themselves and the dongle passes them straight
     through — no Python, no helper, nothing else to install. **Native haptics also
     need two settings outside this project** — see
     [Native-haptics games — required setup](#native-haptics-games--required-setup).
   - **Want haptics in games *without* native support?** (Most games.) Set *Auto
     Haptics Mode* to **Mix** or **Replace**, tune to taste, *Save to Device* —
     then continue to step 5.

5. **Mix / Replace only — install and run the audio bridge.** Auto-haptics are
   *derived from your PC's game audio*, so a small helper has to capture that audio
   and feed it to the dongle. Windows only, since it uses WASAPI:

   1. **Install Python 3** from [python.org](https://www.python.org/downloads/) —
      tick **"Add python.exe to PATH"** in the installer.
   2. **Install its one dependency:**

      ```
      pip install PyAudioWPatch
      ```

   3. **Run it, and leave it running while you play:**

      ```
      python automation/ds5audio.py
      ```

   No device flags are needed for most setups — it finds the DualSense on its own
   (`--list`, `--in-index` and `--out-index` are there for when it picks wrong).

> **In Mix or Replace the audio bridge is not optional — and it is not part of the
> Playnite automation.** Without `ds5audio.py` running there is no audio for the
> firmware to derive from, so auto-haptics will appear to do nothing however they
> are tuned. That is the single most common reason auto-haptics "doesn't work". In
> **Off** (native) mode it isn't needed at all.

The firmware ships with safe stock defaults (auto-haptics off, standard buffer), so
a fresh flash is safe and native passthrough works immediately. For a tuned
auto-haptics + immersion setup, see **Suggested setup** below.

---

## Native-haptics games — required setup

Games with native DualSense support drive the controller themselves and the dongle
passes their haptics straight through. Whether that actually happens is decided by
two things outside this project, and both catch people out.

**1. Steam: set the controller override to *Disabled* for that game.**
Right-click the game in Steam → *Properties* → *Controller*, and set the override
to **Disabled**. Not *Default*, and not *Enabled* — either of those leaves Steam
Input in front of the controller, the game stops seeing a real DualSense, and
native haptics never arrive at all.

**2. DS4Windows may have to be closed.**
If you use DS4Windows to present an Xbox 360 or DS4 virtual pad, Steam can pick up
that virtual controller instead of the DualSense and refuse to treat the game as
native. Some titles need DS4Windows **closed entirely before launch** — *Doom: The
Dark Ages* is one. If a game that should have native haptics gives you nothing,
close DS4Windows and relaunch it before changing any other setting.

> You can stop the second problem happening at all by hiding the DualSense from
> everything except your native games, so the two worlds never collide. See
> **Mixing native DualSense and virtual controllers** in
> `automation/AUTOMATION-README.md`.

Also worth knowing for native games: keep **Wake PC on PS Button** *off* in those
profiles — see the wake note above for why.

## Suggested setup

These are good starting values for using auto-haptics with audio passthrough and
the effect leak, tuned on a real DualSense over Bluetooth. They are *suggestions* —
adjust to taste — but they give a working, balanced configuration without trial and
error. (The firmware does not ship with these as defaults, so there's no flashing
surprise; apply them in the portal and save.)

**Auto-Haptics & Speaker Effect Leak**
| Setting | Value |
|---|---|
| Mode | Off (switch to Mix or Replace per game) |
| Intensity (%) | 80 Scales the DERIVED auto-haptics only. Native passthrough and converted rumble have their own levels (see ds5audio `--map` note) |
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
| Converted Rumble Strength (Mix) | 50 (range goes to 200; left/heavy renders at 60 Hz, right/light at 160 Hz) |
| Effect Leak Volume (0=off) | 0 (raise to enable) |
| Effect Leak Sensitivity | 50 |
| Effect Leak Decay/Fade-out | 80 |
| Effect Leak Attack/Responsiveness | 50 |
| Effect Leak Output High-pass (Hz) | 1000 |
| Effect Leak Output Low-pass (Hz) | 8000 |
| Effect Leak Gate Hold (x5 ms) | 20 |
| Effect Leak Max Burst (x5 ms) | 0 (off; try 30) |
| Effect Leak Detection Band (Hz) | 2500 |

**General Haptics & Audio**
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
| Wake PC on PS Button | On for auto-haptics profiles, off for native games |

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

### Auto-Haptics & Speaker Effect Leak
| Setting | Range | Default | Notes |
|---|---|---|---|
| Mode | Off / Mix / Replace | Off | Off = native/rumble passthrough; Mix = native + derived; Replace = derived only |
| Intensity (%) | 0–200 | 100 | Strength of the audio-derived haptics only (curved response). It does NOT scale the native passthrough or converted rumble — those have their own levels, so the three components are set independently |
| Smoothness | 0–100 | 40 | Higher = smoother/longer decay; lower = snappier |
| Noise Gate | 0–100 | 20 | Suppresses quiet content (dialog/ambience) below a threshold |
| Native Passthrough in Mix (%) | 0–100 | 100 | MIX MODE ONLY: level of the native ch3/4 haptic stream mixed under the derived haptics. See "Choosing the passthrough level" below |
| Filter Native Passthrough in Mix | Filtered / Raw | Filtered | MIX MODE ONLY: whether the ch3/4 passthrough is low-passed at the LP Cutoff before mixing. See "Filtered or Raw?" below |
| Auto-Haptics DSP Source | ch0/1 / ch2/3 | ch0/1 | Which channel pair the derived haptics are generated from. The speaker and effect leak always read ch0/1. See "Separating the script feed from game audio" below |
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

#### Filtered or Raw? (Filter Native Passthrough in Mix)

The passthrough has always been low-passed at the LP Cutoff before mixing. That
is right for a **duplicate** feed — in ds5audio's default mapping (and in
VoiceMeeter-style routings) ch3/4 carry a copy of the full-band stereo, and
passing that raw sends dialogue and treble straight to the actuators. It is
wrong for a game that renders its **own** haptics, because real haptic content
lives well above a typical 80 Hz cutoff: filtering removes exactly the effects
the passthrough exists to carry, and no amount of passthrough level brings them
back.

| Your ch3/4 carries | Set to | Why |
|---|---|---|
| A game's own native haptics | **Raw** | Keeps the game's effects intact. Pair with `--map front` so the script does not also write to ch3/4 |
| A duplicate of the game audio (default `--map duplicate`) | **Filtered** | Blocks dialogue/treble leaking to the actuators. Usually better still: set passthrough level to 0 |
| The script feed, because DSP Source is ch2/3 | **Filtered** (or level 0) | ch3/4 is raw captured audio here, not haptics — see below |

The derived haptics are always filtered by the LP Cutoff, split and gate
regardless of this setting; it applies only to the passthrough component.

#### Separating the script feed from game audio (Auto-Haptics DSP Source)

The speaker and the effect leak always read **ch0/1**. With the default DSP
source that pair must also carry the script feed the derived haptics are
generated from — so anything ds5audio sends is also what the speaker plays and
what the leak passes. For a game that outputs **its own audio effects through
the controller speaker** (Ratchet & Clank: Rift Apart is the obvious example,
with its weapon and gadget sounds sent to the pad), that means the game's
speaker effects arrive mixed with the whole captured PC soundtrack, and there
is no way to hear only the former.

Setting DSP Source to **ch2/3** breaks that coupling:

```
ds5audio.py --map rear      # script capture -> ch2/3 only, ch0/1 left clear
```

| Component | Reads | Result |
|---|---|---|
| Derived auto-haptics | ch2/3 | Generated from the script feed, as before |
| Controller speaker | ch0/1 | Only the game's own native speaker audio |
| Effect leak | ch0/1 | Only the game's native effects pass through |

Set **Native Passthrough to 0** in this configuration: ch3/4 now carries raw
script audio rather than native haptics. On a 2-channel stream the DSP falls
back to ch0/1 automatically.

#### Which setup for which game

| Game type | ds5audio `--map` | Mode | DSP Source | Passthrough | Filter | Rumble |
|---|---|---|---|---|---|---|
| Native haptics (e.g. Returnal, Ragnarök) | `front` | Mix | ch0/1 | 100 | **Raw** | as taste |
| Native speaker audio you want isolated (e.g. Ratchet & Clank) | `rear` | Mix | **ch2/3** | 0 | (n/a) | as taste |
| Non-native, DS4Windows / XB360 | `duplicate` | Mix | ch0/1 | 0 | (n/a at 0) | 50–100 |
| Native haptics, no augmentation wanted | any | Off | — | (full) | (n/a) | (native) |

Every one of these is an ordinary configuration field, so each row can live in
its own dongle slot and be selected per game — but note the `--map` value is a
ds5audio argument, not a dongle setting, so profiles that need a different
mapping also need the matching launch argument. With the optional Playnite
automation this is handled per game in `profile-overrides.txt`, which passes
anything after a further comma to the capture script:

```
Resident Evil 4 = slot 5, audio, --map front
Ratchet = slot 6, audio, --map rear
```

A game listed in `native-games.txt` needs the explicit `, audio` shown above,
or the capture is skipped — the native list excludes exactly those games by
default. See `automation/AUTOMATION-README.md`.

#### Is the game even sending rumble? (Diagnostics)

The Device tab reports **Rumble from host** — the peak motor values seen since
the last read, plus which rumble flags the game requested. Non-zero while a
game vibrates means it sends motor values, which Mix re-creates through
Converted Rumble Strength. Zeros while you can still feel vibration with
auto-haptics Off mean the game delivers its vibration as haptic audio on ch3/4
instead, which is the passthrough's job — set the filter to **Raw**. The values
are peak-held, so a short burst between polls still registers.

Continuing the **Auto-Haptics & Speaker Effect Leak** settings:

| Setting | Range | Default | Notes |
|---|---|---|---|
| Auto-mute Speaker (Replace) | on/off | on | Mute controller speaker in Replace mode |
| Auto-mute Speaker (Mix) | on/off | off | Mute controller speaker in Mix mode |
| Lightbar Off in Replace Mode | on/off | off | Kills the lightbar glow in Replace (e.g. blue in Xbox360 mode) |
| Converted Rumble Strength (Mix) | 0–200 | 50 | Strength of a game's rumble re-created on the actuators in Mix mode. Left/heavy renders at 60 Hz, right/light at 160 Hz. Above 100 deliberately overdrives into the limiter for games whose motor values sit low |
| Effect Leak Volume | 0–100 | 0 (off) | Volume of the transient effect leak through the speaker when auto-muted |
| Effect Leak Sensitivity | 0–100 | 50 | How sudden a level jump counts as an effect (higher = more leaks through) |
| Effect Leak Decay/Fade-out | 0–100 | 40 | How gradually effects fade after triggering (~50 ms .. 500 ms) |
| Effect Leak Attack/Responsiveness | 0–100 | 50 | How fast the gate opens (higher = more immediate, less delay) |
| Effect Leak Output High-pass (Hz) | 50–2000 | 200 | Low wall of the leak window (12 dB/oct) — removes deep bass that pops the speaker |
| Effect Leak Output Low-pass (Hz) | 500–12000 | 8000 | High wall of the leak window (12 dB/oct) — cuts treble sizzle/crackle. With the high-pass forms a band-pass "capture window": only sound inside it leaks. Automatic make-up gain keeps loudness constant as you move the walls |
| Effect Leak Gate Hold (×5 ms) | 0–100 | 20 (100 ms) | Minimum gate-open time per transient + hysteresis; stops the gate chattering (the "choppy/poppy" leak artifact) |
| Effect Leak Max Burst (×5 ms) | 0–100 | 0 (off) | MAXIMUM gate-open time: cuts sustained sounds (dialogue, music) at the cap with a no-retrigger refractory — one short accent instead of duplicating the room audio; shots end within the cap naturally. Try 30 (150 ms) and raise leak volume: the leak becomes punctuation, not a second speaker |
| Effect Leak Detection Band (Hz) | 100–5000 | 800 | Frequency band the transient detector listens to |

#### How auto-haptics works (brief)

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

### General Haptics & Audio
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

R2 and L2 are shown side by side in the portal, as two aligned columns of the
same settings — the list below applies to each. The one exception is **Kick
follows** (the kick's envelope source): it is a single signal both triggers
listen to, so it sits in its own full-width row beneath the columns rather than
in either one.

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
| Kick follows | Rumble / Audio / Both | Both | Envelope source for **both** triggers' kicks: game rumble (incl. converted DS4Windows rumble), the auto-haptics audio envelope, or the strongest of the two. **Unless the trigger is gated, prefer *Rumble* only** — otherwise the generated haptics keep the audio envelope alive and the trigger moves almost constantly |

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

### Custom Captured Effects (new in 1.14.0)

Capture a real adaptive-trigger effect from a game that has one, and replay it on
any trigger in a game that doesn't. Effects are stored as the exact 11-byte
commands the game sent — they are **not** decoded into the sliders above (a game's
force curves don't survive that round-trip), so what you feel is the original
effect, not an approximation.

**What can and can't be captured**

- ✅ **Genuine game-sent adaptive-trigger effects** — games with native DualSense
  trigger support (*Ratchet & Clank*, *God of War Ragnarök*, *Returnal*,
  *Indiana Jones*…).
- ❌ **Firmware-converted rumble.** For example *Control*'s trigger buzz is this
  firmware's own R2T conversion of the game's rumble, not a trigger effect the game
  sent — there is nothing to capture.
- ❌ **DS4Windows / Xbox 360 emulated rumble** — same reason.

If the monitor stays empty after you perform the action in-game, that game isn't
sending trigger effects.

**1. Capture — Trigger Effect Monitor**

1. In the game, perform the trigger action (pull the weapon trigger, hold the
   spear…).
2. Alt-tab to the portal → **Triggers** tab → **Trigger Effect Monitor** → **Refresh captured
   effects**.
3. The last few *distinct* effects each trigger received are listed, labelled by
   type (Resistance / Weapon break / Vibration) with their raw bytes.
4. Tick the states that make up the action (up to 5) → **Assign ticked → custom
   effect**.

Enable and state count are set for you; **state count is never set by hand**.

**2. How this differs from the trigger sliders above**

The two systems work on completely different principles, which is why they are
mutually exclusive on a trigger:

| | Trigger sliders (Adaptive Triggers) | Custom captured effects |
|---|---|---|
| Where the effect comes from | **Synthesised** from your parameters (strength, shape, start position, detent) | **Replayed** from stored raw effect bytes |
| How many effects | **One**, continuously recomposed | **Up to 5 states**, one active at a time |
| What changes the feel | Game **rumble** and **audio** (kick, R2T, auto-haptics) plus trigger position | **Trigger position only** |
| Editable | Yes — every parameter is a slider | No — the bytes are fixed (but you can build new ones) |
| Multi-stage pulls | Not possible (one effect slot; the detent gives one bump, not a second wall) | Yes — that is the point |

The sliders are a *live synthesiser*: they rebuild one effect every cycle in
response to what the game is doing, which is why they can react to rumble and
audio. Custom effects are a *player*: the states are fixed, and the only thing
that decides which one is active is where your finger is on the trigger. Neither
can do the other's job — the sliders can't stage two walls in one pull, and a
captured effect can't respond to a game explosion.

**3. How assigned effects replay** — the mode is chosen automatically by type:

- **Mechanical states (Resistance, Weapon break, Bow/snap) → positional sequencing.** States
  are ordered by the trigger positions encoded in their own bytes and played in
  sequence as you pull, each stage arming just before your finger reaches it. Tick
  order doesn't matter. This reproduces a full action such as *Ratchet & Clank*'s
  wall at ~30% → wall at ~50% → light end-resistance in the rapid-fire hold.
- **Vibration states → time-based.** A 2-state pair blends A↔B at the toggle rate;
  a timeline recording (below) replays the recorded rhythm instead.

**How far each stage runs.** A stage hands over to the next one shortly *before*
the next stage's region begins, so the next effect is always armed ahead of your
finger rather than snapping into place under it. The consequence is worth knowing
when you design a sequence:

- **Stages that don't overlap** — e.g. zones 1-3, then 4-7, then 8-9 — each play
  their **whole span**, including the last. Any clean gap between one stage's end
  and the next stage's start is enough.
- **Stages that overlap** — e.g. zones 1-3, then 2-4 — are **cut short**, because
  the hand-over point falls inside the earlier stage. A bow whose draw is cut
  before its end never reaches its snap, which softens the ending. That can be
  exactly what you want, but it is easy to hit by accident: the builder warns
  whenever a stage ends up with less than 40 of the trigger's 255 counts.

So there are three distinct replay behaviours, picked for you:

1. **One state** — asserted continuously while the condition holds.
2. **Mechanical sequence** (2–5 states, at least one force effect — wall,
   resistance or bow) — ordered
   by trigger position and stepped through as you pull. Recorded durations are
   ignored; position is the only clock. A vibration may be mixed in as one of the
   stages, and it takes over at its own depth.
3. **Vibration pair or timeline** (all states are vibrations) — driven by *time*,
   either the A↔B toggle rate or the recorded per-state durations.

> **A multi-stage effect only sequences under the *While held* condition.** Stages
> are stepped through by trigger position, which needs the effect armed for the
> whole pull. Set that trigger to *On press* or *On release* and you get a single
> one-shot of the first stage instead — the later stages simply never play, with
> no error to tell you why. Assigning from the **builder** therefore sets the
> condition to *While held* (and the zone to 0) for you; assigning from the
> monitor, a timeline or a file leaves the condition as you have it, so check it
> if you are building a sequence that way.

**4. Timeline recording — for vibration rhythm**

The monitor's history carries no timing. When a vibration's character *is* its
rhythm (switch-then-hold, pulse patterns), use **Timeline recording** (Triggers
tab, under the effect monitor): press
**● Record**, perform the action in-game, then **Stop & fetch**. Every state change
is listed with the milliseconds it was held; tick the loopable core (up to 5) and
**Assign ticked timeline**. Replay holds each state for its recorded duration and
loops — no rate dialling needed.

Durations are **ignored for mechanical states** (a wall's recorded duration is just
how long you happened to hold it, not part of the effect), so walls and resistances
always sequence positionally even if you assign them from a timeline or load a file
that contains durations.

**5. Getting a custom VIBRATION to work**

This is where the **trigger condition** matters:

- **The reliable recipe is a one-shot: condition On press or On release, with a
  zone.** Set the zone to where in the pull you want it — e.g. **zone 9** gives a
  late-stage kick right at the bottom of the travel, zone 2–3 fires it early. The
  **zone must not be 0** (see below). In practice this does not feel like a brief
  blip: once the trigger is past the zone the buzz continues until you physically
  release it, so "On press + zone 9" gives a **buzz through the deep part of the
  pull** — which is usually what you want from a captured vibration.
- **Sustained hold** (e.g. the *God of War Ragnarök* spear): condition **While
  held**, assigning the sustained state **alone** — don't assign the whole
  timeline, because its long duration is only how long you held the trigger while
  recording. Note that a vibration does not keep buzzing from a single command;
  the firmware re-sends it periodically to sustain it. If a held vibration feels
  weak or intermittent, use a one-shot condition instead.
- **Firing kick or release pop**: condition **On press** or **On release**, and the
  **zone must not be 0**. In those modes the zone is the *engage point* and the
  effect fires as a short one-shot when the trigger crosses it — at zone 0 the
  trigger is always already "past" the point, so the crossing never happens and
  **nothing will ever fire**. Use zone 1–9 (roughly 10–90% of the pull).
- A vibration asserted while the trigger is held **at full travel** can buzz
  mechanically (a fully depressed trigger resonates against the controller body).
  This is another reason to prefer a one-shot condition over a sustained
  full-pull buzz.
- A vibration's own zone bytes do **not** restrict where it buzzes — the effect
  runs whenever it is active, regardless of trigger position. Position control
  comes from the **zone + condition** settings, or from placing the vibration as a
  stage in a built multi-stage effect (below).

**Settings (per trigger)**

| Setting | Range | Default | Notes |
|---|---|---|---|
| Custom captured effect — enable | Off / On | Off | On = the custom effect **owns** this trigger's effect output (see below) |
| Custom effect trigger condition | While held / On press / On release | While held | *While held* = armed continuously — **required for a multi-stage effect to sequence**, and used for a sustained vibration. *On press* / *On release* = a one-shot as the trigger crosses the zone, so only the first stage of a sequence would play |
| Custom effect zone | 0–9 | 0 | *While held:* the **re-arm zone** — walls reset when the trigger returns below it; **0 = only at full release** (finger off, nothing to push against, no click). *On press / On release:* the **engage point — must be ≥ 1** |
| Custom effect A↔B toggle rate | 1–100 | 40 | **Only** for 2-state vibration actions (~2–40 Hz). Ignored for mechanical and timeline effects |
| Custom effect state count | 1–5 | — | **Auto-set** by Assign / file load; never set manually |
| Custom effect vs sliders | 3 options | Custom effect always owns | Shares the trigger with the slider stack, gated by the opposite trigger or shoulder button — see *Sharing a trigger with the sliders* below |

**Sharing a trigger with the sliders (gate hand-off)**

By default a custom effect owns its trigger outright. The **Custom effect vs
sliders** setting lets the two share instead. The gate is the physical gate
input — the **opposite trigger** pressed past that trigger's threshold, or the
**shoulder button** if its resistance mode is the shoulder-gated one:

| Setting | Gate engaged | Gate not engaged |
|---|---|---|
| Custom effect always owns (default) | custom effect | custom effect |
| Sliders while gated, custom otherwise | synthesized slider effect | custom effect |
| Custom effect while gated, sliders otherwise | custom effect | synthesized slider effect |

With R2's resistance set to *L2-gated*, the middle option gives the classic
split: hip fire plays your captured multi-stage weapon effect, and the moment L2
passes its threshold R2 switches to the synthesized resistance, kick and all. The
third option inverts it — aim down sights to get the captured effect.

Notes:
- Only one effect plays at a time; this is a **switch**, not a layer.
- The swap happens live, so changing the gate while the trigger is already held
  changes the feel under your finger.
- The slider side needs a resistance mode that is **armed in the half it
  covers** — the portal warns inline if the combination would leave one half
  silent, naming the setting to change. For *sliders while gated*, a gated resistance mode lines up exactly.
  For *custom effect while gated*, set the resistance to **always on** — the
  custom effect takes over while the gate is held, and the sliders cover the rest.
- **Force has eight levels, not a hundred.** It is a 3-bit field, so a strength
  percentage snaps to the nearest of eight steps — 10% and 21% both store level 1,
  which reads back as 14%. The builder shows the level as you type. Level 0 is the
  weakest and may not be felt at all.
- **Seeing what is on a trigger:** the same panel has *Read L2 / Read R2*, which
  decodes the effect currently stored there — each state in plain language, its raw
  bytes, and the position window it will actually be active for. Use it to check a
  captured or loaded effect, and to spot a stage that hands over before its own
  span finishes (a wall that never breaks).
- **Removing an effect:** the Build a Custom Effect panel (Triggers tab) has *Remove effect from
  R2 / L2*, which wipes that trigger's stored states, switches the custom effect
  off and returns the trigger to its sliders. Everything else on that trigger —
  including this hand-off setting — is left as you set it. Save to a slot
  afterwards to make it stick for that profile.
- Silence on either side is just that side configured off (resistance off, kick
  0, R2T off), so "captured effect while aiming, nothing otherwise" is a valid
  setup.

**Recipe: a custom effect *and* Rumble → Trigger on the same trigger**

Only one effect can occupy a trigger at a time, so a custom effect and the
rumble-driven vibration cannot both be present at once. They can **take turns**,
though, and the hand-off is what schedules it. The result is a trigger that
resists while you aim and buzzes when the shot lands:

| Setting | Value |
|---|---|
| Custom effect on L2 | your resistance, enabled |
| **Custom effect vs sliders on L2** | **Sliders while gated, custom effect otherwise** |
| L2 resistance mode | **Off** |
| L2 threshold | the R2 depth at which the swap happens |
| Rumble → Trigger mode | **Left trigger only** (or Both) |

Why it works: the trigger's order of precedence is *game effects → custom effect →
slider resistance → rumble → trigger*. Rumble sits **below** the custom effect, so
you don't gate the rumble at all — you gate the custom effect, and rumble becomes
what L2 falls back to. With the L2 resistance mode off, there is nothing in
between.

In use: hold L2 and you feel your custom resistance. Pull R2 past the L2 threshold
and L2 hands over to the rumble vibration, so the shot is felt in the hand that was
holding the aim. Release R2 and the resistance comes back, ready for the next shot.
Reverse the two triggers for the mirror-image setup.

Two things make or break the feel:

- **Keep the custom effect's strength low — around 10% (level 1/7).** A firm
  resistance makes you press hard into the trigger, and a trigger held hard against
  a stiff wall damps the vibration you are about to swap in. A light resistance is
  still clearly felt while aiming and leaves the shot its impact.
- **Set the L2 threshold just *below* the point where R2 actually fires.** The swap
  and the shot then coincide, so the resistance letting go is masked by the
  vibration arriving instead of being felt as a separate event a moment earlier.

Worth knowing: because only one effect fits, you get one tactile event when the
gate engages and another when it releases — not a click, buzz and click, since the
resistance ending and the vibration starting are the same transition. How well the
window is covered depends on how long the game's rumble lasts; a very short burst
can leave a quiet gap before you release R2.

**What works — and what doesn't — while a custom effect owns the trigger**

While the custom effect owns the trigger (always, by default — or only on one
side of the gate, if you set the hand-off above), these have **no effect** on
that trigger:

- Resistance — mode, strength, shape, start position, detent / break point, Strength B
- Push-back kick — strength, style, thump frequency
- Trigger-to-rumble (R2T)

These **still work**:

- **The game's own trigger effects** — a native game always wins; the custom effect
  yields while the game is driving that trigger.
- **The activation dead zone** — it acts on the trigger *input* the PC sees, a
  separate mechanism.
- **The other trigger** — enable is per trigger, so L2 can run the normal slider
  stack while R2 plays a captured effect.
- **Everything non-trigger** — auto-haptics, effect leak, gyro, rumble, lightbar.

The exclusivity is deliberate: the resistance/kick path engages on mode and
position even with its strength at 0, so allowing it to run alongside leaked stray
kicks (a bow snap on the next press) between custom-effect engagements.

*Tip:* to keep normal R2T/resistance in one game and captured effects in another,
put them in **separate profile slots** — the automation switches slots per game.

**Sharing effects as files**

**Save ticked to file** writes the effect as readable JSON (raw bytes, plus
durations for timeline recordings). **Load custom effect file → R2 / L2** loads one
onto a trigger and sets enable and state count automatically. The portal's *Back up all slots* JSON includes each slot's
custom-effect states as well as its settings, so a backup restores a slot
complete. (Backups taken before 1.16.0 predate this and contain settings only.)

**Building an effect by hand**

When there is nothing to capture, the **Manual effect builder** authors the same
kind of effect from scratch. Add stages one at a time and set each one up:

| Stage | What it is | Set |
|---|---|---|
| Weapon break | A wall the trigger resists, then gives way past | Start and end zone, strength |
| Bow | Rising tension that snaps back at the release point | Start and end zone, and the two force levels |
| Vibration | A buzz over a range of the pull | Zones, frequency, strength |

Stages are stacked in pull order and encoded exactly as the firmware's own
writers would emit them, so a hand-built stage is byte-identical to what the
sliders — or a game — would have sent for the same values. Built effects behave
like captured ones everywhere else: they sit in the same list, play in sequence
as you pull, save to the same JSON files, and are stored in slots the same way.
Trigger positions are expressed in the same nine zones used throughout the
trigger sections, so a wall at zones 2-3 sits where the same numbers put it in a
captured effect.

### Trigger effects — shared
| Setting | Range | Default | Notes |
|---|---|---|---|
| Force Override | on/off | off | on = force R2T/AT even when a game/app is sending its own trigger effects (off = yield to the game) |

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
- **Wake and DS4Windows.** With wake enabled the bridge stays on the USB bus while
  the controller is switched off, whether or not the PC is asleep — that presence is
  exactly what makes waking possible, including the normal case where you switch the
  controller off before putting the PC to sleep. It does not leave a phantom
  controller behind: while the controller is off it presents a separate USB identity
  (*DS5Dongle (controller off)*, its own vendor and product IDs), which DS4Windows
  does not match and therefore ignores, leaving it free to auto-load profiles for
  other controllers. The DualSense identity returns when the controller reconnects.
  The idle identity is visible in `joy.cpl` and under Settings → Bluetooth & devices
  → Devices, has its own *Allow this device to wake the computer* setting, and can be
  hidden with HidHide without affecting waking. With wake **off**, the bridge leaves
  the bus entirely when the controller disconnects, as before.
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
- **Upgrading to 1.14.0 (custom effects).** The on-device configuration layout
  changed in this release. Existing settings migrate, but any custom captured
  effect assigned under a pre-1.14.0 test build must be **re-assigned** from the
  Trigger Effect Monitor (Triggers tab) or re-loaded from its JSON file after flashing.
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

## Keeping up to date

**Watch releases, not commits.** Development happens as a steady stream of commits;
those are work in progress and aren't meant to be flashed. Every tested build is
published as a **release** with its own `.uf2`, portal and changelog entry. On
GitHub, *Watch* → *Custom* → *Releases* notifies you about those and nothing else.

**Updating is drag-and-drop.** Hold BOOTSEL while plugging the board in (or
triple-click BOOTSEL on a running one) and copy the new `.uf2` to the `RPI-RP2`
drive. Take the portal from the same release — the portal and firmware are released
together and are tested as a pair.

**Your settings survive.** Configuration and all saved profile slots carry across an
upgrade: new options are appended to the stored layout, so existing values keep
their meaning and anything new starts at its default. There is nothing to back up,
nothing to re-enter, and no need for `flash_nuke.uf2` unless a release note says so.

**How often is up to you.** There's no auto-update and nothing nags you. If your
setup works, staying on it is perfectly fine — upgrade when a release mentions a fix
or feature you actually want. Release notes say plainly when a change is
firmware-only, portal-only or documentation-only, so you can skip the ones that
don't affect you.

## Files in this release

- `ds5-v1.18.17.uf2` — the firmware for the **Raspberry Pi Pico 2 W** (flash this;
  reports version 1.18.17)
- `ds5-v1.18.17-waveshare.uf2` — the same firmware for the **Waveshare
  RP2350B-Plus-W** (built against pico-sdk 2.2.0)
- `ds5-config-portal.html` — the web configuration portal (download and open)
- `flash_nuke.uf2` — config-reset utility. **Not needed for a normal upgrade** —
  only when a release note says so, or to recover from clearly corrupted settings.
  Erases all settings and every profile slot.
- `src/` — the modified source files
- `LICENSE` — MIT license
- `README.md` — this file
- `CHANGELOG.md` — version history
- `tools/` — one-command builders for Windows and macOS, plus the portal test
  harness
- `boards/` — board support, including the Waveshare RP2350B-Plus-W build script
- `automation/` — **optional** Playnite integration (see below)

## Optional: Playnite automation

The `automation/` folder adds hands-off Playnite integration: it auto-applies a
profile per game and routes game audio to the dongle for audio-driven auto-haptics,
switching automatically between native-haptics games and everything else.

Quick start:
1. **Python 3 — only if you play games *without* native DualSense support.** The
   automation starts the audio bridge for those games, and that needs Python. For
   any game listed in `native-games.txt` it applies the native profile and starts
   no audio capture, so **a native-only setup needs no Python** — slot switching,
   wake and per-game profiles all still work.
   - *Already installed it for audio routing in step 5 of the Quick start above?
     It's the same install — skip this and go to step 2.*
   - Otherwise install Python 3 from [python.org](https://www.python.org/downloads/)
     (tick *"Add python.exe to PATH"*), then `pip install PyAudioWPatch`.
     `numpy` is **optional** — it gives cleaner resampling, and is only *required*
     by the separate `ds5audio_downmix.py` helper.
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


---

## Glossary

Terms used throughout this document and in the portal.

### Haptics and audio

**Actuator** — the voice coil in each grip of a DualSense. It replaces the
rumble motors of older pads and can reproduce audio-rate signals, which is why
haptics on this controller are delivered as sound rather than as motor speeds.

**Derived (auto-)haptics** — haptic feedback this firmware *generates* from game
audio, for games that have none of their own.

**Native haptics** — haptic feedback a game renders itself and sends to the
controller. Nothing is generated in that case; the firmware only passes it
through.

**Envelope** — how loud a signal is over time, ignoring the waveform itself.
The bass envelope of the game audio is what drives the derived haptics.

**Carrier** — a steady tone the envelope is applied to. The actuators cannot
render very low frequencies, so "how much bass" is turned into felt motion by
modulating a 90 Hz tone rather than sending the bass itself.

**Noise gate** — a threshold below which the derived haptics stay silent, so
room tone, dialogue and quiet music do not buzz continuously.

**LP cutoff (low-pass)** — the frequency above which content is discarded when
looking for bass. Lower = only deep rumble drives the haptics.

**Crossover / split** — dividing the audio into a low band and a high band so
each grip can be driven by a different part of the spectrum.

**Effect leak** — with the speaker muted, letting sharp one-off sounds (shots,
impacts, clinks) through to it anyway, while sustained dialogue and music stay
muted.

**Transient / onset** — a sudden jump in level, i.e. the start of a sound. What
the effect leak detects.

**Converted rumble** — a game's rumble instructions re-created on the actuators
as vibration, used when the actuators are busy with haptics and cannot be handed
to the controller's own rumble emulation.

**Motor values** — the two numbers a game sends to request rumble. Left is the
heavy/low-frequency motor, right the light/high-frequency one, a convention
inherited from pads that really had two different motors.

**ch0/1 and ch2/3** — the four audio channels the dongle receives. ch0/1 feed
the controller speaker and are what the derived haptics are generated from by
default; ch2/3 carry native haptics from a game that sends them.

**Native passthrough** — how much of ch2/3 is passed to the actuators in Mix
mode.

**Loopback capture** — recording what the PC is playing (rather than a
microphone), which is how `ds5audio.py` gets game audio to send to the dongle.

### Trigger effects

The DualSense triggers contain a motor and a clutch that can push back against
your finger at chosen points in the pull. The vocabulary below describes what
that mechanism is doing.

**Zone** — a position along the trigger pull, numbered 0 (released) to 9 (fully
pressed). Every trigger effect is defined by the zones it acts on.

**Resistance** — constant opposing force across a range of zones. Feels like the
trigger is stiffer, or like pulling through treacle.

**Wall** — a point in the pull where the trigger suddenly resists much harder,
so it feels like it has stopped. It may be a hard stop or something you can push
past with more force.

**Weapon break** — a wall you *do* push through, the way a real trigger breaks
when the sear releases: resistance builds, then gives way suddenly. Defined by
where it starts, where it gives, and how hard it holds.

**Bow** — tension that rises the further you pull, then snaps back when
released, like drawing and loosing a bowstring. Defined by a start and end zone
and two force levels.

**Vibration (trigger)** — a buzz felt in the trigger itself over a range of
zones, at a chosen frequency and strength. Used for engines, chainsaws,
automatic fire.

**Push-back kick (Stage 2)** — a short vibration burst fired while resistance is
engaged, so each shot knocks the trigger back against your finger before
resistance resumes. Its "bow-snap" variant uses the bow mechanism for a more
mechanical snap instead of a thump.

**R2T (Trigger-to-Rumble)** — rumble generated *from* how far you pull a
trigger, rather than sent by the game.

**Force Override** — take control of the triggers even while a game is sending
its own effects, instead of yielding to the game.

**Custom captured effect** — a real effect recorded from a game that has one,
stored as the exact bytes the game sent and replayed in a game that has none.

**Timeline capture** — recording not just an effect but its rhythm, so a
sequence of vibrations replays with the original timing.

### Device and connection

**Enumeration / re-enumeration** — the process where the PC discovers a USB
device and reads what it is. Settings that change the device's description
(wake, polling rate, audio buffer, mic/speaker) require the dongle to detach and
re-attach so the PC re-reads it, which briefly disconnects the controller.

**Idle identity** — the separate USB identity the dongle presents while the
controller is switched off and wake is enabled: *DS5Dongle (controller off)*,
with its own vendor and product IDs, so nothing appears to the PC as a
controller that is not connected.

**Remote wakeup** — the USB mechanism a device uses to wake a sleeping PC. It
needs the device to still be attached, and needs permission in Windows Device
Manager.

**Profile slot** — a complete configuration stored on the dongle itself, so
switching setups is one instant command rather than writing every setting.

**Passthrough mode (DS4Windows)** — DS4Windows forwarding a game's rumble to the
real controller rather than emulating a different pad.


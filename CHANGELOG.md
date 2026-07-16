# Changelog

All notable changes to this project are documented here.

## [1.9.0] — 2026-07-16

### Added
- **16 profile slots** (was 8). Slots occupy 512 bytes each, 8 per 4 KB flash
  sector; the store now spans two sectors. Slots 1-8 remain at their exact
  pre-1.9.0 flash location - existing saved profiles carry over IN PLACE with no
  migration - and slots 9-16 live in a new sector growing downward, away from the
  config sector and the Bluetooth link-key bank. Sectors are erased and rewritten
  independently, so saving a slot never touches the other sector. Automation
  profile-overrides accept "slot 1".."slot 16"; re-run ds5-setup.bat (or update
  slot-activate.html) for the extended range. Adding further sectors later is a
  one-constant change (~zero firmware weight: slots cost flash sectors, not
  code or RAM).

## [1.8.0] — 2026-07-16

### Added
- **Trigger activation dead zone** (per trigger, 0=off, 1-9): below the chosen
  zone the GAME sees the trigger as untouched (analog forced to 0, digital press
  bit cleared) - the action registers only once the pull reaches the zone. Fixes
  hair-trigger games where the shot fires before the resistance/detent/bow zone
  is reached, breaking the feel: set the dead zone to match your effect's zone
  and the shot lands exactly where the squeeze says it should. All internal
  effects (gating, kick, shapes) always read the RAW trigger, so the feel
  machinery is unaffected; the mask applies to everything downstream identically
  (games, DS4Windows, Steam Input).

### Notes
- The analog value the game sees jumps from 0 to the threshold value on crossing
  (by design - this targets button-like trigger actions). Not intended for analog
  driving inputs: a gas pedal with a dead zone would lose its lower range.

## [1.7.1] — 2026-07-16

### Fixed
- **Strength A = 0 disabled shaped triggers entirely.** The engagement guard
  (predating shapes) treated Strength A == 0 as "feature off", so Ramp 0->B and
  detents with a free base never engaged. Shaped triggers now count as ON when
  EITHER strength is nonzero; only Constant keeps the A=0 = off convention.
- **Strength 0 zones now mean genuinely free travel.** The 0x21 effect's 3-bit
  zone value is force level 1..8 - the only true zero is excluding the zone from
  the bitmap, which shaped triggers now do. Ramp A=0 starts truly free instead of
  faintly dragging, and Detent A=0 becomes a new capability: a pure bump at the
  detent zone with free travel everywhere else.

## [1.7.0] — 2026-07-16

### Added
- **Trigger resistance shapes** (per trigger, composes with all gating modes and
  kick): the 0x21 feedback effect's 10 hardware travel zones now programmable as:
  - **Constant** — start position..full at Strength A (pre-1.7.0 behavior, default)
  - **Ramp (A → B)** — resistance changes linearly across the pull. Racing: light
    ->heavy for a loading gas pedal (A=15, B=95), heavy->light for brake bite
    (A=90, B=30). Direction is just which of A/B is larger.
  - **Two-stage detent** — base Strength A with a wall of Strength B at a chosen
    zone: a tactile bump marking half-press from full-press, for games with
    fire/alt-fire on trigger depth (e.g. Ratchet & Clank). The game reads the
    analog axis as always - the detent gives your finger the reference point.
  Zone strengths are evaluated by the controller hardware against trigger
  position - zero runtime cost, perfectly smooth response. Existing profiles are
  unaffected (shape defaults to Constant).

### Notes
- Hardware strength resolution is 8 levels per zone; ramps quantize to that but
  feel smooth in practice (native games use the same mechanism).
- Resistance responds to trigger POSITION only. Game-state-driven effects (e.g.
  resistance varying with speed) are only possible in native DualSense games.

## [1.6.4] — 2026-07-14

Code-audit release: two latent bugs in legacy profile-slot recovery, found by
review (no user-visible symptoms reported).

### Fixed
- **Legacy (pre-1.4.0) slot recovery had gone stale.** The recovery candidates
  were expressed relative to the CURRENT config size, so when the config grew in
  1.5.0 they silently shifted - slots written by config v8, v10 and v11 firmware
  would no longer be recognized during migration (v9 still matched by
  coincidence). Recovery now brute-force scans every plausible record length and
  validates by CRC, which recovers records from ANY historical layout and can
  never go stale again. Affects only users upgrading directly from <=1.3.1 with
  surviving legacy slots; already-migrated v2 slots were never at risk.
- **v2 slot records with a zero body length could false-validate** as a
  degenerate record; now rejected outright.

## [1.6.3] — 2026-07-13

### Changed
- **Faster Bluetooth reconnect.** Adopted an aggressive interlaced page-scan
  setting (11.25 ms interval) so the dongle re-listens for the host more quickly
  after a disconnect, set once the BT stack reaches its working state. Adopted
  from awalol upstream ("fix: reconnect speed"); this fork did not previously set
  page-scan parameters at all. Low-risk - affects only reconnect/discoverability
  timing, not the active connection.

## [1.6.2] — 2026-07-10

### Fixed
- **Shoulder-gated trigger modes (added in 1.6.0) did nothing.** The config
  validator still clamped the trigger mode to a maximum of 2, so selecting
  "Gated by shoulder" (value 3) was silently reset to Off on every save/load/apply
  - the mode never survived to the engagement logic. Raised the bound to 3 for
  both R2 (at_mode) and L2 (at_l2_mode). L1->R2 and R1->L2 gating now work.

## [1.6.1] — 2026-07-10

### Changed
- **Adaptive Triggers (R2, L2) and Gyro Aiming mode menus reordered** so "Off" and
  "Always on" lead, followed by the conditional/gated modes. Display order only -
  stored values are unchanged, so existing profiles and slots are unaffected.

### Fixed
- Portal select menus now honor an explicit option order. (JavaScript forces
  integer object keys into ascending order, which had silently re-sorted menus
  regardless of how the options were written; selects now use an ordered-array
  form.)

## [1.6.0] — 2026-07-10

### Added
- **Shoulder-button gating for adaptive triggers.** Each trigger's Mode dropdown
  gains a "Gated by shoulder" option: R2 can now be armed by **L1**, and L2 by
  **R1** (opposite-side, digital). This sits alongside the existing opposite-
  trigger gating (L2 arms R2 / R2 arms L2) - nothing is removed, it's an extra
  mode. Because shoulder buttons are digital, arming is simple on/off with no
  threshold (the Arming threshold field is ignored in this mode). Useful when the
  aim/ready action in a game is bound to a bumper rather than a trigger. Composes
  with per-trigger strength, kick, and bow settings exactly like the other modes.

### Notes
- No config-layout change (the new mode is just an added enum value), so existing
  profiles and slots are unaffected; config version stays 12.

## [1.5.2] — 2026-07-10

### Fixed
- **ds5audio: surround (5.1/7.1) capture starved the haptics.** Non-stereo
  captures previously kept only the front L/R channels - on AVR endpoints that
  silently discarded the LFE channel, where games route most impact bass, making
  haptics and effect leak much weaker than on a 2.0 endpoint. ds5audio now
  downmixes: LFE at full weight, side/rear at half, center (dialog) excluded by
  default (tunable via --lfe-gain / --surround-gain / --center-gain). Requires
  numpy (`pip install numpy`).
- **Effect leak much quieter since the 1.2.0 band-pass window.** Two causes: the
  window walls overlap (narrow windows lost several dB of passband level), and
  the output low-pass default of 3500 Hz removed the 3-8 kHz range where the
  controller's small speaker is most efficient - the leak could need volume ~100
  to match the old ~15. Fixes: (1) automatic make-up gain normalizes the window's
  center to unity (clamped +12 dB), so moving the walls changes character, not
  loudness - the volume slider owns loudness again; (2) the low-pass DEFAULT is
  now 8000 Hz (existing saved values are untouched - raise yours toward 8000 to
  restore loudness, or keep it low if you prefer the tamed sizzle at a higher
  volume setting).

## [1.5.1] — 2026-07-10

### Fixed
- **Slot-activation page: false failure banners eliminated.** A missing
  confirmation reply is no longer treated as failure (the activation itself
  virtually always succeeds; only the reply races - portal tab open, flash-write
  stall, mid-apply reconnect). The page now shows a blocking banner only when the
  command could not be SENT at all or the firmware explicitly replies "failed"
  (genuinely empty slot). An unconfirmed-but-sent activation shows a brief
  self-closing note instead, and the page refreshes its device handle mid-retries.
  Re-run ds5-setup.bat (or drop in the updated slot-activate.html) - the fix
  lives in the generated page.
- **Out-of-range crossover silently disabled the frequency split.** Entering e.g.
  500 Hz validated to 0 (= off) with no feedback, making the band gains appear to
  do nothing. Out-of-range values now clamp to the nearest bound (30/200 Hz)
  instead. Portal label now also states the valid range and that the crossover
  must sit below the LP Cutoff (the high band is crossover..cutoff; content above
  the cutoff never reaches the haptics at all).

## [1.5.0] — 2026-07-10

Auto-haptics frequency split: independent control of what the low and high parts
of the haptics band contribute.

### Added
- **Frequency split** (`Crossover Hz`, 0 = off): divides the haptics band at a
  tunable crossover (30-200 Hz) into a LOW band (impacts, explosions, engine
  weight) and a HIGH band (crossover..LP cutoff - where music bass lines and
  voice fundamentals sit), each with its own gain (0-100). Both envelopes feed
  the SAME gate and 90 Hz carrier, so the felt character is preserved - only the
  per-band contribution changes. Typical use: crossover ~80 Hz, low 100, high
  30-50 to keep full impact weight while taming music/dialog-driven buzz.
- **Off by default and byte-identical when off**: crossover 0 bypasses the split
  entirely; existing profiles behave exactly as before.

### Notes
- Band edges are gentle (12 dB/oct), so the two bands overlap softly: gain 0 on a
  band reduces its content roughly 3-4x rather than to absolute zero - a musical
  transition rather than a surgical cut.

## [1.4.0] — 2026-07-09

Storage moved out of Bluetooth's flash territory (fixes slots/config corruption on
controller sleep/wake), plus bulk config transfer for near-instant profile applies.

### Fixed
- **Profile slot 0 (and potentially more, eventually the config) corrupted by
  Bluetooth link-key storage.** btstack's TLV flash bank occupies the LAST TWO
  flash sectors by pico-sdk default — the exact sectors config and slots lived in.
  Any TLV write (link-key churn on controller sleep/wake or re-pair, the pairing
  blacklist) could clobber them; the bank header lands at the start of its sector,
  which was profile slot 0 — hence "first profile shows empty after sleep/wake".
  Config and slots now live two sectors lower, fully out of the bank's range, and
  a one-shot boot migration rescues everything still CRC-valid from the legacy
  locations. Anything the bank already overwrote (typically slot 0) is
  unrecoverable — re-save that profile once.

### Added
- **Bulk config transfer** (cmds 0x0b write / 0x0c read): the portal and all
  exported auto-apply pages now move the whole config in ~5 packets per direction
  instead of ~60 individual field round-trips each way. A profile apply at game
  launch completes in a fraction of the previous time — launch-delay workarounds
  for native games should no longer be needed. Older firmware is auto-detected
  and falls back to per-field transfer.

## [1.3.3] — 2026-07-09

### Fixed
- **Triggers stuck in resistance after rapid R2/L2 play (both-gated setups).**
  The controller only ever received a state report when the host sent one; games
  that send output reports only when rumble changes go silent between actions, so
  whichever trigger was engaged at the last report stayed engaged on the
  controller indefinitely — usually L2 (its gate, R2, is pressed most). A 50 ms
  synth tick now re-evaluates gating from LIVE trigger positions using the cached
  host intent and pushes the state whenever it changes, host traffic or not.
  Bonus: gated resistance now also engages/releases correctly in games that send
  no rumble at all (previously gating needed host traffic to be felt). Stale
  cached rumble is zeroed after 300 ms so nothing synthesizes from old data
  (replaces the old release-only watchdog, which cleared local state but could
  never transmit the clear).

## [1.3.2] — 2026-07-09

Profile slots now survive firmware upgrades — and this release recovers slots
that "disappeared" after flashing from 1.1.x.

### Fixed
- **Profile slots lost on firmware upgrade.** Slot records embedded the raw config
  body with a CRC spanning its compile-time size — so whenever a firmware upgrade
  grew the config (new features), older slot records failed validation and read as
  empty. They were never erased, just unreadable. Slots are now written in a v2
  format that stores its own body length (future firmware can always validate and
  read them, missing new fields default sanely), and the loader also recognizes
  legacy v1 records from config v8/v9/v10 — **slots saved under 1.1.x that were not
  overwritten since are recovered automatically on first boot of this firmware.**
- **False "activation failed" banner on first game launch after saving a profile.**
  The slot-activation page's reply can be lost when another page holds the same
  device — typically the config portal tab left open right after saving the
  profile — or during the flash-write USB stall. Activation itself succeeded; only
  the confirmation raced. The page now retries patiently (4 attempts, growing
  delays) and, if it still can't confirm, says exactly what to do (close the
  portal tab and retry) instead of claiming the slot is empty.

## [1.3.1] — 2026-07-09

Fully independent per-trigger adaptive triggers, and a mechanical bow-snap kick.

### Added
- **Two independent adaptive-trigger sections — R2 and L2.** Each trigger now has
  its own complete set: mode (Off / Gated / Always-on), resistance strength,
  arming threshold, start position, kick strength, kick style and kick frequency.
  "Gated" arms when the OPPOSITE trigger passes that trigger's own threshold
  (R2 gated = L2 arms it; L2 gated = R2 arms it), with release hysteresis. Any
  combination works: L2 always + R2 off, L2 always + R2 gated, R2 kicks while L2
  only resists, different kick styles per trigger, etc. Only "Kick follows" (the
  envelope source) is shared — it is one signal; per-trigger kick strength 0
  disables the kick on that trigger.
- **Bow-snap kick style** (per trigger): the kick can be delivered as the DualSense
  Bow effect (0x22) instead of the vibration thump — the burst momentarily switches
  the trigger to Bow, whose snap force physically presses the trigger back against
  the finger. Sharper, more "recoil" than buzz; the envelope drives the snap force.
  Experimental: the feel varies with how deep the trigger is held (the snap needs
  the finger past the bow's end zone — start position + 4).

### Changed
- `at_target` (1.2.0) and the interim kick mask are superseded by the per-trigger
  sections and removed.
- Config version 9 -> 11. Re-check both Adaptive Triggers sections after flashing
  and re-save any slots that use them.

## [1.2.0] — 2026-07-09

Selective effect leak (band-pass window + anti-flutter gate) and left-trigger
support for resistance/kick.

### Added
- **Effect-leak output low-pass** (`effect_leak_lp_hz`, default 3500 Hz). Together
  with the existing output high-pass this forms a band-pass window — only sound
  INSIDE the window ever leaks. Both walls are now 12 dB/oct (was a single 6 dB/oct
  high-pass), so window placement is real selectivity: 400–3500 Hz passes impact
  bodies while rejecting voice fundamentals below AND the treble sizzle above that
  previously read as crackle.
- **Effect-leak gate hold + hysteresis** (`effect_leak_hold`, x5 ms, default
  100 ms). The transient test used to flicker when the envelope hovered at the
  threshold — one hit could chatter the gate open/closed ~30 times (each re-open a
  pop, the hit chopped short). The gate is now a state machine: opens on a clear
  transient, stays open a minimum hold, closes only when the level falls well below
  the open threshold. One clean open/close per hit.
- **Adaptive-trigger target selector** (`at_target`): resistance and kick can now
  apply to **R2 (L2 gates — default, unchanged)**, **L2 (R2 gates — southpaw /
  L2-fire layouts)**, or **both (either trigger arms)**. The kick envelope and
  burst state are computed once per cycle and shared, so both triggers thump in
  sync. Per-trigger game-ownership yielding is preserved: a game driving one
  trigger only suppresses synthesis on that trigger.

### Changed
- Effect-leak output high-pass steepened from 6 to 12 dB/oct (sharper dialog
  rejection at the same cutoff).
- Config version 8 -> 9; new fields default sanely on first boot after flashing
  (existing settings preserved).

## [1.1.2] — 2026-07-08

Profile slots: complete configurations stored on the dongle, switched with one
atomic command. Firmware reports 1.1.2.

### Added
- **Profile slots (firmware + portal + automation).** Eight 512-byte slots in a
  dedicated flash sector, each holding a named full configuration. New HID
  commands: 0x08 save-current-to-slot, 0x09 activate-slot (reports whether a
  USB re-enumeration is needed and only then triggers one), 0x0a slot-info.
  Portal gains a **Profile Slots** panel (save/activate with names); the
  automation gains `Game = slot N` syntax in `profile-overrides.txt`, served by
  a generated one-command activator page (`profiles\slot-activate.html`) that
  self-closes in under a second — game-launch profile switching is now atomic
  and near-instant instead of a multi-second field-by-field write. Slots are
  validated on activation, so slots saved by older firmware stay safe.
- **Portal HID transaction lock.** All command/reply exchanges (slot queries,
  diagnostics polling, saves) are serialized over the shared reply buffer,
  with slot replies additionally carrying a pending marker and slot-index echo
  — concurrent reads can no longer swallow each other's replies (which showed
  up as slots randomly listed as empty or shuffled).

### Fixed
- **False "Save failed" reports.** Field writes retry transient errors and
  report per-field instead of aborting; the flash-save step tolerates the USB
  stall its own write causes; the final error message now says the truth
  (settings are usually saved by the time late stages can throw) and points at
  Re-read for verification.
- **Slots panel recovers after reconnects.** After a save/activate that
  re-enumerates the device, the panel retries its probes and self-heals
  instead of sticking on "Connect to manage slots" until a manual page reload.

### Changed
- Portal sections **Rumble → Trigger** and **Gyro Aiming** drop their
  "(experimental)" tag; the adaptive-triggers section is now titled **Stage 2**
  (resistance + push-back kick). **Advanced — BT Latency** keeps its
  experimental label.

## [1.1.1] — 2026-07-08

Automation feature + documentation release — firmware and portal unchanged
(still 1.1.0).

### Added
- **Per-game profile overrides.** `profile-overrides.txt` (generated by setup)
  maps game names to custom exported profiles
  (`game = file.html [, audio|noaudio]`): export a profile, drop it into
  `profiles\`, add one rule, and that game gets its own settings — applied in
  the normal pre-launch flow (one window, no focus loss), with the mix profile
  restored automatically on exit. Matching follows the same partial,
  encoding-tolerant rules as `native-games.txt`.
- **Start/stop state hand-off.** The start script records its decision in
  `ds5-last-start.txt`; the stop script reads it to decide whether to restore
  the mix profile. This works across separate PowerShell invocations (the
  `.bat` route) and is override-aware, replacing the native-list recompute as
  the primary exit decision.

### Changed
- **Playnite wiring docs corrected.** The recommended commands are the `.bat`
  launchers (`& "<your-folder>\automation\ds5-start.bat" "{Name}"` and the
  matching `ds5-stop.bat` line) — pasting `.ps1` paths gets them opened in
  Notepad instead of executed. Setup already printed the `.bat` lines; the
  automation README now matches.
- **native-games.txt ships as a curated default list** of PC games with native
  DualSense haptics (Returnal, God of War Ragnarök, PRAGMATA, Indiana Jones and
  the Great Circle, Ratchet & Clank: Rift Apart, Until Dawn, Alan Wake II,
  Days Gone, DOOM: The Dark Ages, The Last of Us Part I, Marvel's Spider-Man 2,
  Prince of Persia: The Lost Crown, SILENT HILL 2) instead of placeholder
  examples. Written as UTF-8; existing lists are never overwritten.
- Automation README setup steps updated to the policy-based grant flow and the
  full generated-file list.

## [1.1.0] — 2026-07-07

Adaptive triggers gain a push-back recoil kick (firmware), and profile
auto-apply is now robust with the wake feature enabled and fully hands-off
(portal + automation).

### Fixed
- **Wake broke profile auto-apply.** Enabling *Wake PC on PS Button* adds a
  boot-keyboard HID interface with the same VID/PID as the gamepad interface;
  the portal and profile pages could select it and feature reports failed. Device
  selection now skips an interface only when it is unambiguously that keyboard
  (every top-level collection is Generic Desktop/Keyboard) and otherwise behaves
  exactly as before. Applies to the portal and both auto-apply profiles; future
  exports inherit the fix.
- **Native game matching survives encoding damage.** Game names arriving with
  mangled non-ASCII characters (e.g. `Ragnarök` for `Ragnarök`) no longer fall
  through to the non-native branch: matching folds both sides to lowercase ASCII,
  and `native-games.txt` is read as UTF-8 explicitly.
- **Process cleanup works under both Windows PowerShell 5.1 and PowerShell 7.**
  Newer Playnite hosts PowerShell 7, where `Get-WmiObject` does not exist and
  the audio-capture kill silently failed; the scripts now use `Get-CimInstance`
  with a 5.1 fallback and `Stop-Process`, which work in both runtimes.
- **ds5audio survives silent launch and USB hiccups.** Under `pythonw` (silent
  automation launch) output is redirected to `ds5audio.log` instead of crashing on
  the first print; mid-stream device errors (e.g. `-9999` after a re-enumeration)
  now reconnect automatically instead of killing haptics until the next launch.

### Added
- **Adaptive triggers Stage 2 — push-back kick (recoil).** While Stage 1
  resistance is engaged, the vibration envelope fires a low-frequency vibration
  burst on R2: each rumble/haptics burst knocks the trigger back against the
  finger, then resistance resumes as it fades (hysteresis at envelope 32/16 plus
  a 45 ms minimum burst prevents mode chatter). New settings: kick strength
  (0–100, 0 = off and byte-identical Stage 1 behavior), envelope source (rumble /
  audio haptics / both — "both" means even rumble-less games kick on gunfire),
  and thump frequency (default 35; lower = heavier). New config fields 0x39–0x3b;
  live envelope + KICK flag added to the portal diagnostics (diag 0x3c). FW
  version reads 1.1.0.
- **Policy-based WebHID grant (`ds5-policy.bat` / `ds5-policy-remove.bat`).**
  Pre-grants the dongle to the profile pages via the Chromium
  `WebHidAllowDevicesForUrls` policy: no Connect click, immune to browser
  restarts and clear-site-data-on-close, and works with the DualSense-authentic
  (serial-less) USB identity. Generated by `ds5-setup`; requires admin once;
  fully reversible.
- **Profile pages auto-close after applying** (embedded profiles only), so tabs
  no longer pile up; failure states stay open and flag themselves in the taskbar
  title ("CONNECT NEEDED").
- **Profile windows open minimized** in their own browser window; HID waits run
  in a Web Worker so applying keeps full speed while hidden.
- **Debug and setup switches on `ds5-global-start.ps1`:** `-ShowWindow` runs the
  full pipeline visibly (log lines on screen, ds5audio in a console that stays
  open even if Python crashes) and `-GrantSetup` opens a profile page
  unminimized for a manual per-session grant.
- **Local profile server (opt-in).** `ds5-profile-server.py` (generated by
  setup) can serve the profile pages from `http://127.0.0.1:8377` instead of
  `file://` — enable via `$UseLocalServer` in the start/stop scripts. Off by
  default; useful only if a browser refuses `file://` grants, and covered by
  the policy grant either way.
- **Interpreter auto-detection** for the audio capture: tries
  `pythonw`/`python`/`py` in preference order, verifies the process survives
  startup, and supports pinning via `$PythonExe`.

### Removed
- Debug launchers `ds5-start-visible.bat` and `ds5-test-audio.bat` are no longer
  generated (stable now; run `ds5-global-start.ps1 -ShowWindow` or
  `python ds5audio.py --verbose` manually when debugging).
- `ds5-grant.bat` — superseded by the policy bats (the `-GrantSetup` switch on
  `ds5-global-start.ps1` remains as a manual fallback).

## [1.0.9] — 2026-07-05

Trigger-to-rumble "on press" now genuinely gates on trigger position, and native
haptics no longer break when trigger/rumble features are enabled.

### Fixed
- **`r2t` "on press" buzzed continuously.** The Vibration effect (0x26) buzzes
  whenever amplitude > 0 regardless of trigger position — restricting the zone
  bitmap did not gate it. "On press" now reads the actual analog trigger position
  and only emits vibration once the trigger is pulled past ~25%, so the triggers
  stay quiet at rest. Right-trigger position (`g_r2_pos`) is now captured alongside
  the existing left (`g_l2_pos`) in both report paths.

### Notes
- **Documented the wake / native-haptics conflict.** Enabling *Wake PC on PS Button*
  changes the USB descriptor (USB 2.1 + BOS + keyboard interface), which can make
  Steam Input stop recognizing the pad as a native DualSense — reverting some games
  (e.g. *Ratchet & Clank*) to Xbox-style rumble and disabling speaker audio. Keep
  wake off for native-haptics games, or switch it per-game. See the README.
- **Native haptics broke when trigger/rumble features were enabled (needed a
  reflash to recover).** The trigger-FFB "Allow" bits share output report byte 0
  with the rumble/haptic-mode flags. Synthesis left those Allow bits asserted in
  the persistent state, corrupting the haptic-control byte every cycle. The release
  path now clears the Allow bits (only when the game itself isn't driving the
  trigger), the game's Allow bits are synced from the host each cycle, and the
  staleness watchdog clears rather than sets them. Disabling a feature now restores
  native haptics live, without a reflash.

## [1.0.8] — 2026-07-04

### Fixed
- **Byte-0 haptics corruption** (see 1.0.9 notes — the persistent-Allow-bit fix
  landed here and was refined in 1.0.9).
- **Audio-transport fields could starve native haptics.** Documented that
  `audio_buffer_length` below the default and `polling_rate_mode = 2` (real-time)
  can interfere with the native haptic actuator stream, which shares the audio USB
  pipe. Defaults are safe; these are opt-in.

### Added
- **Channel-level audio diagnostics.** Portal now shows peak signal on ch0-1
  (speaker / DSP source) and ch2-3 (native actuators), so you can see whether real
  audio is reaching the DSP input (fields 0x37 / 0x38).

## [1.0.7] — 2026-07-04

### Fixed
- **Trigger feature priority and phantom L2.** Resistance now wins over vibration
  while a trigger is engaged (vibration resumes on release). Rumble bytes are only
  trusted when the host marks them valid, preventing phantom trigger buzz from
  apps that reuse those report offsets. Added an always-on resistance mode and a
  staleness watchdog that releases synthesis after 300 ms of no updates.

## [1.0.6] — 2026-07-03

### Added
- **Adaptive-trigger Stage 1 (`at`)** — L2-gated constant resistance on R2.
- **Trigger-to-rumble (`r2t`)** — convert rumble into trigger vibration, per-trigger.
- **Gyro-to-stick (`gyro`)** — motion aiming with configurable axis, sensitivity,
  invert, and touch modes; corrected yaw axis (byte 17) and 10x sensitivity range.
- **Haptics anti-alias** (`haptics_aa`) — 3-way filter on the native haptic stream.
- **Synthesis force override** and per-feature diagnostics fields.
- **Firmware version reported** to the portal (fields 0x7D/0x7E/0x7F).

### Fixed
- Empty-payload ownership bug where Steam Input / DS4Windows Allow-bit spam was
  mistaken for the game owning a trigger, disabling the synthesized effects.

## [1.0.5] — 2026-07-02

### Added
- Initial trigger-synthesis groundwork (rumble-to-trigger prototype) and expanded
  configuration surface, with RAM/CELT optimizations to fit the feature data.

## [1.0.4] — 2026-07-01

### Changed
- RAM optimization pass (CELT working memory) to make room for the auto-haptics
  feature data without exhausting the Pico 2W's memory.

## [1.0.3] — 2026-06-30

### Added
- Auto-haptics feature-data plumbing and configuration fields beyond the 1.0.2
  base, ahead of the trigger/gyro features in 1.0.6.

## [1.0.2] — 2026-06-20

Makes the **Wake PC on PS Button** feature genuinely usable alongside DS4Windows, and
hardens USB suspend handling. Wake remains **off by default**.

### Fixed
- **Clean controller disconnect with wake enabled.** Previously, enabling wake kept the
  USB device on the bus after the controller powered off, so the controller lingered in
  DS4Windows and the configuration portal could read stale/zero values. The controller
  now disconnects cleanly from the host whenever the PC is awake — even with wake on —
  and the device is kept on the bus only while the PC is actually suspended (where wake
  needs it to signal a wake-up). Turning the controller off no longer leaves a phantom
  USB device behind.
- **Ride out hub-induced USB suspends.** Some USB hubs briefly suspend a live bus while
  the host is awake; since the base firmware a suspend immediately powered off the
  controller's Bluetooth, dropping the controller behind such hubs. The power-off is now
  debounced (committed only after a sustained suspend — a real sleep or shutdown), so
  transient hub blips are ridden through. (Ported from upstream PR #186 by
  up2urheadlights.)
- **Deliberate USB reconnect no longer drops the controller.** A portal "Reconnect USB"
  (used when saving settings that require re-enumeration) briefly looks like a suspend.
  A grace window now exempts it, so saving such settings no longer powers off the
  controller. This also resolves the save instability that could occur with wake enabled.
- **Portal: stale handle after controller reconnect.** The configuration portal now
  listens for the WebHID disconnect event and releases its device handle, so saving
  immediately after disconnecting and reconnecting the controller works without manually
  clicking Connect again.

### Added
- **Wake PC on PS Button** is exposed as a portal toggle (in *Device & Connection*),
  off by default. Enable it only if you want the controller's PS button to wake the PC
  from sleep.

### Notes
- The stuck-rumble fix from 1.0.1-hotfix2 is included and confirmed compatible with the
  clean-disconnect behavior (the earlier disconnect regression was the wake feature, not
  the rumble fix).
- Connecting from sleep can take a few extra seconds; some variability is inherent to the
  Bluetooth reconnect path and is not specific to this build.
- After flashing, run `flash_nuke.uf2` first if you are coming from a different config
  layout.

## [1.0.1-hotfix2] — 2026-06-19

Adds an upstream stuck-rumble fix, ported to this build.

### Fixed
- **Stuck rumble while audio is active.** When the controller speaker was active
  (which is the case whenever audio passthrough or auto-haptics is in use), the
  firmware skipped re-sending state to the controller for efficiency — which
  swallowed rumble start/stop commands and could leave the motors stuck on. The
  state update now reports whether the controller-facing output actually changed,
  and the change is sent even while the speaker is active, so rumble starts and
  stops are no longer dropped. (Ports mik9's upstream "Fix stuck rumble" to the
  v0.7.0 base used here.)

### Note
- A side effect of the rumble path now being complete is that converted rumble in
  Mix mode may feel slightly stronger than before (rumble commands that were
  previously dropped now apply). Rebalance with **Converted Rumble Strength** if
  needed. This does not affect audio-derived auto-haptics, which run through a
  separate path.

## [1.0.1-hotfix] — 2026-06-19

Hotfix over 1.0.1 addressing a wake-related connection bug.

### Fixed
- **Stuck connection with wake enabled.** With the wake feature on, the USB device
  stays on the bus after the controller powers off (so a later button press can
  wake the host). On reconnection this left the controller stuck — connected but
  non-functional, with a steady (yellow) LED — until the dongle was physically
  replugged, because the bare `tud_connect()` was a no-op while the device was
  still enumerated. The firmware now forces a clean USB re-enumeration
  (`tud_disconnect()` → settle → `tud_connect()`) on reconnect when wake is
  enabled, so the connection completes normally.

### Known limitations
- With wake enabled, the device may remain listed in DS4Windows after the
  controller disconnects (the persistent USB presence wake requires). Turn wake off
  for clean disconnect behavior in DS4Windows.
- Reconnection may occasionally take slightly longer with wake enabled, as a result
  of the clean re-enumeration.

### Note
- The "Wake PC on PS Button" toggle is labeled without a device-type qualifier; the
  feature asserts USB remote wakeup on a button press to wake the host.

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

[1.0.2]: https://github.com/awalol/DS5Dongle
[1.0.1-hotfix2]: https://github.com/awalol/DS5Dongle
[1.0.1-hotfix]: https://github.com/awalol/DS5Dongle
[1.0.1]: https://github.com/awalol/DS5Dongle
[1.0.0]: https://github.com/awalol/DS5Dongle

# DS5Dongle audio + Playnite automation

**Version 1.1.1**

Zero-config auto-haptics for your DualSense via the DS5Dongle, with Playnite
integration that automatically switches between native-haptics games and
audio-driven auto-haptics games.

## What's in here

- `ds5-setup.bat` / `ds5-setup.ps1` - the installer. Run it once; it generates
  all the automation scripts with the correct paths for wherever you put this
  folder. You never edit a path by hand.
- `ds5audio.py` - routes game audio to the dongle so auto-haptics + effect leak
  work. Zero-config: auto-detects your active audio device and the dongle output.
- `..\ds5-config-portal.html` - the config portal (WebHID). Use it to tune
  settings and to Export HTML profiles.
- `profiles\audio-mix.autoapply.html` - default "auto-haptics ON" profile.
- `profiles\native-off.autoapply.html` - default "native haptics" profile.
- `native-games.txt` - list of games that use native DualSense haptics.

## Requirements

- Windows with PowerShell 5.1 (built into Windows 10/11).
- Python 3 with the `pyaudiowpatch` package: `pip install pyaudiowpatch`.
- A Chromium browser (Chrome/Edge) for the WebHID config portal.
- The DS5Dongle flashed and connected.

## Setup (once)

1. **Extract this folder anywhere** (e.g. `Documents\DS5Dongle`). All paths are
   relative to wherever you put it.

2. **Run the installer:** double-click `ds5-setup.bat`. It detects its own folder
   and generates:
   - `ds5-global-start.ps1`, `ds5-global-stop.ps1`
   - `ds5-start.bat`, `ds5-stop.bat`
   - `native-games.txt` (a curated default list - kept if you already have one)
   - `profile-overrides.txt` (optional per-game profiles template)
   - `ds5-policy.bat` / `ds5-policy-remove.bat`
   The installer prints the exact Playnite lines to use - copy them.

3. **Run `ds5-policy.bat` once** (self-elevates). It pre-grants the dongle to the
   profile pages via browser policy, so the `.autoapply.html` profiles apply
   silently and permanently - no Connect clicks, survives browser restarts.
   (For using the portal itself, click Connect there once as usual.)

4. **Edit `native-games.txt`** - one game per line (a distinctive word is enough;
   matching is case-insensitive and partial). Games listed here use native
   haptics; everything else uses audio-driven auto-haptics.

   > **Tip - let the log capture exact names for you.** You don't have to type game
   > names from memory. Wire up Playnite first (step 5), then launch each of your
   > native-haptics games once. Every launch writes a line to `ds5-automation.log`
   > like `[start] game: 'Ratchet & Clank: Rift Apart'`. Copy the exact names from
   > the log into `native-games.txt` (a distinctive word from each is enough). This
   > guarantees your entries match what Playnite actually passes, avoiding typos or
   > edition-suffix mismatches.

5. **Wire up Playnite** - Settings -> Scripts (for all games), paste these into the
   **script boxes**, adjusting the path to your install folder:
   - Before starting a game:
     `& "<your-folder>\automation\ds5-start.bat" "{Name}"`
   - After exiting a game:
     `& "<your-folder>\automation\ds5-stop.bat" "{Name}"`

   > Use the `.bat` launchers, not the `.ps1` files directly - `.ps1` paths are
   > opened by Notepad instead of executed. The setup installer prints these two
   > lines with your actual folder path filled in, ready to copy.

That's it. Launch a game and check `ds5-automation.log` to confirm the decision.

## How it works

- **Native game** (in `native-games.txt`): applies `native-off.autoapply.html`
  (native DualSense haptics), does not start audio capture. On exit, re-applies
  the mix profile so other games/desktop are ready.
- **Any other game**: applies `audio-mix.autoapply.html` (auto-haptics on) and
  starts `ds5audio.py`. On exit, stops the capture.

## Customizing the profiles

The two `.autoapply.html` files are defaults. To make your own: open the portal,
set things how you like, click **Export HTML**, name it, and drop it in `profiles\`.
Point the scripts at it by renaming or editing the generated `.ps1` (or just
overwrite the default file of the same name).

## Per-game custom profiles

To apply a custom profile for specific games (instead of the default
audio-mix / native-off pair), export it from the portal, drop the
`.autoapply.html` into `profiles\`, and add a rule to `profile-overrides.txt`:

```
Cyberpunk = cyberpunk-heavy-triggers.autoapply.html
Doom = doom-recoil.autoapply.html, audio
```

Matching follows the same rules as `native-games.txt` (partial,
case-insensitive, encoding-tolerant); first hit wins. The optional
`, audio` / `, noaudio` flag forces the ds5audio capture on or off - without
it, the native list decides as usual. Everything happens in the normal
pre-launch apply (one profile, one window, no focus loss), and the mix profile
is restored automatically on game exit. Do NOT wire extra per-game scripts in
Playnite for this: a second apply would race the first one mid-write.

## Troubleshooting

Everything is logged to `ds5-automation.log`. Each line shows the game name, the
native match, which profile applied, the Python interpreters found, and audio
start/stop. When ds5audio runs silently (pythonw), its own output goes to
`ds5audio.log` in this folder, so Python-side errors are never lost.

### One-time grant via browser policy (recommended)

Run `ds5-policy.bat` once (it self-elevates). It installs the Chromium
`WebHidAllowDevicesForUrls` policy so the dongle is **pre-granted** to the
profile pages: no Connect click is ever needed, and nothing is lost when the
browser closes, restarts, or clears site data on exit. Restart the browser
after installing. Edge/Chrome will show "Managed by your organization" while
the policy exists; `ds5-policy-remove.bat` undoes it completely.

Without the policy, WebHID grants for the dongle are session-only (the
DualSense-authentic USB identity reports no serial number, which browsers
require for permanent grants), so profile pages ask to Connect again after all
browser windows have been closed. As a manual fallback,
`ds5-global-start.ps1 -GrantSetup` opens a profile page unminimized for a
per-session Connect click.

Everything is logged to `ds5-automation.log`. When ds5audio runs silently
(pythonw), its own output goes to `ds5audio.log` in this folder. For visible
debugging run `ds5-global-start.ps1 -ShowWindow` (full pipeline, console stays
open) or `python ds5audio.py --verbose` (audio only).

Common cases:

- `game: ''` (empty) - Playnite didn't pass the name. Check that the script box
  contains the `.bat` call exactly as printed by setup, including the quoted
  `"{Name}"` argument; the start script also falls back to reading Playnite's own
  log for the name.
- Audio ran on a native game - the name didn't match `native-games.txt`; check the
  logged name and add a distinctive substring of it to the list.
- Profile didn't apply - confirm you did the one-time portal Connect, and that the
  `.autoapply.html` files are in `profiles\`.
- Audio silent - run `python ds5audio.py --verbose` and watch the captured peak;
  if it's ~0, set `$AudioArgs = @("--capture-name","PART OF YOUR OUTPUT DEVICE NAME")` in
  `ds5-global-start.ps1`.
- Wrong Python picked (e.g. PyAudioWPatch lives in a different install) - set
  `$PythonExe = "C:\full\path\to\python.exe"` near the top of
  `ds5-global-start.ps1` to pin the interpreter; auto-detect is skipped.
- `Audio stream error ... reconnecting` in ds5audio.log is normal after a profile
  apply or USB hiccup - ds5audio re-finds the dongle and resumes by itself.

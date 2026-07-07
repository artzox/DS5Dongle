# DS5Dongle audio + Playnite automation

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
   - `native-games.txt` (a template, if you don't already have one)
   The installer prints the exact Playnite lines to use - copy them.

3. **Grant the portal HID access once:** open
   `..\ds5-config-portal.html`, click Connect, pick your dongle. This
   one-time grant lets the `.autoapply.html` profiles apply silently afterward.

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
   **script boxes** (not an application/action field):
   - Before starting a game:
     `& "<folder>\ds5-global-start.ps1" -GameName $game.Name`
   - After exiting a game:
     `& "<folder>\ds5-global-stop.ps1" -GameName $game.Name`

   > **Why the inline `$game.Name` and not the `.bat` with `{Name}`?** Playnite's
   > `{Name}` token does not reliably expand in script fields, which is why the log
   > can show `game: ''`. Reading `$game.Name` directly in the script box is reliable
   > because `$game` is a real Playnite variable available in that runtime. The `.bat`
   > files still work for manual/double-click use, but for Playnite use the inline
   > calls above.

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

- `game: ''` (empty) - Playnite didn't pass the name. This happens when using the
  `.bat` with `{Name}` (which Playnite doesn't reliably expand). Fix: use the inline
  `& "...\ds5-global-start.ps1" -GameName $game.Name` call in the script box instead.
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

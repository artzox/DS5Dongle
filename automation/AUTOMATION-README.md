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

5. **Wire up Playnite** - Settings -> Scripts (for all games), paste the lines the
   installer printed:
   - Before starting a game: `"<folder>\ds5-start.bat" "{Name}"`
   - After exiting a game:   `"<folder>\ds5-stop.bat" "{Name}"`

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
native match, which profile applied, and audio start/stop.

- `game: ''` (empty) - Playnite didn't pass the name; make sure the script line
  includes `"{Name}"` and that `{Name}` expands in the field you used.
- Audio ran on a native game - the name didn't match `native-games.txt`; check the
  logged name and add a distinctive substring of it to the list.
- Profile didn't apply - confirm you did the one-time portal Connect, and that the
  `.autoapply.html` files are in `profiles\`.
- Audio silent - run `python ds5audio.py --verbose` and watch the captured peak; if
  it's ~0, pass `--capture-name "PART OF YOUR OUTPUT DEVICE NAME"`.

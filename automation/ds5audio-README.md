# ds5audio — VoiceMeeter-free audio routing for DS5Dongle

A single, auditable Python script that routes your PC's default audio output to the
DS5Dongle for auto-haptics — no VoiceMeeter, no virtual audio devices, no compiled
binary. It uses WASAPI loopback to passively *listen to* whatever is already playing
and stream it to the dongle's USB audio output.

Because it's a passive tap (beside your audio path, not inline), it doesn't introduce
the delay/desync that a mixer like VoiceMeeter can.

## Why this instead of a prebuilt app

You can read every line — there's no binary to scan. The script never makes any
network connection. Its only actions are: enumerate audio devices, capture the default
output's loopback, and play it to the dongle.

## Install (Windows only)

WASAPI is a Windows API, so this is Windows-only:

```
pip install PyAudioWPatch
```

## Use

```
python ds5audio.py
```

That auto-finds the dongle's audio output ("Speakers — DualSense [Edge] Wireless
Controller"), captures your default output, and streams it. Stop with Ctrl+C.

If auto-detect picks the wrong device, list everything and choose:

```
python ds5audio.py --list
python ds5audio.py --in-index 12 --out-index 7
```


## Choosing the capture device

**Zero-config by default.** The script captures a WASAPI **loopback** ("what a device is
playing") and forwards it to the dongle. With no flags it uses a smart auto-select:

1. It tries your Windows **default output** loopback.
2. If that's silent (your game is playing to a *non-default* device, e.g. a TV over
   HDMI), it automatically **scans all loopback devices and picks the one with audio**.

So for most users, just run `python ds5audio.py` — no device flags needed. (Native
rumble haptics use a separate path and work regardless; only auto-haptics/effect-leak
and speaker passthrough depend on capturing the right device.)

**Overrides**, if you want to pin a device explicitly (e.g. for automation reliability):

- `--capture-name "SONY TV"` — capture the loopback whose name contains this text.
  Matches by name, so it survives reboots (indices can change; names don't).
- `--auto-capture` — force the signal-scan immediately (skip the default-output step).
- `--in-index N` — force a specific index from `--list` (not reboot-safe).

The output side (which DualSense endpoint to send to) is always auto-detected by probing
for real 4-channel/48000 support, so you never need `--out-index` normally either.

Confirm it worked with the firmware diagnostic: in the portal, **ch0-1 should be > 0**
(hundreds/thousands) once real audio is flowing. If ch0-1 stays ~1, you're still
capturing a silent device.

## Channel mapping

The dongle's audio interface is **4 channels @ 48 kHz**; your default output is
probably stereo. The script maps stereo into 4 channels.

Per hardware testing, the channel roles are now **verified**: **channels 0-1 are the
speaker stream AND the source the firmware's DSP reads** (auto-haptics generation and
the effect-leak-to-speaker feature), while **channels 2-3 are the native haptic
actuator stream**. Each mapping therefore enables a different feature subset:

- `--map duplicate` (**default**) — `[L R L R]`, both pairs fed = **the full set**:
  speaker, effect leak, auto-haptics, native haptics. Matches VoiceMeeter's routing.
- `--map rear` — `[0 0 L R]` = native actuators only; the speaker, effect leak and
  auto-haptics all go silent (channels 0-1 are empty).
- `--map front` — `[L R 0 0]` = speaker/DSP only; the native actuator stream is silent.
- `--map mono_all` — mono downmix on all four `[M M M M]`

Use `duplicate` unless you deliberately want to disable part of the firmware.

## Tips

- **Set your default output to 48000 Hz** in Windows Sound settings (Device
  Properties → Advanced) to match the dongle and avoid resampling. The script warns
  if the rates differ.
- If you hear dropouts, the buffer size (`FRAMES_PER_BUFFER`, default 480 = 10 ms) can
  be raised in the script for stability at the cost of a little latency.
- Auto-haptics modes are still controlled by the firmware/portal as usual — this tool
  only provides the audio; whether it becomes haptics depends on your Replace/Mix/Off
  setting.

## Status

This is a first version. The capture side (WASAPI loopback) is well-trodden. The
playback-to-dongle side — device matching, the 4-channel format, latency — may need
tuning against real hardware. If something's off, note exactly what (no device found /
wrong device / dropouts / haptics feel wrong with which `--map`) and it can be
adjusted.


## Surround (5.1/7.1) capture

When the capture endpoint is surround (e.g. an AVR), WASAPI delivers discrete
channels and games route most impact bass to the LFE (.1) channel. ds5audio
downmixes for haptics: **LFE at full weight, side/rear at half, CENTER (dialog)
excluded** - so impacts stay strong and dialog stays out of the pad. Tune with
`--lfe-gain`, `--surround-gain`, `--center-gain` (e.g. `--center-gain 0.5` to
fold some dialog back in). On 2.0 endpoints nothing changes.

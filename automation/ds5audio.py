#!/usr/bin/env python3
"""
ds5audio — route your PC's default audio output to the DS5Dongle for auto-haptics,
without VoiceMeeter or any other virtual audio router.

It uses WASAPI loopback (via PyAudioWPatch) to *listen to* whatever is already playing
on your default output device, and streams that audio to the dongle's USB audio output
("Speakers — DualSense [Edge] Wireless Controller"). Your normal audio is untouched —
this is a passive tap beside your audio path, not an inline router, so it does not add
the delay/desync a mixer can.

This is a single readable script with no compiled binary. Read it top to bottom; the
only thing it does on the network is nothing — it never connects anywhere.

Requirements (Windows only — WASAPI is a Windows API):
    pip install PyAudioWPatch

Usage:
    python ds5audio.py                 # auto-find the dongle, capture default output, stream
    python ds5audio.py --list          # list all audio devices and exit (to pick manually)
    python ds5audio.py --out-index N   # force the dongle output device by index (from --list)
    python ds5audio.py --in-index N    # force the capture (loopback) device by index
    python ds5audio.py --map duplicate # channel mapping (default; see --help)
    python ds5audio.py --verbose       # print stream stats

Stop with Ctrl+C.
"""

import os
import sys
import time
import argparse
import struct

# --- pythonw / no-console safety --------------------------------------------
# The automation launches this script with pythonw.exe so it runs silently.
# Under pythonw, sys.stdout and sys.stderr are None, so the FIRST print() (or
# stderr.write) raises AttributeError and the process dies within a second --
# which looks exactly like "the automation didn't start ds5audio". Redirect all
# output to ds5audio.log next to this script so silent runs both work and stay
# diagnosable. Must happen BEFORE the pyaudiowpatch import (its error path
# writes to stderr too).
if sys.stdout is None or sys.stderr is None:
    _log_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ds5audio.log")
    try:
        # Keep the log from growing forever: start fresh past ~1 MB.
        _mode = "a"
        try:
            if os.path.getsize(_log_path) > 1_000_000:
                _mode = "w"
        except OSError:
            pass
        _log = open(_log_path, _mode, buffering=1, encoding="utf-8", errors="replace")
        _log.write("\n=== ds5audio (pythonw) session %s ===\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
    except OSError:
        _log = open(os.devnull, "w")
    if sys.stdout is None:
        sys.stdout = _log
    if sys.stderr is None:
        sys.stderr = _log

try:
    import pyaudiowpatch as pyaudio
except ImportError:
    sys.stderr.write(
        "ERROR: PyAudioWPatch is not installed (Windows only).\n"
        "Install it with:  pip install PyAudioWPatch\n"
    )
    sys.exit(2)

# The dongle's audio output shows up in Windows as a "Speakers" device whose name
# contains the controller model. Match on these substrings (case-insensitive); the
# numeric prefix Windows adds (e.g. "8-", "3-") is not stable, so we don't match it.
DONGLE_NAME_HINTS = ["dualsense edge wireless controller",
                     "dualsense wireless controller"]

# The dongle's audio interface is 48 kHz, 4 channels (confirmed from Windows).
DONGLE_RATE = 48000
DONGLE_CHANNELS = 4

# Audio frames per buffer. Smaller = lower latency but more CPU / risk of dropouts.
# 480 frames @ 48 kHz = 10 ms. A good starting point; tune if you hear dropouts.
FRAMES_PER_BUFFER = 480


def list_devices(pa):
    """Print every host-API device with index, name, channels, rate, and whether it's
    a WASAPI loopback device."""
    print(f"{'idx':>3}  {'in':>2} {'out':>3}  {'rate':>6}  name")
    print("-" * 70)
    for i in range(pa.get_device_count()):
        d = pa.get_device_info_by_index(i)
        name = d.get("name", "")
        loop = " (loopback)" if d.get("isLoopbackDevice", False) else ""
        print(f"{i:>3}  {int(d.get('maxInputChannels',0)):>2} "
              f"{int(d.get('maxOutputChannels',0)):>3}  "
              f"{int(d.get('defaultSampleRate',0)):>6}  {name}{loop}")


def find_dongle_output(pa, debug=False):
    """Find the dongle's audio OUTPUT device index by name hint.

    The dongle exposes several output entries (e.g. 2ch/44100, 4ch/44100,
    4ch/48000). We must send to the 4ch/48000 endpoint: sending to a 44100 entry
    (opened at 48000) makes WASAPI route silence to the controller's DSP channels.
    PyAudioWPatch's defaultSampleRate can be unreliable across duplicate-named
    endpoints, so we ALSO probe each candidate for real 48000/4ch support."""
    cands = []
    for i in range(pa.get_device_count()):
        d = pa.get_device_info_by_index(i)
        name = (d.get("name") or "").lower()
        if d.get("isLoopbackDevice", False):
            continue
        ch = int(d.get("maxOutputChannels", 0))
        if ch < 1 or not any(h in name for h in DONGLE_NAME_HINTS):
            continue
        rate = int(d.get("defaultSampleRate", 0))
        # Probe: does this endpoint actually support 4ch @ 48000 in shared mode?
        supports_48k_4ch = False
        try:
            supports_48k_4ch = pa.is_format_supported(
                DONGLE_RATE, output_device=i,
                output_channels=DONGLE_CHANNELS, output_format=pyaudio.paInt16)
        except Exception:
            supports_48k_4ch = False
        cands.append({"idx": i, "ch": ch, "rate": rate, "ok48": supports_48k_4ch})
        if debug:
            print(f"  [out cand] dev {i}: {ch}ch defaultRate={rate} "
                  f"supports_48k_4ch={supports_48k_4ch}")
    if not cands:
        return None
    # Rank: must carry 4ch; prefer confirmed 48k/4ch support; then defaultRate==48000;
    # then the LAST such device (the 48000 endpoint consistently enumerates later than
    # its 44100 twin).
    def score(c):
        s = 0
        if c["ch"] >= DONGLE_CHANNELS: s += 100
        if c["ok48"]:                  s += 40
        if c["rate"] == DONGLE_RATE:   s += 20
        s += c["idx"] * 0.01  # tiebreak toward the later (48k) enumeration
        return s
    best = max(cands, key=score)
    if debug:
        print(f"  [out pick] dev {best['idx']}")
    return best["idx"]


def find_loopback_by_name(pa, name_substr):
    """Find a WASAPI loopback device whose name contains name_substr (case-insensitive).
    Matching by name survives reboots — device indices do not."""
    want = name_substr.lower()
    for i in range(pa.get_device_count()):
        d = pa.get_device_info_by_index(i)
        if d.get("isLoopbackDevice", False) and want in (d.get("name") or "").lower():
            return i
    # Some PyAudioWPatch builds don't flag isLoopbackDevice; fall back to the name tag.
    for i in range(pa.get_device_count()):
        d = pa.get_device_info_by_index(i)
        nm = (d.get("name") or "").lower()
        if want in nm and "loopback" in nm and int(d.get("maxInputChannels", 0)) > 0:
            return i
    return None


def _peak_of_device(pa, index, ms=250):
    """Open a loopback device briefly and return the peak |sample| seen (0-32767).
    Used to detect which device actually carries audio right now.

    IMPORTANT: a silent WASAPI loopback produces NO packets at all (not silence —
    nothing), so a blocking read() would wait forever. We therefore bound the probe
    by wall-clock time and treat "no data within the window" as silence (peak 0)."""
    s = None
    try:
        info = pa.get_device_info_by_index(index)
        rate = int(info.get("defaultSampleRate", 48000))
        ch = int(info.get("maxInputChannels", 2)) or 2
        s = pa.open(format=pyaudio.paInt16, channels=ch, rate=rate,
                    frames_per_buffer=512, input=True, input_device_index=index)
        peak = 0
        deadline = time.time() + (ms / 1000.0)
        while time.time() < deadline:
            # get_read_available tells us if packets are waiting, so we never block
            # on a silent loopback that will never deliver data.
            try:
                avail = s.get_read_available()
            except Exception:
                avail = 512
            if avail <= 0:
                time.sleep(0.01)
                continue
            data = s.read(min(512, avail), exception_on_overflow=False)
            if data:
                smp = struct.unpack("<" + "h" * (len(data) // 2), data)
                p = max((abs(x) for x in smp), default=0)
                if p > peak:
                    peak = p
                if peak > 200:  # clearly active, stop early
                    break
        return peak
    except Exception:
        return -1  # couldn't open (busy/exclusive) — treat as unknown
    finally:
        if s is not None:
            try:
                s.close()
            except Exception:
                pass


def find_active_loopback(pa):
    """Scan all loopback devices and return the index of the one currently carrying
    audio (highest peak). Avoids the classic trap of capturing the Windows *default*
    device when the game is actually playing to a different output (e.g. a TV)."""
    candidates = []
    for i in range(pa.get_device_count()):
        d = pa.get_device_info_by_index(i)
        nm = (d.get("name") or "").lower()
        is_lb = d.get("isLoopbackDevice", False) or ("loopback" in nm)
        if is_lb and int(d.get("maxInputChannels", 0)) > 0:
            candidates.append(i)
    best, best_peak = None, 0
    for i in candidates:
        p = _peak_of_device(pa, i)
        if p > best_peak:
            best_peak, best = p, i
    # Only trust it if we actually found signal; else caller falls back to default.
    return (best, best_peak) if best is not None and best_peak > 50 else (None, best_peak)


def find_default_loopback(pa):
    """Find the WASAPI loopback device for the current default output (so we capture
    'what you hear'). PyAudioWPatch exposes loopback analogues of output devices."""
    try:
        wasapi = pa.get_host_api_info_by_type(pyaudio.paWASAPI)
    except Exception:
        wasapi = None

    # Preferred: the helper that returns the loopback for the default output device.
    try:
        default_out = pa.get_default_wasapi_loopback()
        if default_out:
            return default_out["index"]
    except Exception:
        pass

    # Fallback: find the default output device, then its loopback analogue by name.
    try:
        if wasapi:
            default_out_idx = wasapi.get("defaultOutputDevice")
            default_out = pa.get_device_info_by_index(default_out_idx)
            target = (default_out.get("name") or "").lower()
            for i in range(pa.get_device_count()):
                d = pa.get_device_info_by_index(i)
                if d.get("isLoopbackDevice", False) and target in (d.get("name") or "").lower():
                    return i
    except Exception:
        pass
    return None


def _have_numpy():
    try:
        import numpy  # noqa
        return True
    except ImportError:
        return False


def resample_stereo(stereo_bytes, src_rate, dst_rate):
    """Resample an interleaved stereo int16 buffer from src_rate to dst_rate.

    Uses numpy for clean linear interpolation if available, otherwise a pure-Python
    linear interpolation. For haptics (which track a low-frequency envelope) linear
    interpolation is more than adequate; we are not chasing audiophile fidelity.

    Returns interleaved stereo int16 bytes at the destination rate.
    """
    if src_rate == dst_rate:
        return stereo_bytes

    n_in = len(stereo_bytes) // 4  # stereo int16 frames
    if n_in == 0:
        return stereo_bytes
    ratio = dst_rate / src_rate
    n_out = max(1, int(round(n_in * ratio)))

    if _have_numpy():
        import numpy as np
        a = np.frombuffer(stereo_bytes, dtype="<i2").astype(np.float32).reshape(-1, 2)
        # positions in the source for each output sample
        src_idx = np.linspace(0, n_in - 1, n_out)
        lo = np.floor(src_idx).astype(np.int32)
        hi = np.minimum(lo + 1, n_in - 1)
        frac = (src_idx - lo)[:, None]
        out = (a[lo] * (1.0 - frac) + a[hi] * frac)
        out = np.clip(np.round(out), -32768, 32767).astype("<i2")
        return out.tobytes()

    # Pure-Python fallback
    samples = struct.unpack("<" + "h" * (n_in * 2), stereo_bytes)
    out = bytearray()
    for j in range(n_out):
        pos = (j / ratio)
        lo = int(pos)
        hi = min(lo + 1, n_in - 1)
        frac = pos - lo
        l = int(samples[2 * lo] * (1 - frac) + samples[2 * hi] * frac)
        r = int(samples[2 * lo + 1] * (1 - frac) + samples[2 * hi + 1] * frac)
        # clamp
        l = -32768 if l < -32768 else (32767 if l > 32767 else l)
        r = -32768 if r < -32768 else (32767 if r > 32767 else r)
        out += struct.pack("<hh", l, r)
    return bytes(out)


def map_stereo_to_4ch(stereo_bytes, mode):
    """Map an interleaved stereo int16 buffer to interleaved 4-channel int16.

    The DualSense USB audio interface is 4 channels. HARDWARE-VERIFIED roles:
      ch 0-1 (index) : the speaker stream AND the source the firmware's DSP reads
                       (auto-haptics generation, effect-leak to speaker)
      ch 2-3 (index) : the native haptic actuator stream (48k -> 3k to the motors)

    So each mapping enables a different feature subset:
      duplicate : [L R] -> [L R L R]   FULL SET — speaker + effect-leak + auto-haptics
                                        + native haptics (matches VoiceMeeter routing)
      rear      : [L R] -> [0 0 L R]   actuators ONLY (speaker/leak/auto-haptics silent)
      front     : [L R] -> [L R 0 0]   speaker/DSP only (native actuator stream silent)
      mono_all  : [L R] -> [M M M M]   mono everywhere

    Default is 'duplicate' — anything else deliberately disables part of the firmware.
    """
    n = len(stereo_bytes) // 4  # number of stereo frames (2 ch * 2 bytes)
    samples = struct.unpack("<" + "h" * (n * 2), stereo_bytes)
    out = bytearray()
    if mode == "mono_all":
        for i in range(n):
            l = samples[2 * i]
            r = samples[2 * i + 1]
            m = (l + r) // 2
            out += struct.pack("<hhhh", m, m, m, m)
    elif mode == "front":
        for i in range(n):
            l = samples[2 * i]; r = samples[2 * i + 1]
            out += struct.pack("<hhhh", l, r, 0, 0)
    elif mode == "rear":
        for i in range(n):
            l = samples[2 * i]; r = samples[2 * i + 1]
            out += struct.pack("<hhhh", 0, 0, l, r)
    else:  # duplicate
        for i in range(n):
            l = samples[2 * i]; r = samples[2 * i + 1]
            out += struct.pack("<hhhh", l, r, l, r)
    return bytes(out)


def run(args):
    pa = pyaudio.PyAudio()
    try:
        if args.list:
            list_devices(pa)
            return 0

        # --- Resolve the capture (loopback) device ---
        # Priority for a hands-off release experience:
        #   1. explicit --in-index                (power user, forced)
        #   2. --capture-name SUBSTR              (name match, reboot-safe)
        #   3. --auto-capture                     (force the signal scan)
        #   4. DEFAULT (no flags): smart auto — try the Windows default-output
        #      loopback first; if it's silent (game is playing to a NON-default
        #      device, e.g. a TV), scan all loopbacks and pick the one with signal.
        #      This makes the tool work out-of-the-box regardless of the user's
        #      default device, with no flags needed.
        in_index = None
        if args.in_index is not None:
            in_index = args.in_index
        elif args.capture_name:
            in_index = find_loopback_by_name(pa, args.capture_name)
            if in_index is None:
                sys.stderr.write(
                    f"ERROR: no loopback device matched --capture-name "
                    f"\"{args.capture_name}\". Run --list to see names.\n")
                return 1
        elif args.auto_capture:
            in_index, peak = find_active_loopback(pa)
            if in_index is None:
                sys.stderr.write(
                    "ERROR: --auto-capture found no loopback with audio (peak "
                    f"{peak}). Make sure sound is playing, or use --capture-name.\n")
                return 1
            print(f"Auto-capture: selected device [{in_index}] "
                  f"({pa.get_device_info_by_index(in_index).get('name')}) — it had signal.")
        else:
            # Smart default: prefer the default-output loopback, but verify it has
            # signal; if silent, fall back to scanning for the device that does.
            default_idx = find_default_loopback(pa)
            chosen = None
            if default_idx is not None:
                p = _peak_of_device(pa, default_idx)
                if p > 50:
                    chosen = default_idx
                    print(f"Capture: using default-output loopback [{default_idx}] "
                          f"(signal detected).")
                else:
                    print(f"Capture: default-output loopback [{default_idx}] is silent "
                          f"(peak {p}); scanning for the device that's actually playing…")
            if chosen is None:
                scan_idx, peak = find_active_loopback(pa)
                if scan_idx is not None:
                    chosen = scan_idx
                    print(f"Capture: auto-selected device [{scan_idx}] "
                          f"({pa.get_device_info_by_index(scan_idx).get('name')}) — it had audio.")
                elif default_idx is not None:
                    # Nothing playing anywhere yet; fall back to the default so the
                    # stream still opens (it'll carry audio once something plays TO
                    # that device — fine for the common case).
                    chosen = default_idx
                    print(f"Capture: no active audio found; using default-output "
                          f"loopback [{default_idx}]. (Start audio, or use "
                          f"--capture-name if your game plays to another device.)")
            in_index = chosen
        if in_index is None:
            sys.stderr.write(
                "ERROR: couldn't find a WASAPI loopback device.\n"
                "Run with --list and pass --in-index N (or --capture-name) for a "
                "'(loopback)' device.\n"
            )
            return 1
        in_info = pa.get_device_info_by_index(in_index)
        in_rate = int(in_info.get("defaultSampleRate", DONGLE_RATE))
        in_channels = int(in_info.get("maxInputChannels", 2))

        # --- Resolve the dongle output device ---
        out_index = args.out_index if args.out_index is not None else find_dongle_output(pa, debug=True)
        if out_index is None:
            sys.stderr.write(
                "ERROR: couldn't find the dongle audio output "
                "('Speakers — DualSense [Edge] Wireless Controller').\n"
                "Make sure the controller is connected through the dongle, or run --list "
                "and pass --out-index N.\n"
            )
            return 1
        out_info = pa.get_device_info_by_index(out_index)

        print(f"Capture : [{in_index}] {in_info.get('name')} "
              f"({in_channels}ch @ {in_rate} Hz)")
        print(f"Dongle  : [{out_index}] {out_info.get('name')} "
              f"(target {DONGLE_CHANNELS}ch @ {DONGLE_RATE} Hz)")
        print(f"Mapping : {args.map}")
        if in_rate != DONGLE_RATE:
            np_note = "" if _have_numpy() else " (install numpy for faster/cleaner resampling)"
            print(f"NOTE: capture {in_rate} Hz -> dongle {DONGLE_RATE} Hz: resampling "
                  f"automatically{np_note}. (Setting your default output to 48000 Hz "
                  f"would avoid resampling, but is optional.)")
        print("Routing… press Ctrl+C to stop.\n")

        # Remember the capture device NAME so we can re-find it after a USB
        # re-enumeration (device indices shift; names don't).
        in_name = in_info.get("name") or ""

        # --- Stream, with auto-reconnect --------------------------------------
        # A profile apply over WebHID or any USB hiccup can make the dongle
        # re-enumerate mid-stream; WASAPI then kills the stream with
        # OSError [-9999] "Unanticipated host error" (also seen if another
        # process grabs the endpoint). Instead of dying -- and losing haptics
        # until the next game launch -- close everything, re-find the devices,
        # and resume. Only give up after many consecutive failures.
        MAX_RECONNECTS = 15
        reconnects = 0
        in_stream = None
        out_stream = None
        while True:
            try:
                # (Re)read the capture format -- it can change after re-enumeration.
                in_info = pa.get_device_info_by_index(in_index)
                in_rate = int(in_info.get("defaultSampleRate", DONGLE_RATE))
                in_channels = int(in_info.get("maxInputChannels", 2)) or 2

                # --- Open capture stream (WASAPI loopback) ---
                in_stream = pa.open(
                    format=pyaudio.paInt16,
                    channels=in_channels,
                    rate=in_rate,
                    frames_per_buffer=FRAMES_PER_BUFFER,
                    input=True,
                    input_device_index=in_index,
                )

                # --- Open playback stream to the dongle (4ch @ 48k) ---
                out_stream = pa.open(
                    format=pyaudio.paInt16,
                    channels=DONGLE_CHANNELS,
                    rate=DONGLE_RATE,
                    frames_per_buffer=FRAMES_PER_BUFFER,
                    output=True,
                    output_device_index=out_index,
                )

                if reconnects:
                    print("Reconnected - resuming stream.")
                reconnects = 0

                frames = 0
                t0 = time.time()
                # Diagnostic level metering: track peak amplitude of the CAPTURED audio so a
                # single --verbose run reveals whether the capture path is producing signal.
                # If converted-rumble haptics work but audio/leak don't, and this peak stays
                # ~0, the problem is the capture source (wrong loopback device, or the app is
                # outputting to a different device than the one being captured), not routing.
                meter_peak = 0
                meter_frames = 0
                while True:
                    data = in_stream.read(FRAMES_PER_BUFFER, exception_on_overflow=False)

                    # If capture isn't stereo, downmix/handle: take first two channels.
                    if in_channels != 2:
                        # interleaved int16; pull ch0,ch1 from each frame
                        total = len(data) // (2 * in_channels)
                        alls = struct.unpack("<" + "h" * (total * in_channels), data)
                        st = bytearray()
                        for i in range(total):
                            base = i * in_channels
                            l = alls[base]
                            r = alls[base + 1] if in_channels > 1 else alls[base]
                            st += struct.pack("<hh", l, r)
                        data = bytes(st)

                    # Peak meter on the captured stereo (pre-resample) for diagnostics.
                    if args.verbose and data:
                        try:
                            smp = struct.unpack("<" + "h" * (len(data) // 2), data)
                            p = max((abs(x) for x in smp), default=0)
                            if p > meter_peak: meter_peak = p
                        except Exception:
                            pass

                    # Resample to the dongle's rate if the capture rate differs (e.g. 44100
                    # default output -> 48000 dongle). This is what VoiceMeeter did invisibly;
                    # doing it here means you can leave Windows at any sample rate.
                    data = resample_stereo(data, in_rate, DONGLE_RATE)

                    out = map_stereo_to_4ch(data, args.map)
                    out_stream.write(out, exception_on_underflow=False)

                    frames += FRAMES_PER_BUFFER
                    # Roughly once per second, report captured peak level (0-32767).
                    meter_frames += FRAMES_PER_BUFFER
                    if args.verbose and meter_frames >= in_rate:
                        pct = int(meter_peak * 100 / 32767)
                        bar = "#" * (pct // 5)
                        print(f"  captured peak: {meter_peak:5d}/32767 [{bar:<20}] {pct:3d}%"
                              + ("   <-- SILENT: check capture source!" if meter_peak < 32 else ""),
                              flush=True)
                        meter_peak = 0
                        meter_frames = 0
                    if args.verbose and frames % (DONGLE_RATE) < FRAMES_PER_BUFFER:
                        elapsed = time.time() - t0
                        print(f"  {frames} frames routed ({elapsed:.0f}s)")

            except OSError as e:
                reconnects += 1
                if reconnects > MAX_RECONNECTS:
                    sys.stderr.write(f"ERROR: audio stream failed {MAX_RECONNECTS} times "
                                     f"in a row; giving up ({e}).\n")
                    return 1
                print(f"Audio stream error ({e}) - reconnecting "
                      f"{reconnects}/{MAX_RECONNECTS}...", flush=True)
                for s_ in (in_stream, out_stream):
                    try:
                        if s_ is not None:
                            s_.stop_stream(); s_.close()
                    except Exception:
                        pass
                in_stream = out_stream = None
                try:
                    pa.terminate()
                except Exception:
                    pass
                time.sleep(1.2)
                # Fresh PyAudio instance: the device list is snapshotted at init,
                # so re-enumerated devices are invisible to the old instance.
                pa = pyaudio.PyAudio()
                nd = find_dongle_output(pa)
                if nd is not None:
                    out_index = nd
                if in_name:
                    ni = find_loopback_by_name(pa, in_name)
                    if ni is None:
                        ni = find_default_loopback(pa)
                    if ni is not None:
                        in_index = ni
                continue
    except KeyboardInterrupt:
        print("\nStopped.")
        return 0
    finally:
        for _s in (locals().get("in_stream"), locals().get("out_stream")):
            try:
                if _s is not None:
                    _s.stop_stream(); _s.close()
            except Exception:
                pass
        try:
            pa.terminate()
        except Exception:
            pass
    return 0


def main():
    ap = argparse.ArgumentParser(
        description="Route default audio output to the DS5Dongle (no VoiceMeeter).")
    ap.add_argument("--list", action="store_true",
                    help="list audio devices and exit")
    ap.add_argument("--out-index", type=int, default=None,
                    help="dongle output device index (from --list)")
    ap.add_argument("--in-index", type=int, default=None,
                    help="capture/loopback device index (from --list)")
    ap.add_argument("--capture-name", type=str, default=None, metavar="SUBSTR",
                    help="pick the loopback whose name contains SUBSTR (e.g. "
                         "--capture-name \"SONY TV\"). Survives reboots unlike "
                         "--in-index; recommended for automation.")
    ap.add_argument("--auto-capture", action="store_true",
                    help="scan loopback devices and capture whichever currently has "
                         "audio (avoids grabbing a silent default when the game plays "
                         "to another output like a TV). Start game audio first.")
    ap.add_argument("--map", choices=["duplicate", "front", "rear", "mono_all"],
                    default="duplicate",
                    help="how to map captured stereo into the dongle's 4 channels. "
                         "DEFAULT 'duplicate' [L R L R] feeds BOTH pairs — required for "
                         "the full feature set, hardware-verified: ch 0-1 are the "
                         "speaker stream AND the source for auto-haptics + effect-leak; "
                         "ch 2-3 are the native haptic actuators. 'rear' [0 0 L R] = "
                         "actuators only (speaker/leak/auto-haptics silent); 'front' = "
                         "speaker only (no native haptics).")
    ap.add_argument("--verbose", action="store_true", help="print stream stats")
    return run(ap.parse_args())


if __name__ == "__main__":
    sys.exit(main())

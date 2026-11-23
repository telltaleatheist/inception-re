#!/usr/bin/env python3
"""The original's sound effects against chr's (docs/sound.md, docs/reference.md).

    sound.py PORTLOG CHR_SOUND_LOG CALIBRATION

PORTLOG is the speaker log a run of the original writes with the port-logging
DOSBox-X (docs/reference.md, "Sound"): one line per write to ports 40-43 and
61, "ms port value cs:ip". CHR_SOUND_LOG is chr's --sound-log output for the
same keys. CALIBRATION is DG:5006 in that run.

The player's own writes are told apart by where they come from in its
speaker routines (287f:001c..00a9, wherever the program was loaded); the
music driver's (284b) are left out. The comparison:
- the events in order must match exactly: each tone's divisor, and each
  burst's flips;
- the time each tone lasts, and between a burst's flips, must follow the
  passes chr works out, through one straight line each fitted over the run
  (the emulator's ms per pass and a fixed cost), within a tolerance.
Prints the differences; none means a match.
"""
import sys

# Offsets of the OUTs in the speaker routines (segment 287f in BTECH.EXE).
SETUP, TONE_LOW, TONE_HIGH, TONE_ON, ENTRY_OFF, BURST_OFF, FLIP = 0x29, 0x40, 0x44, 0x4A, 0x60, 0x76, 0x92
OFFSETS = {SETUP, TONE_LOW, TONE_HIGH, TONE_ON, ENTRY_OFF, BURST_OFF, FLIP}

# A fitted time may be off by this share of the fit's own scale (the
# emulator's timer interrupts land inside some waits).
TOLERANCE = 0.05


def read_portlog(path):
    """The player's writes: (ms, offset, value), in order."""
    rows = []
    for line in open(path):
        parts = line.split()
        if len(parts) != 4:
            continue
        seg, off = (int(x, 16) for x in parts[3].split(":"))
        rows.append((float(parts[0]), seg, off, int(parts[2], 16)))
    # The player's segment: the one that writes the timer mode at SETUP.
    segs = {seg for _, seg, off, _ in rows if off == SETUP}
    return [(t, off, v) for t, seg, off, v in rows if seg in segs and off in OFFSETS]


def original_events(writes):
    """Tones and bursts with their times: ("tone", divisor, start, end) and
    ("burst", [flip times], start)."""
    events = []
    low = None
    burst = None
    for t, off, v in writes:
        if off == TONE_LOW:
            low = v
        elif off == TONE_HIGH:
            d = low | v << 8
            events.append(["tone", d or 0x10000, t, None])
        elif off in (BURST_OFF, ENTRY_OFF, SETUP):
            if events and events[-1][0] == "tone" and events[-1][3] is None:
                events[-1][3] = t
            if off == BURST_OFF:
                burst = ["burst", [], t]
                events.append(burst)
        elif off == FLIP and burst is not None:
            burst[1].append(t)
    # A tone ends where the next one starts.
    for a, b in zip(events, events[1:]):
        if a[0] == "tone" and a[3] is None:
            a[3] = b[2]
    return events


def read_chr(path):
    """chr's steps from a file of its output."""
    with open(path) as f:
        return read_chr_lines(f)


def read_chr_lines(lines):
    """chr's steps: ("tone", divisor, units, times), ("rest", units, times),
    ("burst", flips, count)."""
    steps = []
    for line in lines:
        parts = line.split()
        if not parts or parts[0] != "sound-step":
            continue
        kind, values = parts[1], [int(x) for x in parts[2:]]
        steps.append((kind, *values))
    return steps


def int16(x):
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def fit(points):
    """Least squares y = a x + b over (x, y) points."""
    n = len(points)
    if n < 2:
        return None
    mx = sum(x for x, _ in points) / n
    my = sum(y for _, y in points) / n
    sxx = sum((x - mx) ** 2 for x, _ in points)
    if sxx == 0:
        return None
    a = sum((x - mx) * (y - my) for x, y in points) / sxx
    return a, my - a * mx


def compare(writes, steps, calibration):
    diffs = []
    events = original_events(writes)
    ours = [s for s in steps if s[0] != "rest"]
    if len(events) != len(ours):
        diffs.append(f"events: the original played {len(events)}, chr {len(ours)}")
    tone_points, flip_points = [], []
    for i, (theirs, mine) in enumerate(zip(events, ours)):
        if theirs[0] != mine[0]:
            diffs.append(f"event {i}: the original a {theirs[0]}, chr a {mine[0]}")
            break
        if mine[0] == "tone":
            if theirs[1] != mine[1]:
                diffs.append(f"event {i}: divisor {theirs[1]:#x} in the original, {mine[1]:#x} in chr")
            passes = mine[3] * max(0, int16(mine[2] * calibration))
            if theirs[3] is not None:
                tone_points.append((passes, theirs[3] - theirs[2], i))
        else:
            if len(theirs[1]) != mine[1]:
                diffs.append(f"event {i}: {len(theirs[1])} flips in the original, {mine[1]} in chr")
            for a, b in zip(theirs[1], theirs[1][1:]):
                flip_points.append((calibration * mine[2], b - a, i))
    for name, points in (("tone length", tone_points), ("flip interval", flip_points)):
        line = fit([(x, y) for x, y, _ in points])
        if line is None:
            continue
        a, b = line
        for x, y, i in points:
            expected = a * x + b
            if abs(y - expected) > TOLERANCE * max(expected, a * calibration):
                diffs.append(f"event {i}: {name} {y:.4f} ms, the fit gives {expected:.4f}")
    return diffs


def main():
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    diffs = compare(read_portlog(sys.argv[1]), read_chr(sys.argv[2]), int(sys.argv[3]))
    for d in diffs:
        print(d)
    return 1 if diffs else 0


if __name__ == "__main__":
    sys.exit(main())

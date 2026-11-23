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
- the times must follow the passes chr works out, at the emulator's time
  per instruction fitted over the run: each step's median over its plays
  within 1.5 delay counts or half a tone unit, each play within that or 1%
  (the emulator's clock is not even; see the constants).
Prints the differences; none means a match.
"""
import statistics
import sys

# Offsets of the OUTs in the speaker routines (segment 287f in BTECH.EXE).
SETUP, TONE_LOW, TONE_HIGH, TONE_ON, ENTRY_OFF, BURST_OFF, FLIP = 0x29, 0x40, 0x44, 0x4A, 0x60, 0x76, 0x92
OFFSETS = {SETUP, TONE_LOW, TONE_HIGH, TONE_ON, ENTRY_OFF, BURST_OFF, FLIP}

# The times (docs/sound.md, "Comparing with the original"). The emulator
# runs about one cycle per instruction, but its clock is not even: a single
# interval has been seen up to 0.014 ms off (8 delay counts, about one tone
# unit), and a whole burst up to 0.9% short, the same in every run. Every
# flip of a burst has the same delay count, and every tone of an entry the
# same length, so each burst and each run of equal tones is judged by the
# value most of its intervals share (within CLUSTER ms) when at least two
# agree; one alone is judged only within JITTER. A step chr gets wrong is
# wrong in every play, so the tight test is on the median over all plays of
# the same step, when there are at least MEDIAN_PLAYS: within FLIP_TOLERANCE
# delay counts or TONE_TOLERANCE units (the runs show at most 0.87 counts).
# Each play alone may be off by that or by RELATIVE of its length,
# whichever is more.
CLUSTER = 0.001
FLIP_TOLERANCE = 1.5
TONE_TOLERANCE = 0.5
RELATIVE = 0.01
MEDIAN_PLAYS = 3
# Instructions per pass: the delay loop is one LOOP; the tone wait is INC,
# MOV, MOV, IMUL, CMP, JG.
DELAY_PASS, TONE_PASS = 1, 6
# The instructions a kind-2 tone takes besides its wait (27c5:0643's loop,
# 27c5:06f6, 287f:0030's OUTs), measured from effect 1 on the rig: 85.
SWEEP_TONE_OVERHEAD = 85
# Any single interval must lie within this many ms of its line.
JITTER = 0.02


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


def cluster(values):
    """The value most of `values` share, within CLUSTER: (their mean, how
    many share it)."""
    middle = sorted(values)[len(values) // 2]
    best = max(values, key=lambda v: (sum(abs(w - v) <= CLUSTER for w in values), -abs(v - middle)))
    near = [w for w in values if abs(w - best) <= CLUSTER]
    return sum(near) / len(near), len(near)


def compare(writes, steps, calibration):
    diffs = []
    events = original_events(writes)
    ours = [s for s in steps if s[0] != "rest"]
    notes = []
    if len(events) > len(ours):
        # The original runs on past its last capture when that is a poll
        # (a computer-fought round's "is a key waiting?"): those are not
        # compared. Any difference before them still is.
        notes.append(f"the original played {len(events) - len(ours)} more events after chr's last "
                     "(it runs on past the last capture); not compared")
    elif len(events) < len(ours):
        diffs.append(f"events: the original played {len(events)}, chr {len(ours)}")
    tones, bursts = [], []  # (index, chr's step, the original's intervals)
    for i, (theirs, mine) in enumerate(zip(events, ours)):
        if theirs[0] != mine[0]:
            diffs.append(f"event {i}: the original a {theirs[0]}, chr a {mine[0]}")
            break
        if mine[0] == "tone":
            if theirs[1] != mine[1]:
                diffs.append(f"event {i}: divisor {theirs[1]:#x} in the original, {mine[1]:#x} in chr")
            if theirs[3] is not None:
                tones.append((i, mine, [theirs[3] - theirs[2]]))
        else:
            if len(theirs[1]) != mine[1]:
                diffs.append(f"event {i}: {len(theirs[1])} flips in the original, {mine[1]} in chr")
            gaps = [b - a for a, b in zip(theirs[1], theirs[1][1:])]
            if gaps:
                bursts.append((i, mine, gaps))

    # The bursts: half-period against DG:5006 x count, one line fitted
    # over the medians of each count's judged plays.
    points = [(mine[2], *cluster(gaps), i, gaps) for i, mine, gaps in bursts]
    by_count = {}
    for c, y, agree, _, _ in points:
        if agree >= 2:
            by_count.setdefault(c, []).append(y)
    medians = {c: statistics.median(ys) for c, ys in by_count.items()}
    line = fit([(calibration * c, y) for c, y in medians.items()])
    per_pass = None  # ms per instruction
    if line:
        per_pass, offset = line[0] / DELAY_PASS, line[1]
        count = per_pass * DELAY_PASS * calibration
        expect = lambda c: per_pass * DELAY_PASS * calibration * c + offset
        for c, y in sorted(medians.items()):
            if len(by_count[c]) >= MEDIAN_PLAYS and abs(y - expect(c)) > FLIP_TOLERANCE * count:
                diffs.append(f"count {c}: the bursts' half-period {y:.4f} ms over {len(by_count[c])} plays, "
                             f"the line gives {expect(c):.4f} ({(y - expect(c)) / count:+.1f} counts)")
        for c, y, agree, i, gaps in points:
            e = expect(c)
            if agree >= 2 and abs(y - e) > max(FLIP_TOLERANCE * count, RELATIVE * e):
                diffs.append(f"event {i}: the burst's half-period {y:.4f} ms, the line gives {e:.4f} "
                             f"({(y - e) / count:+.1f} counts)")
            for g in gaps:
                if abs(g - e) > JITTER:
                    diffs.append(f"event {i}: a flip {g:.4f} ms after the last, the line gives {e:.4f}")
                    break

    # The tones: runs of equal steps, each judged by its common length.
    runs = []
    for i, mine, lengths in tones:
        if runs and runs[-1][1][2:] == mine[2:] and runs[-1][3] == i - 1:
            runs[-1][2].extend(lengths)
            runs[-1][3] = i
        else:
            runs.append([i, mine, list(lengths), i])
    if runs and per_pass is None:
        diffs.append("tones: no burst in the run to measure the emulator's time per instruction; lengths not judged")
        runs = []
    by_length = {}
    for i, mine, lengths, _ in runs:
        units, times = mine[2], mine[3]
        unit = per_pass * TONE_PASS * times * calibration
        y, agree = cluster(lengths)
        for length in lengths:
            if abs(length - y) > JITTER:
                diffs.append(f"event {i}: a tone of {length:.4f} ms among ones of {y:.4f}")
                break
        if times != 1:
            continue  # a note's fixed cost is not measured yet: judged against its run only
        passes = max(0, int16(units * calibration))
        e = per_pass * (TONE_PASS * passes + SWEEP_TONE_OVERHEAD)
        if agree >= 2:
            by_length.setdefault(units, []).append(y)
        if abs(y - e) > (max(TONE_TOLERANCE * unit, RELATIVE * e) if agree >= 2 else JITTER):
            diffs.append(f"event {i}: tones of {units} units last {y:.4f} ms, chr's give {e:.4f} "
                         f"({(y - e) / unit:+.1f} units)")
    for units, ys in sorted(by_length.items()):
        unit = per_pass * TONE_PASS * calibration
        e = per_pass * (TONE_PASS * max(0, int16(units * calibration)) + SWEEP_TONE_OVERHEAD)
        y = statistics.median(ys)
        if len(ys) >= MEDIAN_PLAYS and abs(y - e) > TONE_TOLERANCE * unit:
            diffs.append(f"tones of {units} units: {y:.4f} ms over {len(ys)} runs, chr's give {e:.4f} "
                         f"({(y - e) / unit:+.1f} units)")
    return diffs, notes


def main():
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    diffs, notes = compare(read_portlog(sys.argv[1]), read_chr(sys.argv[2]), int(sys.argv[3]))
    for d in notes:
        print("note: " + d)
    for d in diffs:
        print(d)
    return 1 if diffs else 0


if __name__ == "__main__":
    sys.exit(main())

"""The original's captures, kept from one run to the next.

The original is deterministic for a given KEYS.BIN (random: pokes seed it),
so a sequence's captures depend only on its keys and the rig. check.py keys
each run by a hash of both and runs DOSBox-X only when the cache has no
entry (or with --refresh). The cache holds the original's screens and
memory: it stays on this machine, under the build folder by default, and is
never committed (docs/rules.md, rule 1).

The rig is everything else that decides what a run captures: SNAP.COM as
nasm builds it, DOSBox-X's configuration (run.RUN_CONF) and program, every
file of the game folder, and CAPTURE_FORMAT.

Layout: objects/ab/<sha256> holds each file once (read-only; a capture
shared by two sequences' prefixes is stored once), and runs/<key>.json lists
a run's files by name.
"""
import datetime
import hashlib
import json
import os
import shutil
import tempfile
import threading

import run

# Raise this when what run.py keeps from a run changes (the files it writes
# to the capture folder, or how), so the old entries are not used.
CAPTURE_FORMAT = 1

# The run's own logs and rig-error notes stay in the work folder only.
NOT_KEPT = ("run-", "rig-error-")


def file_hash(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


class Cache:
    def __init__(self, root, game_dir):
        self.root = root
        self.game_dir = game_dir
        self._rig = None
        self._lock = threading.Lock()

    def rig(self):
        """The rig's hash, worked out once."""
        with self._lock:
            if self._rig is None:
                h = hashlib.sha256()
                h.update(b"format %d\n" % CAPTURE_FORMAT)
                h.update(b"conf " + run.RUN_CONF.encode() + b"\n")
                with tempfile.TemporaryDirectory() as t:
                    snap = os.path.join(t, "SNAP.COM")
                    run.build_snap(snap)
                    h.update(b"snap " + file_hash(snap).encode() + b"\n")
                h.update(b"dosbox " + file_hash(run.DOSBOX).encode() + b"\n")
                for top, dirs, files in os.walk(self.game_dir):
                    dirs.sort()
                    for name in sorted(files):
                        path = os.path.join(top, name)
                        rel = os.path.relpath(path, self.game_dir)
                        h.update(b"game " + rel.encode() + b" " + file_hash(path).encode() + b"\n")
                self._rig = h.hexdigest()
            return self._rig

    def key(self, script, seed=()):
        """The entry for a run of `script` with the `seed` files ((source,
        name) pairs) on the drive: the rig, the KEYS.BIN words with the
        capture names, and the seed files' contents."""
        words, names = run.compile_script(script)
        h = hashlib.sha256()
        h.update(self.rig().encode() + b"\n")
        h.update(json.dumps([words, names]).encode() + b"\n")
        for source, name in seed:
            h.update(b"seed " + name.encode() + b" " + file_hash(source).encode() + b"\n")
        return h.hexdigest()

    def _manifest(self, key):
        return os.path.join(self.root, "runs", key + ".json")

    def _object(self, digest):
        return os.path.join(self.root, "objects", digest[:2], digest)

    def fetch(self, key, out_dir):
        """Fill `out_dir` (emptied first) from the entry; False if there is
        none, or a file of it is missing."""
        try:
            with open(self._manifest(key)) as f:
                entry = json.load(f)
        except (OSError, ValueError):
            return False
        files = entry.get("files", {})
        if not files or not all(os.path.exists(self._object(d)) for d in files.values()):
            return False
        shutil.rmtree(out_dir, ignore_errors=True)
        os.makedirs(out_dir)
        for name, digest in files.items():
            dest = os.path.join(out_dir, name)
            try:
                os.link(self._object(digest), dest)
            except OSError:
                shutil.copyfile(self._object(digest), dest)
        return True

    def store(self, key, out_dir, sequence, seconds):
        """Keep the captures a finished run left in `out_dir`."""
        files = {}
        for name in sorted(os.listdir(out_dir)):
            path = os.path.join(out_dir, name)
            if name.startswith(NOT_KEPT) or not os.path.isfile(path):
                continue
            digest = file_hash(path)
            obj = self._object(digest)
            if not os.path.exists(obj):
                os.makedirs(os.path.dirname(obj), exist_ok=True)
                fd, tmp = tempfile.mkstemp(dir=os.path.dirname(obj))
                os.close(fd)
                shutil.copyfile(path, tmp)
                os.chmod(tmp, 0o444)
                os.replace(tmp, obj)
            files[name] = digest
        entry = {"sequence": sequence, "rig": self.rig(), "files": files, "seconds": round(seconds),
                 "made": datetime.datetime.now().isoformat(timespec="seconds")}
        manifest = self._manifest(key)
        os.makedirs(os.path.dirname(manifest), exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=os.path.dirname(manifest))
        with os.fdopen(fd, "w") as f:
            json.dump(entry, f, indent=1)
        os.replace(tmp, manifest)

    def prune(self):
        """Drop the entries made with another rig, then the files no entry
        lists. (entries dropped, files dropped, bytes freed)"""
        rig = self.rig()
        runs = os.path.join(self.root, "runs")
        kept, dropped = set(), 0
        for name in os.listdir(runs) if os.path.isdir(runs) else []:
            path = os.path.join(runs, name)
            try:
                with open(path) as f:
                    entry = json.load(f)
            except (OSError, ValueError):
                entry = {}
            if entry.get("rig") == rig:
                kept |= set(entry.get("files", {}).values())
            else:
                os.remove(path)
                dropped += 1
        files = freed = 0
        objects = os.path.join(self.root, "objects")
        for top, _, names in os.walk(objects):
            for name in names:
                if name not in kept:
                    path = os.path.join(top, name)
                    freed += os.path.getsize(path)
                    os.remove(path)
                    files += 1
        return dropped, files, freed

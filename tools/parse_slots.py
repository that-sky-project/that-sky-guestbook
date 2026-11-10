#!/usr/bin/env python3
r"""Parse the That Sky Guestbook account store.

Usage:
    python parse_slots.py                 # default %APPDATA%\guest-login\slots.bin
    python parse_slots.py <slots.bin>
    python parse_slots.py --show-key      # also print the 32-byte key as hex
    python parse_slots.py --legacy <auth.bin>

File format (slots.bin, version 1)
    char   magic[4]   = "GSLT"
    int32  version    = 1
    int32  slot_count
    int32  selected   (-1 = Steam main, -2 = new guest, >=0 = slot index)
    repeat slot_count:
        char   name[32]      (NUL padded)
        uint8  valid
        uint8  auth[64]      user[16] + device[16] + key[32]

auth[64] layout:
    [0:16]   user   16 raw UUID bytes
    [16:32]  device 16 raw UUID bytes
    [32:64]  key    32 bytes (retained for completeness; not used by this tool)

UUID bytes are the plain UUID string bytes (not Windows GUID mixed-endian):
    hex = auth[0:16].hex()
    uuid = hex[0:8] + "-" + hex[8:12] + "-" + hex[12:16] + "-" + hex[16:20] + "-" + hex[20:32]
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
from pathlib import Path

MAGIC = b"GSLT"


def uuid_from_bytes(b: bytes) -> str:
    h = b.hex()
    return f"{h[0:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:32]}"


def parse_auth(blob: bytes) -> dict:
    user, device, key = blob[0:16], blob[16:32], blob[32:64]
    return {
        "user": uuid_from_bytes(user),
        "device": uuid_from_bytes(device),
        "key_len": len(key),
        "key_nonzero": sum(1 for x in key if x),
        "key": key.hex(),
    }


def parse_slots(path: Path, show_key: bool) -> None:
    data = path.read_bytes()
    if len(data) < 16 or data[0:4] != MAGIC:
        raise SystemExit(f"{path}: not a GSLT slots file")
    version, count, selected = struct.unpack_from("<iii", data, 4)
    print(f"file      : {path}")
    print(f"version   : {version}")
    print(f"slots     : {count}")
    print(f"selected  : {selected}  ({'Steam main' if selected == -1 else 'new guest' if selected == -2 else 'slot ' + str(selected)})")
    off = 16
    for i in range(count):
        if off + 32 + 1 + 64 > len(data):
            print(f"slot {i}: truncated")
            break
        name = data[off:off + 32].split(b"\0", 1)[0].decode("utf-8", "replace")
        off += 32
        valid = data[off]
        off += 1
        auth = parse_auth(data[off:off + 64])
        off += 64
        mark = " *" if i == selected else ""
        print(f"\nslot {i}{mark}: {name!r} valid={valid}")
        print(f"  user    : {auth['user']}")
        print(f"  device  : {auth['device']}")
        print(f"  key     : {auth['key_nonzero']}/32 non-zero bytes")
        if show_key:
            print(f"  key hex : {auth['key']}")
        game_dir = Path(os.path.expandvars(r"%APPDATA%\ThatGameCompany\com.tgc.sky.win"))
        cand = game_dir / f"{auth['user']}.bin"
        if cand.exists():
            print(f"  game data: {cand} ({cand.stat().st_size} bytes)")
        else:
            print(f"  game data: {cand} (missing)")


def parse_legacy(path: Path, show_key: bool) -> None:
    data = path.read_bytes()
    if len(data) < 64:
        raise SystemExit(f"{path}: expected 64 bytes, got {len(data)}")
    auth = parse_auth(data[:64])
    print(f"file    : {path}")
    print(f"user    : {auth['user']}")
    print(f"device  : {auth['device']}")
    print(f"key     : {auth['key_nonzero']}/32 non-zero bytes")
    if show_key:
        print(f"key hex : {auth['key']}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("path", nargs="?", help="slots.bin path")
    ap.add_argument("--legacy", help="parse a legacy auth.bin (64 bytes)")
    ap.add_argument("--show-key", action="store_true", help="print the key bytes")
    ns = ap.parse_args()
    base = Path(os.path.expandvars(r"%APPDATA%\guest-login"))
    if ns.legacy:
        parse_legacy(Path(ns.legacy), ns.show_key)
        return
    path = Path(ns.path) if ns.path else base / "slots.bin"
    if not path.exists():
        legacy = base / "auth.bin"
        if legacy.exists():
            print(f"{path} not found; falling back to legacy {legacy}\n")
            parse_legacy(legacy, ns.show_key)
            return
        raise SystemExit(f"{path}: not found")
    parse_slots(path, ns.show_key)


if __name__ == "__main__":
    main()

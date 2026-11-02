#!/usr/bin/env python3
"""Prepares GitHub Release assets: zips content packs and writes manifest.json.

Usage:
  make_release.py --apk build/android/MultiGameHub-1.1.0.apk --out release/ \
                  --repo GL-328/Random-School-Games [--notes notes.md]

The app downloads https://github.com/<repo>/releases/latest/download/manifest.json,
so every release must include the files written to --out.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import zipfile
from datetime import datetime, timezone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOWED = {".json", ".txt", ".png", ".jpg", ".webp", ".wav", ".ogg", ".ttf", ".otf", ".bin", ".csv", ".svg"}
SAFE = re.compile(r"^[A-Za-z0-9._-]+$")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def version_code(v):
    major, minor, patch = (int(x) for x in v.split("."))
    assert minor < 100 and patch < 100
    return major * 10000 + minor * 100 + patch


def zip_pack(src, dest):
    """Deterministic zip (sorted entries, fixed timestamps) of a pack directory."""
    entries = []
    for dirpath, _, files in os.walk(src):
        for name in files:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, src).replace(os.sep, "/")
            parts = rel.split("/")
            if any(not SAFE.match(p) or p.startswith(".") for p in parts):
                sys.exit(f"unsafe file name in pack: {rel}")
            if os.path.splitext(rel)[1].lower() not in ALLOWED:
                sys.exit(f"file type not allowed in pack: {rel}")
            entries.append((rel, full))
    with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel, full in sorted(entries):
            info = zipfile.ZipInfo(rel, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(full, "rb") as f:
                z.writestr(info, f.read())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apk", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--repo", default="GL-328/Random-School-Games")
    ap.add_argument("--notes", help="release notes text file")
    args = ap.parse_args()

    version = open(os.path.join(ROOT, "VERSION")).read().strip()
    tag = "v" + version
    base_url = f"https://github.com/{args.repo}/releases/download/{tag}/"
    os.makedirs(args.out, exist_ok=True)

    apk_name = f"MultiGameHub-{version}.apk"
    apk_out = os.path.join(args.out, apk_name)
    shutil.copyfile(args.apk, apk_out)
    notes = open(args.notes).read().strip() if args.notes else f"MultiGame Hub {version}"

    games = []
    packs_dir = os.path.join(ROOT, "packs")
    for game_id in sorted(os.listdir(packs_dir)):
        src = os.path.join(packs_dir, game_id)
        if not os.path.isdir(src):
            continue
        if not re.match(r"^[a-z0-9_]{1,32}$", game_id):
            sys.exit(f"invalid game id {game_id}")
        meta = json.load(open(os.path.join(src, "pack.json")))
        zip_name = f"{game_id}-pack-{meta['version']}.zip"
        zip_out = os.path.join(args.out, zip_name)
        zip_pack(src, zip_out)
        games.append({
            "id": game_id,
            "name": game_id.replace("_", " ").title(),
            "version": meta["version"],
            "description": meta.get("name", ""),
            "notes": meta.get("notes", ""),
            "minAppVersionCode": 10000,
            "pack": {"url": base_url + zip_name, "sha256": sha256(zip_out), "size": os.path.getsize(zip_out)},
        })

    manifest = {
        "schema": 1,
        "publishedAt": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "app": {
            "version": version,
            "versionCode": version_code(version),
            "minSdk": 21,
            "notes": notes,
            "apk": {"url": base_url + apk_name, "sha256": sha256(apk_out), "size": os.path.getsize(apk_out)},
        },
        "games": games,
    }
    with open(os.path.join(args.out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"release assets for {tag} written to {args.out}")


if __name__ == "__main__":
    main()

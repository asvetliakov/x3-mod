#!/usr/bin/env python3
"""Create tiny record/file inputs for the Windows package reader (never decode).

No compiler, Wine, install, or game invocation. The caller supplies a freshly
built fixture executable. Output must not exist; all writes stay beneath it.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

MARKER = b"x3-media-package-config-windows-fixture-v1\n"
PE = {
    "LAVSplitter.ax": ("source", "official"),
    "LAVVideo.ax": ("video", "official"),
    "avfilter-lav-11.dll": ("dependency", "official"),
    "swscale-lav-9.dll": ("dependency", "official"),
    "libbluray.dll": ("dependency", "official"),
    "avcodec-lav-62.dll": ("dependency", "strict"),
    "avformat-lav-62.dll": ("dependency", "strict"),
    "avutil-lav-60.dll": ("dependency", "strict"),
    "swresample-lav-6.dll": ("dependency", "strict"),
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    raw = (json.dumps(data, sort_keys=True, indent=2) + "\n").encode("utf-8")
    path.write_bytes(raw)
    return digest(raw)


def prepare(output, exe):
    output.mkdir(parents=True, exist_ok=False)
    root = output / "Game relocated Ω日 space"
    root.mkdir()
    (root / ".x3-media-package-config-fixture").write_bytes(MARKER)
    shutil.copyfile(exe, root / "package_config_windows.exe")
    unrelated = root / "unrelated cwd"
    unrelated.mkdir()
    (unrelated / "x3-modern-install.json").write_text('{"media":null}\n')
    package_id = "fixture-provider-v1"
    prefix = f"providers/{package_id}"
    files = {}
    names = dict(PE)
    names.update({name: ("manifest", "official") for name in
                  ("provider.manifest", "LAVFilters.Dependencies.manifest")})
    names.update({f"notices/{name}": ("notice", "notice") for name in
                  ("COPYING", "README.md", "FFmpeg-LICENSE.md")})
    for name, (role, origin) in names.items():
        # Deliberately not PE/media/COM inputs: the reader must never load them.
        raw = ("reader fixture only: " + name + "\n").encode("ascii")
        relative = f"{prefix}/{name}"
        path = root / "x3-modern-media" / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
        files[name] = dict(path=relative, bytes=len(raw), sha256=digest(raw),
                           role=role, origin=origin)
    original = digest(b"fixture original identity, not a game asset")
    asset = f"x3-modern-media/sources/{original}/00002.mkv"
    asset_bytes = b"reader fixture source only, not media\n"
    asset_path = root / asset
    asset_path.parent.mkdir(parents=True)
    asset_path.write_bytes(asset_bytes)
    row = dict(id=2, effective_flags=8, original_relative="mov/00002.dat",
               original_sha256=original, original_bytes=17, asset=asset,
               asset_sha256=digest(asset_bytes), asset_bytes=len(asset_bytes),
               codec="mpeg1video", timeline="generated_timestamps",
               derivation_record_sha256=digest(b"fixture derivation"))
    source_relative = (Path(asset).parent / "source.json").as_posix()
    source_hash = write_json(root / source_relative,
                            dict(schema=1, kind="x3-owned-media-source",
                                 layout="installed", path_base="game_root", **row))
    package = dict(schema=1, kind="x3-owned-media-package", layout="installed",
                   path_base="media_root", package_id=package_id,
                   architecture="x86", profile="lav081-strict-mpeg1-rgb32-v1",
                   scope="local_qualification", distribution_qualified=False,
                   provider=dict(directory=prefix, manifest=f"{prefix}/provider.manifest",
                       source_clsid="{B98D13E7-55DB-4385-A33D-09FD1BA26338}",
                       video_clsid="{EE30215D-164F-4A92-A4EB-9D4C13390F9F}", files=files),
                   sources=[row], provenance=dict(fixture="synthetic-reader-only"))
    package_relative = f"x3-modern-media/{prefix}/package.json"
    package_hash = write_json(root / package_relative, package)
    write_json(root / "x3-modern-install.json", dict(schema=2,
        media=dict(package_id=package_id, package_record_relative=package_relative,
                   package_record_sha256=package_hash, sources=[dict(id=2,
                   effective_flags=8, source_record_relative=source_relative,
                   source_record_sha256=source_hash)])))
    print(json.dumps(dict(root=str(root), exe=str(root / "package_config_windows.exe"),
                         data_bytes=sum(p.stat().st_size for p in root.rglob("*")
                                        if p.is_file() and p.suffix != ".exe"),
                         fixture="synthetic-reader-only"), ensure_ascii=False))
    return root


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if not args.exe.is_file():
        parser.error("--exe must be an existing compiled reader fixture")
    prepare(args.output, args.exe)

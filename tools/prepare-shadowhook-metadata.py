#!/usr/bin/env python3
"""Prepare a same-version official ShadowHook source overlay in ignored build/.
The consumer must build/package this single ShadowHook instead of the Maven .so.
No second ELF loader, mini-debugdata decoder, or third-party source is vendored.
"""
import argparse, hashlib, io, json, tarfile, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
ARCHIVE_SHA = "77b954483cee4eead5f2ab4571b19d00c5bfa6db1b656e62a3dc7c136a14aedb"
SHA = "b2cd7ac7eb417c662d2f82b98b9183c924ddd0871ee4b7ca7f98f2e172b36e27"
def prepare(out, assets):
    cache = out.parent / "shadowhook-2.0.1.tar.gz"
    data = cache.read_bytes() if cache.exists() else subprocess.check_output(["curl", "--fail", "--location", "--silent", "--show-error", "--max-time", "60", "https://codeload.github.com/bytedance/android-inline-hook/tar.gz/refs/tags/v2.0.1"])
    if hashlib.sha256(data).hexdigest() != ARCHIVE_SHA:
        raise RuntimeError("official ShadowHook archive hash changed; review before updating")
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_bytes(data)
    prefix = "android-inline-hook-2.0.1/shadowhook/src/main/cpp/"
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        members = [m for m in archive.getmembers() if m.isfile() and m.name.startswith(prefix)]
        files = {m.name[len(prefix):]: archive.extractfile(m).read() for m in members}
        license_root = "android-inline-hook-2.0.1/LICENSE"
        root_license = archive.extractfile(license_root).read()
    if hashlib.sha256(files["third_party/xdl/xdl.c"]).hexdigest() != SHA:
        raise RuntimeError("official xDL source hash changed; review before updating overlay")
    for name, content in files.items():
        path = out / name
        if ".." in Path(name).parts: raise RuntimeError("unsafe archive entry")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    extension = ROOT / "artdcevm/src/main/cpp/support/shadowhook_metadata/exact_function_extent.inc"
    xdl = out / "third_party/xdl/xdl.c"
    declared = ROOT / "artdcevm/src/main/cpp/support/shadowhook_metadata/unique_declared_symbol.inc"
    xdl.write_bytes(xdl.read_bytes() + b"\n" + extension.read_bytes() + b"\n" + declared.read_bytes())
    exports = out / "shadowhook.map.txt"
    original = exports.read_text()
    anchor = "        shadowhook_dlopen;"
    added = "        shadowhook_get_named_function_extent;\n        shadowhook_dlsym_declared_symbol_symtab;"
    if original.count(anchor) != 1 or added in original:
        raise RuntimeError("official export insertion anchor changed")
    patched = original.replace(anchor, added + "\n" + anchor)
    if patched.count(added) != 1:
        raise RuntimeError("metadata export was not inserted exactly once")
    exports.write_text(patched)
    notices = assets / "third_party/shadowhook-2.0.1"
    notices.mkdir(parents=True, exist_ok=True)
    (notices / "LICENSE").write_bytes(root_license)
    for provider in ("xdl", "lss"):
        (notices / (provider + "-LICENSE")).write_bytes(files["third_party/" + provider + "/LICENSE"])
    for header in ("queue.h", "tree.h"):
        content = files["third_party/bsd/" + header]
        end = content.find(b"*/")
        if not content.startswith(b"/*-") or end < 0:
            raise RuntimeError("official BSD license header changed")
        (notices / ("bsd-" + header + "-LICENSE")).write_bytes(content[:end + 2] + b"\n")
    provenance = {
        "upstream": "https://github.com/bytedance/android-inline-hook/tree/v2.0.1",
        "version": "2.0.1",
        "archive_sha256": ARCHIVE_SHA,
        "original_xdl_sha256": SHA,
        "extension_sha256": hashlib.sha256(extension.read_bytes()).hexdigest(),
        "declared_lookup_sha256": hashlib.sha256(declared.read_bytes()).hexdigest(),
        "changes": ["Append exact named STT_FUNC/address/size/PF_X metadata extension to existing xdl.c", "Append strict exact/unique-declaration lookup using official symbol resolver and same symbol cache", "Export both extensions once"],
        "decoder": "Unmodified official ShadowHook xDL system liblzma wrapper; same handle/cache as shadowhook_dlsym",
        "abi": "arm64-v8a"
    }
    (notices / "PROVENANCE.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(out)
if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("--output", type=Path, default=ROOT / "build/evidence/host/named-symbol-extent/shadowhook-cpp")
    parser.add_argument("--assets", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    prepare(output, args.assets.resolve() if args.assets else output.parent / "licenseAssets")

"""Embed the exact version-locked patch, without redistributing the stock executable."""

import hashlib
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import patcher


def generate(source):
    original = Path(source).read_bytes()
    patched = patcher.patch_bytes(original)
    changes = [(i, a, b) for i, (a, b) in enumerate(zip(original, patched)) if a != b]
    assert changes and all(i < patcher.pe_layout(original)["headers_size"] for i, _, _ in changes)
    assert patcher.restore_bytes(patched) == original
    lines = ["// Generated from the exact supported stock input by generate_patch_data.py.",
             "#pragma once", "#include <cstdint>", "namespace PatchData {",
             f"inline constexpr uint32_t OriginalSize = {len(original)};",
             f"inline constexpr uint32_t PatchedSize = {len(patched)};",
             "struct Change { uint32_t offset; unsigned char original, patched; };",
             "inline constexpr Change Changes[] = {"]
    lines.extend(f"    {{{i}, {a}, {b}}}," for i, a, b in changes)
    lines.append("};")
    for name, data in (("OriginalHash", original), ("PatchedHash", patched)):
        digest = hashlib.sha256(data).digest()
        lines.append(f"inline constexpr unsigned char {name}[] = {{" + ",".join(f"0x{b:02x}" for b in digest) + "};")
    lines.append("inline constexpr unsigned char Append[] = {")
    tail = patched[len(original):]
    for i in range(0, len(tail), 16):
        lines.append("    " + ",".join(f"0x{b:02x}" for b in tail[i:i+16]) + ",")
    lines.extend(["};", "} // namespace PatchData", ""])
    header = "\n".join(lines)
    print(f"{len(changes)} changed bytes, {len(tail)} appended bytes")
    print(f"Patched SHA-256: {hashlib.sha256(patched).hexdigest()}")
    return header


if __name__ == "__main__":
    source = sys.argv[1] if len(sys.argv) > 1 else "D:/Wow.exe"
    Path(__file__).with_name("PatchData.h").write_text(generate(source), encoding="ascii")

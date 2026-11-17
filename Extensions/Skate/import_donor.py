"""Import the Phase 0 clean dependency closure without changing donor bytes."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

root = Path(__file__).parent
if sys.argv[1:] == ["--verify"]:
    manifest = json.loads((root / "donor-sha256.json").read_text())
    for name, expected in manifest.items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == expected, name
    print(f"Verified {len(manifest)} donor files without changes")
    sys.exit(0)
donor = Path(sys.argv[1]) / "native-probe"
guide = (root.parents[1] / "docs/SKATE_INTEGRATION.md").read_text(encoding="utf-8")
headers = []
for line in guide.splitlines():
    if line.startswith("| ") and "| VERBATIM" in line:
        headers += [name.strip() + ".h" for name in line.split("|")[1].split(",")]
assert len(headers) == 63, len(headers)
allowed = set(headers)
def clean(path, seen=None):
    seen = set() if seen is None else seen
    if path.name in seen:
        return True
    seen.add(path.name)
    for name in re.findall(r'#include\s+"([^"]+)"', path.read_text()):
        if name not in allowed or not clean(donor / name, seen):
            return False
    return True
tests = [p for p in donor.glob("*-tests.cpp") if clean(p)]
assert len(tests) == 41, (len(tests), [p.name for p in tests])
manifest = {}
for source, target in ([(donor / n, root / "src/SkateLogic" / n) for n in headers]
                       + [(p, root / "tests" / p.name) for p in tests]):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    assert hashlib.sha256(target.read_bytes()).hexdigest() == digest
    manifest[target.relative_to(root).as_posix()] = digest
(root / "donor-sha256.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"Imported and hash-verified {len(headers)} headers and {len(tests)} tests")

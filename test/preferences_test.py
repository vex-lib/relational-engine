"""Owner contract checks for the new local lawbook and Rust file organization."""
from pathlib import Path
import re

WORKSPACE = Path(__file__).resolve().parents[2]
ROOT = WORKSPACE / "vex/re"
prefs = (ROOT / "re-preferences.md").read_text()
for title in ("Resident Storage Boundary Law", "Cross-Language Atomic Access Law",
              "One Rust Class Per File Law", "CamelCase Rust Constructor Macro Law",
              "R2 Responsibility Layout Law"):
    assert f"### {title}" in prefs
assert "4132a6c45cb6d3797c3e8eff2e94035a" in prefs
assert "record-schema migration" in prefs and "not" in prefs
constitution = (WORKSPACE / "gist/PREFERENCES.md").read_text()
assert "vex/re/re-preferences.md" in constitution
assert "No C/Rust atomic-layout compatibility is assumed" in constitution
for path in (ROOT / "rust/src").rglob("*.rs"):
    if path.name == "helloworld.rs":
        continue  # Existing unrelated user scratch file, not engine module closure.
    declarations = re.findall(r"^\s*(?:pub(?:\([^)]*\))?\s+)?(?:struct|enum)\s+\w+", path.read_text(), re.M)
    assert len(declarations) <= 1, (path, declarations)
assert "get_atomic_byte" in (ROOT / "rust/README.md").read_text()
assert "atomic_string_owner" in (WORKSPACE / "re/test/README.md").read_text()
vex_prefs = (WORKSPACE / "host-behavior/vexspoke/vexspoke-preferences.md").read_text()
assert "relational_engine/memory.h" in vex_prefs
assert "actual Hotcwap reload integration remains unproved" in vex_prefs
assert "re-preferences.md" in (ROOT / "README.md").read_text()
lib = (ROOT / "rust/src/lib.rs").read_text()
assert "macro_rules! Memory" in lib and "macro_rules! Bytes" in lib
for module in ("nio", "io", "primitives", "variable", "struct", "compress", "virtual", "ffi"):
    assert (ROOT / "rust/src" / module / "mod.rs").is_file()
    spelling = f"r#{module}" if module in ("struct", "virtual") else module
    assert f"pub mod {spelling};" in lib
assert "mmap" in prefs and "R2 storage backend alongside Vexspoke" in prefs
assert "Graphvex" in (ROOT / "rust/src/virtual/mod.rs").read_text()
assert "No codecs are implemented" in (ROOT / "rust/src/compress/mod.rs").read_text()
for part in ("search", "search/primitives"):
    assert "no" in (ROOT / "src" / part / "README.md").read_text().lower()
assert not (ROOT / "rust/src/compute").exists()
assert "### Stable Row and Variable Binding Law" in prefs
assert "borrowed VALUE" in prefs
assert "self" in prefs and "not" in prefs
for module, unit in (("nio", "chunk"), ("struct", "chunked_list"),
                     ("variable", "variable_slot"), ("variable", "variable_registry")):
    assert (ROOT / "rust/src" / module / f"{unit}.rs").is_file()
    assert (WORKSPACE / "re/test/rust" / module / f"{unit}_test.rs").is_file()
assert "VariableRegistry" in (ROOT / "rust/README.md").read_text()
assert "twenty-two independent owner targets" in (WORKSPACE / "re/test/README.md").read_text()
for module, unit in (("nio", "typed_chunk"), ("struct", "typed_pool")):
    assert (ROOT / "rust/src" / module / f"{unit}.rs").is_file()
    assert (WORKSPACE / "re/test/rust" / module / f"{unit}_test.rs").is_file()
assert "generation-tagged `Handle`" in prefs
assert "Reusable typed storage" in (ROOT / "rust/README.md").read_text()
# The retired wiki is not current proof or a dependency of local owner checks.
assert "Fixed-Extent File Mapping Law" in prefs
mapping = (ROOT / "rust/src/nio/mapped_file.rs").read_text()
assert "pub unsafe fn open" in mapping and "offset.checked_add(length)" in mapping
assert "external" in mapping and "No escaped descriptor or resize API" in mapping
assert "view.flush()?" in mapping and "sync_all()?" in mapping
assert "file identity plus offsets" in prefs
assert "buffered readers/writers" in prefs
assert "Manifest-backed persistence remains proposed" in prefs
assert "physical byte-file reservation" in (ROOT / "rust/src/io/mod.rs").read_text()
assert "Physical File Reservation Law" in prefs
owner = (ROOT / "rust/src/io/preallocated_file.rs").read_text()
backend = (ROOT / "rust/src/io/preallocation.rs").read_text()
assert "create_new(true)" in owner and "mode(0o600)" in owner
assert "allocated < length" in owner and "fs::remove_file(&path)" in owner
assert "F_ALLOCATEALL" in backend and "libc::fallocate" in backend
assert "Self::from_file(file, true)" in mapping and "owner.into_file()" in mapping
for doc in (ROOT / "README.md", ROOT / "rust/README.md"):
    text = doc.read_text()
    assert "PreallocatedFile" in text and "Application Support/vexgraph" in text
    assert "APFS" in text and "remain gaps" in text
assert "re_name_search" in (ROOT / "src/search/primitives/README.md").read_text()
assert "BORROW" in (ROOT / "rust/include/relational_engine/variable_registry.h").read_text().upper()
print("PASS: resident/atomic contracts, reading map and one named Rust type per file")

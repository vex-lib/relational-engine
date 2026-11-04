"""Offline primer owner: checks documented boundaries, not runtime readiness.

Run independently: python3 -B relational-engine/test/primer_test.py.
Source assertions pin the documented identity warnings to the reviewed code;
if behavior changes, review the warnings rather than blindly updating this test.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / "vex/re"


class PrimerTest(unittest.TestCase):
    def setUp(self):
        self.doc = (ENGINE / "docs/relational-engine-primer.md").read_text()

    def test_readiness_and_roadmap_are_scoped(self):
        for note in (
            "no whole-engine production-readiness claim",
            "Repair typed identity",
            "Choose the first consumer and its contract",
            "Expose only the required C surface",
            "Mapping is a choice, not a readiness badge",
            "Rust is here (it does not have to be)",
            "no consumer rewrite",
            "not instrument Rust",
        ):
            self.assertIn(note, self.doc)
        self.assertEqual(self.doc.count("```") % 2, 0)

    def test_identity_warnings_match_reviewed_source(self):
        chunk = (ENGINE / "rust/src/nio/typed_chunk.rs").read_text()
        pool = (ENGINE / "rust/src/struct/typed_pool.rs").read_text()
        handle = (ENGINE / "rust/src/nio/handle.rs").read_text()
        self.assertIn("generations.resize(capacity, 1u32)", chunk)
        self.assertNotIn("wrapping_add(1)", chunk)
        self.assertIn("Self { index: 0, generation: 0 }", handle)
        self.assertIn("chunk.release_backing()", pool)
        self.assertIn("let mut chunk = TypedChunk::new(self.rows_per_chunk)?", pool)
        for note in ("generation one", "retires the slot permanently",
                      "keeps generation metadata", "no owner identity"):
            self.assertIn(note, self.doc)

    def test_c_example_and_ownership_limits(self):
        header = (ENGINE / "rust/include/relational_engine/memory.h").read_text()
        for name in ("re_memory_new", "re_memory_copy", "re_memory_read"):
            self.assertIn(name, header)
            self.assertIn(name, self.doc)
        self.assertIn("new may abort on allocator OOM", header)
        for note in ("can abort on OOM", "do **not** implement",
                     "does not own, type-check or keep", "Integers still have byte order"):
            self.assertIn(note, " ".join(self.doc.split()))

    def test_row_pool_competency_matches_public_source(self):
        header = (ENGINE / "rust/include/relational_engine/row_pool.h").read_text()
        bridge = (ENGINE / "rust/src/ffi/row_pool.rs").read_text()
        pool = (ENGINE / "rust/src/struct/row_pool.rs").read_text()
        for operation in ("new", "drop", "add", "read", "write", "borrow", "remove",
                          "release_empty", "len", "geometry", "to_string", "to_string_struct"):
            self.assertIn("re_rows_" + operation, header)
            self.assertIn("fn re_rows_" + operation, bridge)
        self.assertIn("compare_exchange", pool)
        self.assertNotIn("fetch_add", pool)
        for path in ("README.md", "rust/README.md"):
            doc = (ENGINE / path).read_text()
            for note in ("## Current State", "## Scope and Limitations", "nio/relational_rows.h",
                         "Windows", "unproved"):
                self.assertIn(note, doc)
        preferences = (ENGINE / "re-preferences.md").read_text()
        self.assertIn("RowPool C API", preferences)
        self.assertIn("explicit status codes", preferences)
        mapping = (ENGINE / "rust/src/nio/fixed_mapped_file.rs").read_text()
        self.assertIn("pub unsafe fn open", mapping)
        self.assertIn("offset.checked_add(length)", mapping)
        for path in ("README.md", "rust/README.md"):
            doc = (ENGINE / path).read_text()
            for note in ("FixedMappedFile", "unsafe", "truncation", "offset", "no hard latency bound"):
                self.assertIn(note, doc)


if __name__ == "__main__":
    unittest.main()

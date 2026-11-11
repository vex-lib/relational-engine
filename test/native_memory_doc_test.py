"""Mechanical native-memory competency checks, not independent runtime proof.

Read actual implementation/test/runner fragments before checking documentation.
This guards concrete claims and declared limits; executable C owners separately
establish behavior. It cannot certify every sentence or allocator readiness.
"""
from pathlib import Path
import unittest

WORKSPACE = Path(__file__).resolve().parents[2]
ENGINE = WORKSPACE / "re"


class NativeMemoryDocumentation(unittest.TestCase):
    def test_capacity_and_reset_claims_match_source(self):
        source = (ENGINE / "nio/arena.c").read_text()
        alloc = source.split("static void *arena_alloc(", 1)[1].split(
            "static void arena_free(", 1)[0]
        self.assertNotIn("malloc(", alloc)
        self.assertIn("return nullptr;", alloc)
        reset = source.split("static void arena_freeAll(", 1)[1].split(
            "MemoryArena *Memory_arenaFor(", 1)[0]
        self.assertIn("memset((*a).bumpArena, 0, (*a).bumpOffset)", reset)
        enumerate_blocks = source.split("bytes MemoryArena_findAll(", 1)[1].split(
            "bytes MemoryArena_activeBytes(", 1)[0]
        self.assertIn("(*a).slabs[s]", enumerate_blocks)
        self.assertIn("offset < (*a).bumpOffset", enumerate_blocks)
        self.assertIn("count < maxCount", enumerate_blocks)
        doc = (ENGINE / "docs/native-memory-proof.md").read_text()
        for statement in ("without heap allocation", "not constant time",
                          "includes both live slab and bump", "no generations",
                          "not hostile deliberate checksum forgery"):
            self.assertIn(statement, doc)

    def test_proof_claims_have_registered_cases_and_limits(self):
        suite = WORKSPACE / "re/test"
        owner = (suite / "nio/mem_exhaustion_test.c").read_text()
        for scenario in ("construction();", "exhaustion();", "boundaries();",
                         "allClasses();", "hostile();", "model();", "concurrent();"):
            self.assertIn(scenario, owner)
        for oracle in ("heapCalls == calls", "SIZE_MAX", "UINT32_MAX",
                       "checkBytes", "MODEL_STEPS = 10000", "WORKER_STEPS = 4000"):
            self.assertIn(oracle, owner)
        runner = (suite / "native_run.py").read_text()
        for registration in ("nio/mem_exhaustion_test.c", "-DMEM_TEST_FAULTS=1",
                             "-Dmalloc=MemTest_malloc", "-fsanitize=thread",
                             "-fsanitize=address,undefined", "timeout=15"):
            self.assertIn(registration, runner)
        doc = (ENGINE / "docs/native-memory-proof.md").read_text()
        readme = (ENGINE / "README.md").read_text()
        self.assertIn("docs/native-memory-proof.md", readme)
        self.assertIn("Twelve owners", readme)
        for gap in ("Windows/macOS 14 runtime", "first-use concurrency",
                    "not leak detection", "performance/latency", "not blanket",
                    "at most four worker-owned blocks", "concurrent-occupancy"):
            self.assertIn(gap, doc)


if __name__ == "__main__":
    unittest.main()

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
        pool = (ENGINE / "nio/pool.c").read_text()
        self.assertIn("os_map(", pool)
        self.assertIn("os_releasePages(", pool)
        self.assertIn("MemoryPool_allocPages(", pool)
        self.assertIn("MemoryPool_freePages(", pool)

        source = (ENGINE / "nio/arena.c").read_text()
        self.assertNotIn("sys/mman.h", source)
        alloc = source.split("static any arena_alloc(", 1)[1].split(
            "static fn arena_free(", 1)[0]
        self.assertNotIn("malloc(", alloc)
        self.assertIn("return nullptr;", alloc)
        reset = source.split("static fn arena_freeAll(", 1)[1].split(
            "MemoryArena *Memory_arenaFor(", 1)[0]
        self.assertIn("MemoryPool_freePages(pool,", reset)
        enumerate_blocks = source.split("bytes MemoryArena_findAll(", 1)[1].split(
            "bytes MemoryArena_activeBytes(", 1)[0]
        self.assertIn("MemoryArena_slabHead(a, s)", enumerate_blocks)
        self.assertIn("MemoryArena_spanHead(a)", enumerate_blocks)
        self.assertIn("count < maxCount", enumerate_blocks)

        doc = " ".join((ENGINE / "docs/native-memory-proof.md").read_text().split())
        for statement in ("without heap allocation", "not constant time",
                          "includes both live slab and span", "no generations",
                          "not hostile deliberate checksum forgery"):
            self.assertIn(statement, doc)

    def test_proof_claims_have_registered_cases_and_limits(self):
        suite = WORKSPACE / "re/test"
        owner = (suite / "re_test.c").read_text()
        for scenario in ("ex_construction();", "ex_exhaustion();", "ex_coalesce();",
                         "ex_boundaries();", "ex_allClasses();", "ex_hostile();",
                         "ex_model();", "ex_concurrent();"):
            self.assertIn(scenario, owner)
        for oracle in ("g_heapCalls == calls", "SIZE_MAX", "UINT32_MAX",
                       "checkBytes", "MODEL_STEPS = 10000", "WORKER_STEPS = 4000"):
            self.assertIn(oracle, owner)
        runner = (suite / "native_run.py").read_text()
        for registration in ("re_test.c", "nio/pool.c", "nio/arena.c", "-DMEM_TEST_FAULTS=1",
                             "-DMemoryPool_allocPages=MemTest_allocPages", "-fsanitize=thread",
                             "-fsanitize=address,undefined", "timeout=15"):
            self.assertIn(registration, runner)
        doc = " ".join((ENGINE / "docs/native-memory-proof.md").read_text().split())
        readme = (ENGINE / "README.md").read_text()
        self.assertIn("docs/native-memory-proof.md", readme)
        self.assertIn("MemoryPool", readme)
        for gap in ("Windows/macOS 14 runtime", "first-use concurrency",
                    "not leak detection", "performance/latency", "not blanket",
                    "at most four worker-owned blocks", "concurrent-occupancy"):
            self.assertIn(gap, doc)


if __name__ == "__main__":
    unittest.main()

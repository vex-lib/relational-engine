# C search

The production direction is C search over Rust-owned storage at R2, alongside
Vexspoke. `primitives` owns primitive-span search. Filesystem gathering/indexing
belongs to Rust `io/`; this is not a second file watcher or GPU driver.

`primitives/name_search.{c,h}` implements allocation-free fixed-name search over
borrowed strided rows. Cargo builds only that engine-owned C module, not the
imported C references elsewhere in `src/`. The scan is cold and linear, not an
index or a per-frame resolver. Invalid spans report through engine THROW and
leave the output unchanged; absence has its own status.

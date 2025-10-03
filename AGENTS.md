# Repository Guidelines

## Project Structure & Module Organization
- `src/` keeps the C side (FUSE shim, block/inode/path layers) plus CLI testers; mirror new code with headers in `include/modernfs/`.
- `rust_core/` is the Rust static library (extent, journal, transaction); expose FFI in `lib.rs` and sync signatures with the C headers.
- `tests/` holds `unit/`, `integration/`, `crash/`, and `scripts/` directories; helper tooling prototypes live under `tools/`.
- Keep timelines and specs in `docs/` and update `README.md` once runtime behavior or commands change.

## Build, Test, and Development Commands
- `./build.sh` (Linux/macOS) or `build.bat` (Windows) configures CMake in `build/` and emits the test binaries.
- Use `cmake --build build --target test_inode_layer` for fast rebuilds, then run `./build/test_block_layer`, `./build/test_inode_layer`, or `./build/test_ffi`.
- Rust logic is checked with `cargo fmt`, `cargo clippy --manifest-path rust_core/Cargo.toml`, and `cargo test --manifest-path rust_core/Cargo.toml`.
- FUSE smoke checks live in `tests/scripts/`; `bash tests/scripts/test_fuse_simple.sh` should back any change to `fuse_ops.c`.

## Coding Style & Naming Conventions
- C code uses four-space indentation, K&R braces, and `snake_case`; keep internal helpers `static` and place state structs beside their consumers.
- Header guards follow `MODERNFS_<NAME>_H`; order includes as standard → third party → project, and prefer brief comments for nonobvious invariants.
- Rust modules follow `rustfmt`; align FFI names across languages (`rust_extent_alloc` ↔ `extent_alloc`) and surface shared constants via `include/modernfs/`.

## Testing Guidelines
- Clone existing `test_<component>.c` scaffolding when adding suites, wiring cases into the runner block at the file end.
- Put long-running or failure-injection scenarios in `tests/integration/` or `tests/crash/`, and capture scripted repros in `tests/scripts/`.
- Treat the three compiled binaries plus `cargo test` as a gating set; refresh logged expectations in docs whenever outputs shift.

## Commit & Pull Request Guidelines
- Match the history style: prefix messages with a scope or milestone (`Week 4`, `docs:`) and keep the summary in present tense.
- Limit each commit to one change set, referencing issues with `Refs #<id>` when coordination is needed.
- PRs should describe motivation, surface test evidence (`test_inode_layer`, `cargo test`, FUSE logs), and flag any C ↔ Rust interface updates or follow-up work.

# Rust engineering

## Recognize the project

- Read `Cargo.toml` first: `edition` (2021 or 2024, which changes language rules), `rust-version` (the minimum supported Rust version, never use a newer feature), `[workspace]` members, `[workspace.dependencies]`, `[workspace.lints]`, `[features]` and `[profile.*]`.
- Read `rust-toolchain.toml` for the pinned toolchain and components, `Cargo.lock` (committed for binaries and usually for workspaces), `.cargo/config.toml` for aliases, target flags and linkers, `clippy.toml`, `rustfmt.toml`, `deny.toml` and `build.rs` scripts.
- Find the real commands in `Makefile`, `justfile`, `xtask/` and the CI workflows. The defaults are `cargo build`, `cargo test`, `cargo clippy --all-targets --all-features` and `cargo fmt --all -- --check`.
- Note whether the crate is a library, a binary or both, whether it is `no_std`, and which async runtime it uses. These decide the error, panic and dependency rules below.

## Architecture

- Split a workspace into crates along real boundaries: a domain crate with no I/O, adapter crates for storage and external services, and thin binaries that wire them. Crates compile in parallel and enforce dependency direction at build time.
- The domain defines traits for what it needs (`trait OrderRepository`), and adapters implement them. Dependencies point inward, and the domain never depends on `axum`, `sqlx` or `tokio` types in its public API unless the project decided so.
- Use generics with trait bounds for static dispatch in hot or library code, and `Box<dyn Trait>` or `Arc<dyn Trait>` where heterogeneous values or smaller binaries matter. Do not add a trait with a single implementation only for mocking when a fake through the real type works.
- Keep the binary's `main` small: parse configuration, build the dependency graph, run, map the final error to an exit code.

## Project structure

```
myservice/
├── Cargo.toml               # Workspace manifest with shared dependencies, lints and profiles
├── Cargo.lock
├── rust-toolchain.toml
├── crates/
│   ├── domain/              # Types, rules and traits, no I/O
│   │   └── src/lib.rs
│   ├── storage/             # sqlx repositories and migrations
│   │   ├── migrations/
│   │   └── src/lib.rs
│   └── api/                 # axum routes, extractors, error responses
│       └── src/lib.rs
├── bins/
│   └── server/
│       └── src/main.rs      # Configuration, wiring, runtime start, shutdown
├── tests/                   # Integration tests of the binary crate when there is one at the root
└── xtask/                   # Project automation written in Rust, when the project uses it
```

- Inherit versions with `dep = { workspace = true }` and lints with `[lints] workspace = true` so every crate shares one policy.
- One module per file, named for what it holds. Use `foo.rs` with a `foo/` folder for submodules in new code unless the project uses `mod.rs`.
- Unit tests live in a `#[cfg(test)] mod tests` at the bottom of the file, integration tests in the crate's `tests/` folder and examples in `examples/`.
- Keep the public API deliberate: `pub(crate)` by default, `pub` only for what consumers use, and re-export the surface from `lib.rs`.
- Document public items with `///` and modules or crates with `//!`, including an `# Errors` section for fallible functions and `# Panics` or `# Safety` where they apply. Other comments stay rare.

## Patterns and practices

### Ownership and types

- Borrow in parameters (`&str`, `&[T]`, `&Path`, `impl AsRef<Path>`) and return owned values. Take ownership (`String`, `Vec<T>`) only when the function stores or consumes the value.
- Clone deliberately. A `.clone()` added to silence the borrow checker usually hides a design problem, while cloning an `Arc` to share across tasks is normal.
- Use newtypes (`struct OrderId(Uuid)`) for identifiers, units and validated values, with a constructor that validates and no public field, so invalid states cannot be built.
- Model closed sets with `enum` and match exhaustively without a catch-all `_` arm on your own enums, so adding a variant forces every match to be revisited. Mark public enums and structs that may grow with `#[non_exhaustive]`.
- Prefer `impl Trait` in argument position for simple generic parameters, `where` clauses for complex bounds, and implement standard traits (`Debug`, `Clone`, `PartialEq`, `Display`, `From`, `TryFrom`, `Default`) where they make sense.
- Mark functions whose result must be used with `#[must_use]`.
- Use iterators and combinators where they read clearly, and a plain `for` loop where they do not.

### Errors

- Return `Result<T, E>` for every fallible operation and propagate with `?`.
- In libraries define error enums with `thiserror`, one variant per failure a caller can act on, with `#[from]` or `#[source]` to keep the cause.
- In binaries and at the top of applications use `anyhow::Result` and add context with `.context("read config file")` or `.with_context(|| format!("open {}", path.display()))`.
- Never use `unwrap()` or `expect()` in library code or request paths. They are acceptable in tests, in `main` during startup with a message that explains the invariant, and on values whose invariant is proven locally. `panic!` is for bugs, not for input.
- Map errors to responses at the boundary, for example an `impl IntoResponse for ApiError` in axum, and never leak internal error text to clients.

### Async with Tokio

- Never block the runtime: no `std::thread::sleep`, synchronous file or network I/O, heavy CPU work or blocking locks inside async code. Use `tokio::time::sleep`, `tokio::fs`, and `tokio::task::spawn_blocking` for blocking or CPU-heavy work.
- Never hold a `std::sync::Mutex` guard across an `.await`. Scope the guard so it drops before awaiting, or use `tokio::sync::Mutex` only when the lock must span an await.
- Every spawned task has an owner. Keep `JoinHandle`s or use a `JoinSet`, propagate shutdown with `tokio_util::sync::CancellationToken`, and await tasks on shutdown.
- Know the cancellation safety of every future in `tokio::select!`: a branch that loses is dropped, and a future such as `read_exact` or a hand-written state machine can lose data. Pin long-lived futures outside the loop and select on references.
- Bound every queue (`mpsc::channel(n)`, not `unbounded_channel`) and every fan-out with a `Semaphore` or `buffer_unordered(n)`.
- Put timeouts on outbound calls with `tokio::time::timeout` or the client's own timeout.
- Install `tracing` with `tracing-subscriber` in the binary, instrument with `#[tracing::instrument(skip(secret_args))]`, and log fields, not formatted strings. Libraries emit `tracing` events and never install a subscriber.

### Features and configuration

- Features are additive: enabling a feature never removes an API or changes behavior others rely on. Gate optional dependencies with `dep:` syntax.
- Check feature combinations the project supports, with `cargo hack --each-feature` when the project uses it.
- Load configuration into a typed struct with `serde`, validate it at startup, and keep secrets in `secrecy::SecretString` or an equivalent when the project uses it so they never print through `Debug`.

## Data, networking and persistence

- Serialize with `serde` derives. Use `#[serde(deny_unknown_fields)]` for strict inputs, `#[serde(rename_all = "camelCase")]` to match the wire format, and separate request and response types from domain types.
- Build axum services with `Router`, typed extractors and `State`, and use the path syntax of the version the project pins (`/{id}` from axum 0.8, `/:id` before). Add `tower-http` layers for tracing, timeouts, compression and request body limits, and keep `DefaultBodyLimit` appropriate for each route.
- In actix-web use `web::Data`, extractors and `ResponseError`, and keep blocking work in `web::block`.
- With `sqlx` prefer the checked macros `query!` and `query_as!`, which verify SQL against the database at compile time. Commit the `.sqlx` folder produced by `cargo sqlx prepare` and build with `SQLX_OFFLINE=true` in CI. Bind every value, never `format!` SQL.
- Run migrations with `sqlx::migrate!()` or the tool the project uses, keep them reversible or forward-only as it decides, and never edit an applied migration.
- Share one connection pool through application state, use transactions with `pool.begin()` and commit explicitly, since dropping a transaction rolls it back.
- Use `reqwest` with one shared `Client` configured with timeouts and `rustls` when the project chooses it.

## Security

- Forbid unsafe code in crates that do not need it with `#![forbid(unsafe_code)]` or `unsafe_code = "forbid"` in `[lints.rust]`.
- When `unsafe` is unavoidable, keep the block minimal, wrap it in a safe abstraction, write a `// SAFETY:` comment that proves every invariant, document `# Safety` on every `unsafe fn`, and test it under Miri (`cargo +nightly miri test`) when the code allows. Enable `clippy::undocumented_unsafe_blocks`. Edition 2024 requires `unsafe {}` inside `unsafe fn` and `unsafe extern` blocks, and makes `std::env::set_var` unsafe.
- Validate untrusted input sizes before allocating, and use checked or saturating arithmetic (`checked_add`, `try_from`) on lengths and counts. Integer overflow panics in debug and wraps in release.
- Treat `as` casts between numeric types as truncating. Use `TryFrom` where values may not fit.
- Use `ring`, `rustls`, `argon2` or other well-reviewed RustCrypto crates, `rand::rngs::OsRng` or `getrandom` for secrets, and `subtle` for constant-time comparison.
- Audit dependencies with `cargo audit` and enforce licenses, sources, bans and advisories with `cargo deny check`.

## Performance

- Measure with `criterion` or `divan` benchmarks and profile with `cargo flamegraph` or `perf` before optimizing.
- Avoid needless allocation: `&str` over `String`, `Cow<'_, str>` when ownership varies, `Vec::with_capacity`, and reuse buffers in loops.
- Prefer static dispatch in hot paths and avoid `Box<dyn Fn>` per call.
- Use `rayon` for data-parallel CPU work, never inside the async runtime without `spawn_blocking`.
- Tune release builds in `[profile.release]` as the project decides, for example `lto = "thin"`, `codegen-units = 1`, `strip = true`, and `panic = "abort"` only when nothing relies on unwinding.

## Tests

- Unit tests sit in the module they test, integration tests in `tests/` use only the public API, and doc examples are tests that must compile and pass. Mark examples that must not run with `no_run` or `ignore` only for a stated reason.
- Name tests for the behavior (`rejects_quantity_below_one`). Use `#[tokio::test]` for async tests, `rstest` for parameterized cases when the project uses it, and `proptest` for properties of parsers, encoders and invariants.
- Fake external services with hand-written implementations of the trait, `wiremock` for HTTP, and `#[sqlx::test]` for tests against a real database with migrations applied.
- Snapshot outputs with `insta` when the project uses it, and review changes with `cargo insta review`.
- Run `cargo test --workspace --all-features`. With `cargo nextest run`, also run `cargo test --doc`, since nextest does not run doc tests.
- Measure coverage with `cargo llvm-cov --workspace --all-features --lcov --output-path lcov.info` and read the summary or `cargo llvm-cov --html`. Use `--fail-under-lines` when the project declares a threshold. Use `cargo tarpaulin` only where the project already does.

## Tooling and quality gates

- Format with `cargo fmt --all` and check with `cargo fmt --all -- --check`.
- Lint with `cargo clippy --workspace --all-targets --all-features -- -D warnings`. Enable `clippy::pedantic` or other groups only as the project's `[workspace.lints]` decides, and fix findings rather than adding `#[allow]`. When a suppression is unavoidable, prefer `#[expect(lint, reason = "...")]` on the narrowest item.
- Build documentation with `RUSTDOCFLAGS="-D warnings" cargo doc --no-deps` when the project publishes docs.
- Check the minimum supported version in CI with the toolchain of `rust-version` when the project declares one.
- Check public API changes of published crates with `cargo semver-checks` when the project uses it.

## Build, configuration and release

- Keep `Cargo.lock` committed for applications and build CI with `--locked` so dependency resolution never changes silently.
- Add dependencies with `cargo add` using the workspace table, with default features off when only part is needed (`default-features = false, features = [...]`).
- Cross-compile with `rustup target add` and `cross` or `cargo zigbuild` as the project does. Build fully static Linux binaries with the `x86_64-unknown-linux-musl` target when the project ships them.
- Version crates with SemVer, update `CHANGELOG.md` where it exists, and publish with `cargo publish --dry-run` first. Release tooling such as `cargo-release`, `release-plz` or `cargo-dist` decides tags and artifacts when present.
- Keep `build.rs` minimal and print `rerun-if-changed` directives so it does not rerun on every build.

## Pitfalls

- The `unwrap()` on input, environment values or lock poisoning in request paths turns bad data into a crash.
- Holding a lock or a `RefCell` borrow across an `.await` or a callback causes deadlocks, panics or `Send` errors.
- The `tokio::spawn` without keeping the handle detaches the task, so its panic and error are lost.
- Blocking calls inside async code stall every task on that worker thread.
- Fighting the borrow checker with `clone`, `Rc<RefCell<_>>` or `'static` bounds everywhere instead of restructuring ownership.
- The `String` and `&str` are UTF-8, and slicing with byte indices can panic in the middle of a character. Use `char_indices` or `get(..)`.
- A `Drop` that does I/O cannot report errors and cannot await. Provide an explicit `close` or `shutdown` method.
- Sorting floats with `partial_cmp().unwrap()` panics on NaN. Use `total_cmp`.
- Feature flags that are not additive break downstream crates that enable different sets.
- Edition differences matter: the 2024 edition changes `impl Trait` capture rules, temporaries in `if let` and tail expressions, and reserves `gen`. Follow the project's edition when copying examples.

## Definition of done

- The command `cargo build --locked` succeeds for every target and feature set the project supports, with the toolchain of `rust-toolchain.toml` and within `rust-version`.
- The command `cargo fmt --all -- --check` reports no difference.
- The command `cargo clippy --all-targets --all-features -- -D warnings` reports nothing, and every `#[allow]` or `#[expect]` has a reason.
- No `unwrap`, `expect` or `panic!` was added to library or request paths.
- Library errors are typed with `thiserror`, and application errors carry context.
- No blocking call runs inside async code, every task has an owner, and every `select!` branch is cancellation safe.
- Every `unsafe` block has a `// SAFETY:` comment and a safe wrapper, and unsafe-free crates forbid it.
- SQL is checked by `sqlx` macros or bound with parameters, and the offline query data is up to date.
- The command `cargo test --workspace` passes, including doc tests, and coverage was read from `cargo llvm-cov`.
- The command `cargo deny check` or `cargo audit` reports no advisory the project has not accepted.
- Public items have `///` documentation with `# Errors`, `# Panics` and `# Safety` sections where they apply.

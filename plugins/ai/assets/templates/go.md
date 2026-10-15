# Go engineering

## Recognize the project

- Read `go.mod` first: the `module` path, the `go` directive (the minimum language version, which also decides semantics such as per-iteration loop variables from 1.22), the optional `toolchain` line and, from Go 1.24, the `tool` directives that pin developer tools. Never use a feature newer than the `go` directive.
- A `go.work` file means a multi-module workspace. Run commands from the module you change, and never commit a `go.work` the project does not already commit.
- A `vendor/` folder means the build uses vendored modules. After any dependency change run `go mod tidy` and then `go mod vendor`, and commit both.
- Find the real commands in `Makefile`, `Taskfile.yml`, `magefile.go`, `scripts/` and the CI workflows. The defaults are `go build ./...`, `go test ./...`, `go vet ./...` and `golangci-lint run`.
- Read `.golangci.yml` (note `version: "2"` for golangci-lint v2, whose configuration differs from v1), `sqlc.yaml`, `buf.yaml`, `Dockerfile` and `.goreleaser.yaml` when present. They decide generation, linting and release.
- Generated code carries a `// Code generated ... DO NOT EDIT.` header. Change its source (the SQL, the proto, the `//go:generate` directive) and regenerate with `go generate ./...` or the tool the project names, never edit the output.

## Architecture

- Organize by domain, not by technical layer. A package is named for what it provides (`billing`, `order`, `postgres`), never `utils`, `common`, `helpers`, `models` or `types`.
- Keep the dependency direction one way: `cmd` wires everything, transport packages (`http`, `grpc`) call domain packages, and domain packages declare the small interfaces they need from storage and external services. The domain never imports the transport or the database driver.
- Define an interface in the package that consumes it, with only the methods that consumer calls. Accept interfaces, return concrete structs. Do not declare an interface next to its only implementation "for testing".
- Wire dependencies by hand in `main` or a `run` function through constructors such as `NewService(store Store, clock Clock) *Service`. Avoid global state, `init()` side effects and service locators. Use code generation for wiring only when the project already does.
- Prefer a flat structure that grows when a boundary appears. A small service can be a handful of packages, and a new layer needs a reason the code shows.

## Project structure

```
myservice/
├── cmd/
│   └── myservice/
│       └── main.go          # Parses config, wires dependencies, calls run(ctx) and exits
├── internal/
│   ├── order/               # Domain package: types, rules, service, consumer-side interfaces
│   ├── billing/             # Another domain package
│   ├── postgres/            # Store implementations, sqlc output, migrations embedding
│   ├── httpapi/             # Handlers, routing, middleware, request and response types
│   └── config/              # Typed configuration loaded from flags and environment
├── migrations/              # Versioned SQL migrations
├── pkg/                     # Only for code other modules are meant to import
├── testdata/                # Fixtures and golden files, ignored by the go tool
├── go.mod
└── go.sum
```

- Put everything that is not a deliberate public API under `internal/`, which the compiler forbids other modules to import. Create `pkg/` only when external consumers exist.
- One binary per folder under `cmd/`, each with a tiny `main` that calls `run(ctx, args, getenv, stdout, stderr) error` so the whole program is testable.
- Tests live beside the code in `_test.go` files. Use package `foo_test` for black-box tests of the public API and package `foo` only when a test needs unexported details.
- Fixtures go in `testdata/`. Embed static assets and migrations with `//go:embed`.
- Document every exported identifier with a doc comment that starts with its name (`// Store persists orders.`), as `go vet` and linters expect, and keep other comments rare.

## Patterns and practices

### Errors

- Return errors as the last value and handle each one where it occurs. Never discard an error with `_` unless the call truly cannot fail in a way that matters, and then the reason is obvious from the code.
- Wrap with context using `fmt.Errorf("load order %d: %w", id, err)`. The message describes the operation in lowercase without "failed to" and without trailing punctuation.
- Declare sentinel errors (`var ErrNotFound = errors.New("order not found")`) or typed errors for the cases callers branch on, and test them with `errors.Is` and `errors.As`, never by comparing strings or with `==` on wrapped errors.
- Translate errors at boundaries: the store maps `sql.ErrNoRows` or `pgx.ErrNoRows` to the domain `ErrNotFound`, and the HTTP layer maps domain errors to status codes. Do not leak driver errors to clients.
- Use `errors.Join` to return several independent failures. Use `%w` only for errors the caller may inspect, since wrapping makes them part of your API.
- Do not panic for expected failures. A panic is for programmer errors, and an HTTP server recovers per request only as a last resort.

### Context and goroutines

- Pass `ctx context.Context` as the first parameter of every function that does I/O, waits or may be cancelled. Never store a context in a struct and never pass `nil`, use `context.TODO()` only while migrating.
- Respect cancellation: select on `ctx.Done()` in loops and blocking waits, and pass the context to every database, HTTP and RPC call.
- Every goroutine has an owner that knows how it ends. Start goroutines with `errgroup.WithContext` (from `golang.org/x/sync/errgroup`) or a `sync.WaitGroup` (Go 1.25 adds `wg.Go`), and wait for them before returning.
- A goroutine that sends on a channel nobody reads, or waits on a channel nobody closes, leaks. Close a channel only from the sender side, and use buffered channels only with a reason.
- Use channels to transfer ownership or signal events, and a `sync.Mutex` to protect shared state. Keep the critical section short and never call out to unknown code while holding a lock. Do not copy a struct that contains a mutex.
- Bound concurrency with a semaphore, `errgroup.SetLimit` or a worker pool. Never start one goroutine per item of unbounded input.
- Use `signal.NotifyContext(ctx, os.Interrupt, syscall.SIGTERM)` in `main` and shut servers down with `srv.Shutdown(ctx)` under a deadline.

### Configuration and logging

- Load configuration once at startup into a typed struct from flags and environment, validate it, and fail fast with a clear message. Pass the struct, or the pieces each component needs, through constructors.
- Log with `log/slog`. Build one `*slog.Logger` in `main` with a JSON handler in production, inject it, and attach request-scoped attributes with `logger.With`. Use key-value pairs, never formatted strings, and never log secrets, tokens or full request bodies.
- Use `time.Duration` for every timeout and interval, and inject a clock when tests depend on time.

### Language idioms

- Make the zero value useful, and use constructors only when invariants require them.
- Use functional options (`WithTimeout(d)`) only for constructors with several optional settings, otherwise take a config struct or plain parameters.
- Use generics for algorithms over many types and type-safe containers, not to abstract code that has one concrete type.
- Use `defer` for cleanup right after acquiring a resource, and check the error of `Close` on writers, where it reports lost data.
- Use iterators (`iter.Seq`, Go 1.23) and `slices`/`maps` package functions where the `go` directive allows, instead of hand-written loops for sorting and searching.

## Data, networking and persistence

- Configure every `http.Server` with `ReadHeaderTimeout`, `ReadTimeout`, `WriteTimeout` and `IdleTimeout`. The zero values mean no timeout and expose the server to slow clients.
- Never use `http.Get` or `http.DefaultClient` for outbound calls. Build an `*http.Client` with a `Timeout` and a tuned `Transport`, reuse it, pass `ctx` with `http.NewRequestWithContext`, always close `resp.Body`, and check the status code before decoding.
- Limit request bodies with `http.MaxBytesReader`, decode JSON with `json.NewDecoder` and `DisallowUnknownFields` when the contract is strict, and validate the decoded value before using it.
- Use the routing patterns of `net/http` (`mux.HandleFunc("GET /orders/{id}", h)` and `r.PathValue("id")`, Go 1.22) unless the project already uses a router such as chi.
- With `database/sql`, set `SetMaxOpenConns`, `SetMaxIdleConns` and `SetConnMaxLifetime`, use the `Context` variants of every call, always `defer rows.Close()` and check `rows.Err()` after the loop.
- With PostgreSQL prefer `pgx` v5 through `pgxpool`, and keep queries in `sqlc` when the project uses it: write the SQL, run `sqlc generate`, and commit the generated code.
- Run a transaction in one function that begins, defers `Rollback` (harmless after commit) and commits at the end. Keep network calls out of transactions.
- Apply migrations with the tool the project uses (goose, golang-migrate, atlas), keep them forward-only and safe for a running previous version.

## Security

- Build SQL only with placeholders (`$1` for pgx, `?` for MySQL). Never use `fmt.Sprintf` for query text, including identifiers, which come from an allowlist.
- Run commands with `exec.CommandContext(ctx, name, args...)`. Never pass input to `sh -c`.
- Render HTML only with `html/template`, never `text/template`, and never convert input to `template.HTML`.
- Resolve user-supplied paths inside a root with `os.Root` (Go 1.24) or `filepath.Rel` checks after `filepath.Clean`, and refuse anything that escapes.
- Use `crypto/rand` for tokens and keys, never `math/rand`. Compare secrets with `subtle.ConstantTimeCompare`.
- Leave `tls.Config.InsecureSkipVerify` false, and set `MinVersion: tls.VersionTLS12` or higher when you build a TLS config.
- Run `govulncheck ./...` and fix reachable vulnerabilities by upgrading the module or the toolchain.

## Performance

- Measure before optimizing. Write benchmarks (`for b.Loop()` from Go 1.24, or `b.N` on older versions), run them with `-benchmem` and compare with `benchstat`. Profile with `pprof` (`net/http/pprof` only on an internal port).
- Preallocate slices and maps when the size is known, reuse buffers in hot paths, and avoid converting between `[]byte` and `string` in loops.
- Stream large payloads with `io.Reader` and `io.Writer` instead of reading them whole.
- Keep allocations off hot paths: check escape analysis with `go build -gcflags=-m` when a benchmark shows allocations.
- Use `sync.Pool` only for short-lived, frequently allocated objects a profile points at.

## Tests

- Use the standard `testing` package. Add `testify` or `go-cmp` only when the project already uses them, and prefer `cmp.Diff` for comparing structs.
- Write table-driven tests with named cases run through `t.Run(tc.name, ...)`. Mark helpers with `t.Helper()`, register cleanup with `t.Cleanup`, use `t.TempDir()` for files, `t.Setenv` for environment and `t.Context()` (Go 1.24) for a context cancelled at the end of the test.
- Test handlers with `httptest.NewRecorder` and whole servers or clients with `httptest.NewServer`. Fake dependencies with small hand-written structs that satisfy the consumer's interface.
- Test against a real database with testcontainers-go or the project's compose setup, behind a build tag or `testing.Short()` check if the project separates integration tests.
- Use `t.Parallel()` where tests share nothing, and use `testing/synctest` (Go 1.25) for time-dependent concurrent code instead of sleeps.
- Write fuzz tests (`func FuzzParse(f *testing.F)`) for parsers and decoders. Run them with `go test -fuzz=FuzzParse -fuzztime=30s ./internal/parse`, and commit failing inputs that land in `testdata/fuzz`.
- Run the race detector on every test run that CI allows: `go test -race ./...`.
- Measure coverage with `go test -race -covermode=atomic -coverprofile=coverage.out ./...`, read it with `go tool cover -func=coverage.out` and `go tool cover -html=coverage.out`, and add `-coverpkg=./...` when tests in one package exercise others.
- Golden files live in `testdata/` and are refreshed through an `-update` flag the test defines, never by hand.

## Tooling and quality gates

- Format with `gofmt` and organize imports with `goimports` (or `golangci-lint fmt` in v2). Code that is not formatted is not done.
- Run `go vet ./...` and the linter the project configures, usually `golangci-lint run ./...` with `staticcheck`, `errcheck`, `govet`, `ineffassign` and `unused` at least. Fix findings instead of adding `//nolint`, and when a suppression is unavoidable it names the linter and the reason.
- Pin developer tools in `go.mod` with `tool` directives and run them with `go tool <name>` when the project uses Go 1.24 or newer, otherwise follow its `tools.go` or Makefile convention.
- Keep `go.mod` and `go.sum` tidy: CI should fail when `go mod tidy` changes them. Run `go mod verify` to check downloaded modules.

## Build, configuration and release

- Build static binaries with `CGO_ENABLED=0 go build -trimpath -ldflags="-s -w -X main.version=$VERSION" ./cmd/myservice`. Cross-compile with `GOOS` and `GOARCH`. Enable cgo only when a dependency truly needs it, and then build in the target environment.
- Read version information from `debug.ReadBuildInfo()` or `-X` linker flags, never hard-code it.
- Ship the binary in a minimal image such as `scratch` or distroless, adding CA certificates and time zone data (`import _ "time/tzdata"`) when needed.
- Release with the tool the project uses, often GoReleaser, which builds archives, checksums and container images from tags.
- Library modules follow semantic import versioning: a breaking change in v2 or later changes the module path to `.../v2`.
- Upgrade dependencies deliberately with `go get module@version`, then `go mod tidy`, the tests and `govulncheck`.

## Pitfalls

- Capturing the loop variable in goroutines or closures is safe only when the `go` directive is 1.22 or newer. On older modules copy it first.
- A nil pointer of a concrete type stored in an interface is not a nil interface. Return a literal `nil` for "no error".
- Writing to a nil map panics, and maps are not safe for concurrent writes. Use a mutex or `sync.Map` only where its access pattern fits.
- Appending to a slice that shares a backing array with another slice silently overwrites the other. Copy with `slices.Clone` when ownership is unclear.
- The `defer` inside a loop runs at function return, not per iteration. Move the body into a function.
- The `time.After` in a long-lived loop and forgotten `time.Ticker` instances leak timers on old Go versions. Stop tickers.
- Writing headers after `w.WriteHeader` or the first `Write` has no effect, and the handler must `return` after writing an error response.
- A forgotten `resp.Body.Close()` or `rows.Close()` leaks connections until the pool is exhausted.
- The `json` ignores unexported fields and silently zeroes missing ones. Tag every field and decide how absence is modelled.
- Using `log.Fatal` or `os.Exit` outside `main` skips deferred cleanup and makes code untestable.

## Definition of done

- The code builds with the `go` directive of `go.mod`, and `go build ./...` succeeds for every target platform the project ships.
- The command `gofmt` and `goimports` produce no changes, and `go vet ./...` and the project linter report nothing.
- The command `go mod tidy` leaves `go.mod`, `go.sum` and `vendor/` unchanged.
- Every error is handled or returned with context, wrapped with `%w` where callers inspect it.
- Every I/O function takes a context, and every goroutine has an owner that waits for it.
- Servers and clients have timeouts, bodies are limited and closed.
- Generated code was regenerated from its source and committed.
- Tests are table-driven where cases vary, pass with `go test -race ./...`, and coverage of the changed packages was read from `go tool cover`.
- Parsers and decoders that take untrusted input have fuzz targets.
- The command `govulncheck ./...` reports no reachable vulnerability.
- Exported identifiers have doc comments that start with their name.

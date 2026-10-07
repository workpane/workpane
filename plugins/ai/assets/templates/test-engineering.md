# Test engineering

Your job is to create and improve the tests of this project so they catch real defects, run fast and stay trustworthy. Coverage is a measure you report, not the goal. A suite is strong when breaking a behavior makes a test fail for the right reason, and weak when tests pass whatever the code does.

## 1. Discover the test stack and its conventions

- Find the frameworks and runners in the manifests and lock files: `package.json` with Vitest, Jest, Mocha, Playwright or Cypress, `pyproject.toml` or `setup.cfg` with pytest, `go.mod` with the standard `testing` package and helpers such as testify, `Cargo.toml` with `cargo test` and nextest, `build.gradle(.kts)` or `pom.xml` with JUnit 5, Kotest, Mockito or MockK, `.csproj` with xUnit, NUnit or MSTest, `composer.json` with PHPUnit or Pest, `Gemfile` with RSpec or Minitest, `pubspec.yaml` with `flutter_test`, Xcode schemes with XCTest or Swift Testing.
- Read the configuration: test roots, file name patterns, setup files, environment variables, coverage settings and thresholds, and the continuous integration job that runs them. Use the exact commands CI uses.
- Read five or six existing tests in different layers to learn the naming, the folder layout, the arrange-act-assert style, the fixtures, the factories and the fakes the project already has. New tests look like their neighbors.
- Run the whole suite once before changing anything and record the result and the time. A suite that is already red or flaky is the first thing to report, with the failing names.

## 2. Measure coverage with the project's tool

Use the coverage tool the project declares. When it declares none, use the standard one of the ecosystem, run inside the project environment:

| Ecosystem | Command | Report |
| --- | --- | --- |
| Vitest | `npx vitest run --coverage` | `coverage/` with the configured reporters |
| Jest | `npx jest --coverage` | `coverage/lcov-report/index.html` |
| Python | `pytest --cov=<package> --cov-branch --cov-report=term-missing` | Terminal, or `--cov-report=html` |
| Go | `go test -covermode=atomic -coverpkg=./... -coverprofile=cover.out ./...` then `go tool cover -func=cover.out` | `go tool cover -html=cover.out` |
| Rust | `cargo llvm-cov --html` | `target/llvm-cov/html` |
| JVM with JaCoCo | `./gradlew test jacocoTestReport` or `mvn verify` with the JaCoCo plugin | `build/reports/jacoco` or `target/site/jacoco` |
| Kotlin with Kover | `./gradlew koverHtmlReport` and `./gradlew koverVerify` | `build/reports/kover` |
| .NET | `dotnet test --collect:"XPlat Code Coverage"` | Cobertura XML, rendered by ReportGenerator when the project has it |
| PHP | `XDEBUG_MODE=coverage vendor/bin/phpunit --coverage-text` or with PCOV | Terminal or HTML |
| Ruby | SimpleCov started in the spec helper | `coverage/index.html` |
| Swift | `swift test --enable-code-coverage`, or `xcodebuild test -enableCodeCoverage YES` then `xcrun xccov view --report` on the result bundle | Terminal |
| Flutter | `flutter test --coverage` | `coverage/lcov.info` |
| C and C++ | The coverage build of the project with `gcovr` or `llvm-cov` | HTML or terminal |

Read the report per file and per branch, not only the total. Lines that are covered but never asserted on are not tested, which is why the report is a map, not a verdict.

## 3. Find untested behavior by risk

Rank what to test by how much a defect would cost and how likely it is, not by which file has the lowest number:

- Business rules: pricing, permissions, state transitions, limits, eligibility, calculations of money and time.
- Error paths: every `catch`, every error return, every refusal of invalid input, every timeout and retry. These are the least covered and the most likely to fail in production.
- Boundaries: zero, one, many, the maximum, empty strings, Unicode, negative numbers, the first and last page, leap days and time zone transitions.
- Security checks: authorization on every object and function, validation, escaping, rate limits. A test that user B cannot read user A's object is among the most valuable in any suite.
- Concurrency: races on shared state, double submission, idempotency of handlers that can be retried, ordering of events.
- Integration seams: serialization, database queries and migrations, configuration loading, and the contracts with other services.
- Recently changed and frequently broken code, which the history shows with `git log --format= --name-only | sort | uniq -c | sort -rn | head`.

Write the list in your plan with the test each item needs, before writing tests.

## 4. Choose the layer per behavior

- **Unit tests** for pure logic, rules, parsers and calculations. Fast, many, no I/O.
- **Integration tests** for code whose correctness depends on a real collaborator the project owns or can run: queries against a real database in a container or an embedded engine, HTTP handlers through the framework's test client, message handlers with an in-memory broker. Mocking the database to test a query proves nothing.
- **Contract tests** for the boundary between services: consumer-driven contracts with Pact, or validation of requests and responses against the OpenAPI or JSON schema the API publishes, for example with Schemathesis.
- **End-to-end and UI tests** sparingly, for the few journeys whose failure would stop the business, such as sign up, checkout and the main workflow. Use stable selectors such as roles, labels and `data-testid`, never CSS paths or text that translators change, and wait for conditions, never for fixed delays.

Test each behavior at the lowest layer that can prove it, and once.

## 5. Write deterministic tests

- **Time**: inject a clock or use the framework's fake timers, such as `vi.useFakeTimers`, `freezegun` or `time-machine`, a `Clock` parameter in Java and Kotlin, or a time source in Go. Never sleep to wait for something to happen.
- **Randomness**: seed it or inject the generator, and print the seed when a property test fails.
- **Network**: no real external calls. Use a fake server bound to loopback, such as WireMock, MSW, `responses`, `httptest.Server` or `nock`, or a fake of the client. Block outbound network in the test environment when the project allows it.
- **File system**: temporary directories from the framework, such as `tmp_path`, `t.TempDir()`, `@TempDir` or `tempfile`, cleaned automatically. Never write into the repository.
- **Environment**: set and restore environment variables and global state inside the test, and never depend on the developer's machine, locale or time zone. Set the time zone and locale explicitly when a behavior depends on them.
- **Order**: every test creates the data it needs and passes alone, in any order and in parallel. Run with random order when the runner supports it.

## 6. Fakes, mocks, fixtures and builders

- Prefer fakes that behave like the real thing, such as an in-memory repository with the same contract, over mocks that script calls. A fake is reused across tests and catches contract mistakes.
- Use mocks for what you do not own and cannot run, and for verifying an interaction that is the behavior itself, such as "an email is sent". Never mock the code under test, value objects or the language's standard library.
- Build test data with factories or builders that give valid defaults and let each test state only what matters to it, such as `anOrder().withQuantity(0).build()` or a factory with traits. Avoid large shared fixture files that every test depends on.
- Keep test helpers in the support folder of the project, and do not let them grow logic that itself needs tests.

## 7. Stronger techniques where they pay

- **Property-based tests** for parsers, serializers, encoders, sorting, money and date arithmetic and anything with an invariant, such as "decoding an encoded value gives the value back". Use Hypothesis, fast-check, proptest, jqwik or the library the project has.
- **Fuzz tests** for code that parses untrusted input: `go test -fuzz`, `cargo fuzz`, Atheris, Jazzer or libFuzzer. Commit the crashing inputs as regular test cases.
- **Snapshot and golden tests** only for output that is large and stable, such as generated code, rendered documents or serialized formats. Keep snapshots small and reviewed, never update them blindly with an update flag, and prefer explicit assertions when the output is short.

## 8. Diagnose flaky tests

- Reproduce first: run the test in a loop, in random order and in parallel, such as `pytest --count=50` when the project has `pytest-repeat` and `pytest-randomly`, `go test -count=100 -race -run TestName`, or the runner's repeat option.
- Look for the usual causes: shared state between tests, real time and sleeps, unordered collections compared as ordered, asynchronous work not awaited, ports and files reused, external services, and time zone or locale assumptions.
- Fix the cause in the test or in the code. A retry annotation or a longer timeout hides the defect and is not a fix.

## 9. Judge strength with mutation testing

When coverage is already high, measure whether the tests would notice a change in the code. Mutation tools alter the code, such as flipping a condition or removing a call, and check that some test fails:

- JavaScript and TypeScript: Stryker, `npx stryker run`.
- JVM: PIT, through `mvn org.pitest:pitest-maven:mutationCoverage` or the Gradle plugin.
- Python: mutmut, `mutmut run` then `mutmut results`.
- Rust: cargo-mutants, `cargo mutants`.

Run them on the changed or critical modules only, because they are slow. Each surviving mutant is either a missing assertion, which you add, or an equivalent mutant, which you note. Do not add these tools to the project unasked. Run them through the ecosystem's runner and report the score.

## 10. Reaching 100 percent when required

- Cover every line and branch of the scope through behavior: each branch exists because some input takes it, so find that input and assert on the outcome it produces.
- A branch no input can reach is dead code. Report it and propose its removal instead of forcing a test through it.
- Never test private functions directly, call internals through reflection, or assert on how a result was computed. Test through the public interface, so refactoring keeps the tests green.
- Exclude only what is honestly not the project's logic: generated code, vendored code, build scripts and the entry point that only wires dependencies. Use the configuration of the tool, such as `omit` in coverage settings, `coveragePathIgnorePatterns` or `coverage.exclude`, JaCoCo and Kover excludes, or the generated-file markers the tool recognizes, and list every exclusion in your report with its reason. An inline marker such as `# pragma: no cover`, `/* istanbul ignore next */` or `/* c8 ignore next */` on hand-written logic is cheating and is not allowed.

## 11. CI integration and thresholds

- Make sure the suite and coverage run in continuous integration with the same commands you used, and that a failing test fails the job.
- When the project declares a threshold, keep it and meet it. When the task asks for one, set it in the tool's configuration, such as `fail_under`, `coverageThreshold`, `thresholds` in Vitest, `koverVerify` rules or a JaCoCo rule, at the level reached, so it can only move up.
- Upload or publish the coverage report as the project already does. Do not add a coverage service without a request.
- Keep the suite fast: parallelize where the runner supports it, keep slow integration and end-to-end tests in their own job or tag when the project separates them, and report the time before and after.

## 12. Report

State the coverage before and after for the scope, with the command and the numbers from the tool, the mutation score if you measured one, the behaviors now covered grouped by risk, every exclusion with its reason, flaky tests found and their causes, and production defects the new tests revealed. A test that exposes a real bug is reported as a finding, and the bug is fixed only when the task asks for fixes.

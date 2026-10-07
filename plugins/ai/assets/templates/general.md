# Application engineering

This section applies to any stack that has no dedicated section here. Learn the idioms of the stack in front of you quickly and apply the defaults below only where the project has not already decided.

## Recognize the project

- Identify the language and runtime from the manifests and lock files, such as `package.json`, `pyproject.toml`, `go.mod`, `Cargo.toml`, `pom.xml`, `build.gradle.kts`, `*.csproj`, `composer.json`, `Gemfile`, `mix.exs`, `deno.json`, `CMakeLists.txt`, `Makefile` or `build.zig`. Read the runtime version from version files such as `.nvmrc`, `.python-version`, `.tool-versions`, `rust-toolchain.toml` or the toolchain fields of the manifest.
- Identify the framework from the dependencies and the entry point, then read its official guide for the version pinned, not for the latest release. Idioms change between major versions.
- Find the commands that build, run, test, lint and format in the scripts of the manifest, a task runner such as `make`, `just`, `task` or `npm` scripts, the continuous integration workflows and the development guide. Use them exactly.
- Read the entry point, the composition root where dependencies are wired, one complete feature from its route or command to its storage, and the tests of that feature. That gives you the architecture, the error model and the testing style in one pass.
- Learn the idioms of the ecosystem before writing: its package layout, naming conventions, error handling style, async model, standard formatter and linter, and documentation comment convention. Comments stay rare whatever the convention allows.

## Architecture defaults

When the project has an architecture, follow it. When it has none, or a new area needs one:

- Organize by feature: one module per business capability, such as `orders`, `billing` and `accounts`, each holding its own domain, use cases, adapters and tests. Shared code lives in a small `shared` or `core` module only once two features need it.
- Inside a feature, separate four roles and keep the dependency direction pointing inward:
  - **Domain**: entities, value objects and rules, with no framework, database or transport code.
  - **Application**: use cases that orchestrate the domain, open transactions and call ports.
  - **Infrastructure**: database repositories, HTTP clients, queues, file storage and other adapters that implement the ports.
  - **Interface**: HTTP handlers, command line commands, consumers and views that translate requests into use case calls and results into responses.
- Invert dependencies at boundaries only: the application declares the interface it needs, such as a repository or a payment gateway, and infrastructure implements it. Do not add interfaces inside a layer where there is one implementation and no boundary.
- Wire everything in one composition root at startup, through constructor injection or the container of the framework. Do not reach for globals or service locators inside the domain.
- Scale the layering to the project. A small tool with three commands needs modules and functions, not four layers.

## Configuration and secrets

- Read configuration from the environment and the configuration files the project uses, parse it once at startup into a typed object, and validate it there. A missing or invalid value stops the start with a message that names the key.
- Give each value one source of truth and a documented default only when a safe default exists. Secrets never have defaults.
- Keep secrets in the environment or the secret manager of the deployment, keep `.env` files ignored, and commit an example file with placeholder values that lists every key.
- Separate configuration by environment through values, never through branches of code that test the environment name.

## Error model

- Distinguish expected failures, such as validation, not found, conflict and permission denied, from unexpected ones, such as a bug or an unavailable dependency. Model expected failures in the domain with stable error codes in `snake_case`.
- Map errors to the transport once, at the interface layer. Domain code never knows status codes.
- Unexpected errors are logged with their context and a correlation identifier, and the caller receives a generic message with that identifier, never a stack trace.
- Wrap errors with context as they cross layers, keeping the original cause, and never catch an error only to return an empty value.

## Logging and observability

- Log structured events, such as JSON or key-value pairs, through the project's logger, with a level that matches the action needed: `error` for something that needs attention, `warn` for a degraded but handled state, `info` for business events, `debug` for diagnosis.
- Attach a request or job identifier to every log line of that unit of work, and propagate trace context to outbound calls when the project uses tracing, such as OpenTelemetry.
- Never log secrets, tokens, passwords or personal data. Redact at the logger rather than at each call.
- Expose health endpoints that distinguish liveness from readiness, and metrics for request rate, errors and latency of each endpoint and job when the project collects metrics.

## Data access and migrations

- Access the database through the repositories or the query layer of the project, with parameterized queries. Keep queries next to the feature that owns the table.
- Change the schema only through versioned migrations committed with the code, applied by the migration tool of the stack. Never edit a migration that has run anywhere shared.
- Make migrations safe on live data: add nullable columns or columns with constant defaults, backfill in batches, add indexes concurrently where the database allows, and drop columns only after no running code reads them.
- Use transactions around writes that must succeed together, keep them short and never call external services inside them. Use constraints, such as unique, foreign key and check, to enforce invariants the database can enforce.
- Avoid N+1 queries by loading related data in one query, and index every column used to filter, join or sort on a hot path.

## API design

- Name REST resources as plural nouns with identifiers in the path, such as `GET /orders/{id}` and `POST /orders/{id}/cancellations` for an action that is not plain CRUD. Use the HTTP methods for their meaning.
- Return accurate status codes: `200` with a body, `201` with a `Location` header for creation, `202` for accepted asynchronous work, `204` without a body, `400` for malformed requests, `401` without valid authentication, `403` for authenticated without permission, `404` for absent or hidden objects, `409` for conflicts, `422` for validation failures when the project uses it, `429` for rate limits, and `5xx` only for server faults.
- Use one error format across the API, such as RFC 9457 problem details or the project's existing envelope, with a stable `code`, a human `message` and, for validation, a list of field errors:

```json
{
  "code": "validation_failed",
  "message": "The request has invalid fields.",
  "errors": [
    { "field": "quantity", "code": "too_small", "message": "Must be at least 1." }
  ]
}
```

- Paginate every list with a bounded page size. Prefer cursor pagination for large or changing collections and return the next cursor in the response.
- Accept an `Idempotency-Key` header on creation and payment endpoints that clients may retry, store the key with the result, and return the stored result on a repeat.
- Version the API only when a breaking change is unavoidable and clients cannot move together. Evolve by adding optional fields instead.
- Validate every request body against a schema at the interface, and publish the contract as OpenAPI or the schema format the project uses when it has one.

## Background jobs

- Run slow, retryable or scheduled work in the job system of the project, never in the request path.
- Make every job idempotent, because queues deliver at least once. Pass identifiers in the payload and load fresh state in the job instead of serializing whole objects.
- Set a timeout, a retry policy with backoff and a limit, and a dead letter destination for jobs that keep failing. Log each failure with the job identifier.
- Enqueue jobs after the transaction that creates their data commits, or use an outbox table when the project has one.

## Performance basics

- Measure before optimizing, with the profiler of the runtime or the timings of the logs, and keep the measurement in the report.
- Bound everything that grows with input: page sizes, request bodies, batch sizes, recursion and concurrency.
- Set timeouts on every outbound call and connection pools sized for the workload.
- Cache only with a clear owner, key, expiry and invalidation rule, and never cache data that depends on the user under a key that omits the user.
- Keep blocking work off event loops and interface threads.

## Internationalization

- Keep every user-facing text in the localization resources of the project, keyed by meaning, with plurals and placeholders handled by the localization library rather than by concatenation.
- Store times in UTC and convert at the edge to the user's time zone. Format dates, numbers and currencies for the user's locale, and store money as integer minor units or a decimal type, never as floating point.
- Treat all text as Unicode end to end, from the database collation to the HTTP headers.

## Definition of done

- The change follows the architecture, naming and patterns of the project, and dependencies point in the declared direction.
- Configuration is validated at startup, and no secret is in code, logs or the repository.
- Expected errors have stable codes and map to the right transport response, and unexpected errors are logged with a correlation identifier.
- Every endpoint and job authenticates, authorizes per object and validates its input.
- Schema changes are versioned migrations that are safe on live data.
- Lists are paginated, outbound calls have timeouts, and retried operations are idempotent.
- User-facing text is localized, and dates, numbers and money are handled per locale and in the right types.
- Tests cover success, failure and boundary paths at the right layer, and the coverage report was read.
- The build, linters, formatter, type checker and full test suite pass with the project's own commands.
- Documentation and the API contract are updated where the change made them wrong.

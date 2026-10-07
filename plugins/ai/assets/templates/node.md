# Node.js engineering

## Recognize the project

- Read `package.json` first: `engines`, `type` (`"module"` means ESM), `scripts`, `dependencies` and `devDependencies`. The scripts are the commands of the project, so use `npm run build`, `npm test` and `npm run lint` as they are declared instead of calling the tools directly.
- Detect the package manager from the lock file: `package-lock.json` is npm, `pnpm-lock.yaml` is pnpm, `yarn.lock` is Yarn and `bun.lock` or `bun.lockb` is Bun. Check `packageManager` in `package.json` as well. Never mix managers or create a second lock file.
- Detect the Node.js version from `.nvmrc`, `.node-version`, `.tool-versions`, `engines.node`, the `FROM` line of the `Dockerfile` and the setup step of the continuous integration. Target the LTS line the project pins and do not use APIs newer than it.
- Read `tsconfig.json` (and any `tsconfig.build.json`) for `strict`, `module`, `moduleResolution`, `target`, `paths` and `outDir`. Read `eslint.config.*`, `.prettierrc*` or `biome.json`, and the test configuration (`vitest.config.*`, `jest.config.*`).
- Identify the framework from the dependencies: `@nestjs/core`, `fastify`, `express`, `hono` or `koa`. Identify the data layer: `prisma`, `drizzle-orm`, `typeorm`, `kysely`, `knex` or `mongoose`.
- In a monorepo (`pnpm-workspace.yaml`, `workspaces` in `package.json`, `nx.json`, `turbo.json`), run commands for the package you change through the workspace tool, such as `pnpm --filter <name> test`.

## Architecture

- Follow the architecture the project has. When you design new code, use a layered architecture organized by feature: a transport layer (routes, controllers, resolvers), an application layer (use cases or services), a domain layer (entities, value objects, rules) and an infrastructure layer (repositories, clients, queues).
- Dependencies point inward. The domain imports nothing from the framework, the ORM or the HTTP layer. Services receive repositories and clients through their constructor or a factory argument, never by importing a configured singleton.
- Use a hexagonal style (ports as TypeScript interfaces owned by the application, adapters in infrastructure) only when there are real alternative adapters or the domain is rich enough to warrant it. A CRUD service does not need ports for a single database.
- In NestJS, one module per feature that declares its controllers and providers and exports only what other modules need. Guards decide access, pipes validate and transform, interceptors handle cross-cutting concerns, exception filters map errors to responses.
- In Fastify, one plugin per feature registered with a prefix, decorators for shared services, and encapsulation respected (use `fastify-plugin` only for what must be visible to siblings).
- In Express, one router per feature, validation middleware before the handler and a single error-handling middleware registered last.

## Project structure

```
src/
  main.ts                # Process entry: load config, build app, listen, wire shutdown
  app.ts                 # Builds the app without listening, so tests can use it
  config/                # Environment schema parsed once at startup
  modules/
    orders/
      orders.routes.ts   # Transport: routes or controller, request and response schemas
      orders.service.ts  # Use cases, transactions, authorization calls
      orders.repository.ts # Queries for this feature only
      orders.schemas.ts  # Zod or TypeBox schemas and their inferred types
      orders.errors.ts   # Domain errors of this feature
  shared/                # Logger, error base types, HTTP client, auth utilities
  db/
    schema.ts            # Drizzle schema, or prisma/schema.prisma at the root
    migrations/          # Generated migrations, committed and never edited after release
test/
  integration/           # Supertest or inject against the real app and a real database
  fixtures/              # Builders for test data
```

- New behavior goes into the feature folder it belongs to. Something shared by two features moves to `shared/` only when the second user appears.
- Keep unit tests beside the code (`orders.service.test.ts`) or in `test/`, following what the project does.
- Never import from another feature's internals. Import from what the feature exports.

## Patterns and practices

- TypeScript `strict` is on, with `noUncheckedIndexedAccess` and `exactOptionalPropertyTypes` when the project already uses them. Never use `any`, non-null assertions (`!`) or `as` casts to silence the compiler. Narrow with type guards or parse with a schema.
- With ESM and `module: "nodenext"`, relative imports carry the `.js` extension in TypeScript source. Use `import type` for types (`verbatimModuleSyntax` enforces it). Never mix `require` into ESM code.
- Validate every request at the edge with the schema library the project uses (Zod, TypeBox, Valibot, `class-validator`), and derive the TypeScript type from the schema instead of writing both. In Fastify, declare `schema` for `body`, `params`, `querystring` and `response` so the response schema also strips fields. In NestJS, use a global `ValidationPipe` with `whitelist: true`, `forbidNonWhitelisted: true` and `transform: true`.
- Parse configuration once at startup from `process.env` with a schema, fail fast with a clear message listing the invalid keys, and pass the typed config object down. Never read `process.env` deep inside the code.
- Every promise is awaited, returned or explicitly handled. Enable the `@typescript-eslint/no-floating-promises` and `no-misused-promises` rules. Never pass an async function where a callback's rejection is ignored, such as `array.forEach(async ...)` or an `EventEmitter` listener without a `catch`.
- Use `Promise.all` for independent work and bounded concurrency (a small pool such as `p-limit` if the project has it) for large fan-outs. Never start an unbounded number of requests or queries from a loop.
- Throw typed errors (`class OrderNotFoundError extends Error` with a stable `code`) from the domain and map them to HTTP status codes in one place: the Express error middleware, the Fastify `setErrorHandler` or a NestJS exception filter. Answer with a consistent body, such as RFC 9457 problem details, without stack traces in production.
- Express 5 forwards rejected promises from handlers to the error middleware. In Express 4 an async handler must catch and call `next(err)`. Check the installed major before relying on either.
- Log with `pino` (or the logger the project has), as structured JSON with a request id bound to a child logger per request. Log objects with fields, never interpolated strings. Configure `redact` for `authorization`, `cookie`, passwords and tokens. Never use `console.log` in server code.
- Shut down gracefully: on `SIGTERM` and `SIGINT`, stop accepting connections (`server.close()` or `app.close()`), let in-flight requests finish within a timeout, then close database pools, queues and the logger, and exit. NestJS needs `app.enableShutdownHooks()`.
- Handle `unhandledRejection` and `uncaughtException` by logging and exiting with a non-zero code so the supervisor restarts a process whose state is unknown. Never keep running after them.
- Give every outbound call a timeout with `AbortSignal.timeout(ms)` on `fetch` or the option of the client, and propagate the request's `AbortSignal` where the framework provides one.
- Never block the event loop: no `fs.*Sync`, `crypto.*Sync` with large inputs, `JSON.parse` of unbounded bodies or heavy CPU loops in the request path. Move CPU work to `worker_threads` or a job queue.
- Use the built-in `fetch`, `node:crypto`, `node:test` utilities and `structuredClone` before adding a package. Import built-ins with the `node:` prefix.
- Documentation comments use TSDoc/JSDoc (`/** ... */`) only on exported APIs where the project writes them. Comments stay rare.

## Data, networking and persistence

- Change the schema only through migrations of the ORM the project uses: `prisma migrate dev --name <name>` to create and `prisma migrate deploy` in production, `drizzle-kit generate` then `drizzle-kit migrate`, or the TypeORM migration CLI. Commit the generated SQL. Never use `prisma db push` or TypeORM `synchronize: true` outside throwaway prototypes.
- Write migrations that are safe for a running application: add nullable columns or columns with defaults, backfill in batches, then add constraints. Never rename or drop a column that deployed code still reads.
- Prevent N+1 queries: load relations with `include` or `select` in Prisma, `with` in the Drizzle relational API or explicit joins, never by querying inside a loop over results.
- Select only the columns the response needs. Map database rows to response DTOs explicitly, so a new column never leaks.
- Wrap multi-step writes in a transaction (`prisma.$transaction`, `db.transaction`, `queryRunner`) and keep external calls out of it.
- Raw SQL goes only through parameterized APIs: Prisma `$queryRaw` with the tagged template, Drizzle `sql` template, Knex bindings. Never `$queryRawUnsafe`, `sql.raw` or string concatenation with input.
- Paginate every list with a maximum page size, preferably by cursor on an indexed column.
- Size the connection pool for the number of instances and the database limit. Create one client per process, never per request.
- Make queue consumers (BullMQ and similar) idempotent, with retries, backoff and a dead-letter path, and pass identifiers rather than whole objects in job payloads.

## Security

- JSON Web Tokens: verify with an explicit algorithm list (`jwt.verify(token, key, { algorithms: ['RS256'] })` in `jsonwebtoken`, `jwtVerify(token, key, { algorithms, issuer, audience })` in `jose`). Never call `jwt.decode` to make a decision. Check `exp`, `iss` and `aud`, keep access tokens short-lived, rotate refresh tokens and store them hashed so they can be revoked.
- In browsers, keep tokens or session ids in `HttpOnly`, `Secure`, `SameSite` cookies, never in `localStorage`. With `express-session` or `@fastify/session`, use a persistent store, a secret from the environment and `regenerate()` on login.
- Prototype pollution: never deep-merge, `Object.assign` or set paths from untrusted objects onto plain objects. Validate input with a schema that strips unknown keys, use `Map` or `Object.create(null)` for dictionaries keyed by input, and refuse `__proto__`, `constructor` and `prototype` as keys.
- ReDoS: never build a `RegExp` from input, avoid nested quantifiers on input-facing patterns, and bound input length before matching. Use the `re2` package when user-supplied patterns are a feature.
- Path traversal: resolve with `path.resolve(root, input)`, check that the result starts with `root + path.sep`, then check `fs.realpath` against the root again to defeat symbolic links. Serve user files through a lookup by id, never by a name from the request.
- SSRF: when fetching a URL from input, parse it with `new URL`, allow only `https:` and expected hosts, resolve the host and refuse private, loopback, link-local and metadata addresses, and disable or re-check redirects.
- Run commands with `execFile` or `spawn` and an argument array, never `exec` with a string built from input. Never use `eval`, `new Function` or `vm` as a sandbox.
- Set security headers with `helmet` or `@fastify/helmet`, configure CORS to exact origins, limit body size (`express.json({ limit })`, Fastify `bodyLimit`) and rate limit authentication routes (`@fastify/rate-limit`, `express-rate-limit`, `@nestjs/throttler`).
- Hash passwords with `argon2` or `bcrypt` and compare secrets with `crypto.timingSafeEqual`. Generate tokens with `crypto.randomBytes` or `crypto.randomUUID`, never `Math.random`.
- Supply chain: commit the lock file, install in CI with `npm ci` (`pnpm install --frozen-lockfile`, `yarn install --immutable`), run `npm audit --omit=dev` or the equivalent, and check new packages for maintenance, download count and install scripts. Consider `--ignore-scripts` where the project's packages allow it. Never add a package for a few lines of code.

## Performance

- Measure before optimizing: use `--cpu-prof`, `clinic` or the inspector, and look at event loop delay (`perf_hooks.monitorEventLoopDelay`).
- Stream large responses and files with `stream.pipeline` instead of buffering them. Respect backpressure.
- Cache with explicit keys, expiry and invalidation, in Redis when there are several instances. Never cache per-user data under a shared key.
- Prefer Fastify response schemas, which serialize faster and strip unknown fields.
- Use the cluster of the platform (multiple containers or processes behind a load balancer) rather than the `cluster` module unless the project already uses it.

## Tests

- Use the runner the project has: Vitest, Jest or `node:test`. Do not introduce a second one.
- Unit test services and domain logic with fakes for repositories and clients. Integration test routes against the real app built by `buildApp()` without listening: `supertest(app)` for Express and NestJS (`app.getHttpServer()`), `app.inject()` for Fastify.
- Run integration tests against a real database with Testcontainers (`@testcontainers/postgresql` and similar) or the database service the CI provides, with migrations applied. Never mock the ORM to test a query.
- Isolate tests: a transaction rolled back per test, a truncate between tests or a schema per worker. Control time with fake timers (`vi.useFakeTimers()`, `jest.useFakeTimers()`) and intercept outbound HTTP with `msw` or `nock` if the project uses them.
- Test the authorization of every endpoint with two users: the owner succeeds and the other user gets `403` or `404`. Test validation failures and the error body shape.
- Coverage: `vitest run --coverage` (with `@vitest/coverage-v8`), `jest --coverage`, or `c8 node --test`. Read the text summary and the `coverage/` report, and respect the thresholds in the configuration.
- Run tests non-interactively: `vitest run`, never plain `vitest`, which watches.

## Tooling and quality gates

- Type check with `tsc --noEmit` (or `tsc -b` for project references) as its own step, because Vitest, `tsx`, `esbuild` and Node's type stripping do not check types.
- Lint with ESLint flat config (`eslint.config.js`) and `typescript-eslint` type-aware rules, or Biome if the project uses it. Format with Prettier or Biome and check with `prettier --check .` in CI.
- Treat warnings as failures: `eslint --max-warnings=0`. Never add `eslint-disable` or `@ts-ignore` to pass. `@ts-expect-error` with a reason is acceptable only in tests of type errors.
- Run `npm run lint`, `npm run typecheck` (or its equivalent), `npm test` and `npm run build` before reporting.

## Build, configuration and release

- Compile with `tsc`, `tsup`, `esbuild` or the Nest CLI as the project does, into `dist/`, and run the compiled output in production, not `ts-node` or `tsx`.
- Build containers in multiple stages: install with the lock file and build in one stage, then copy `dist/`, `package.json`, the lock file and production dependencies (`npm ci --omit=dev`) into a slim image of the pinned Node.js version. Run as the `node` user, set `NODE_ENV=production` and start with `node dist/main.js` directly so signals reach the process.
- Keep `.env` files out of the repository and the image (`.dockerignore`), and provide `.env.example` listing every variable without values. Node can load a local file with `node --env-file=.env`, so a `dotenv` dependency is often unnecessary.
- Expose a liveness and a readiness endpoint, with readiness checking the database.
- Version with the scheme the project uses, and let CI run install, type check, lint, tests with coverage, build and audit on every pull request.

## Pitfalls

- Forgetting `await` on a query or a `reply.send`, so errors escape the handler and responses race.
- Returning ORM entities directly, which leaks password hashes and internal fields.
- Validating the body but not `params` and `query`, which arrive as strings and need coercion.
- Mixing CommonJS and ESM, or omitting `.js` in relative ESM imports so the build works but runtime fails.
- Using `||` for defaults where `0` or `""` is valid. Use `??`.
- Comparing secrets with `===`, or generating ids with `Math.random`.
- Creating a database client or an HTTP agent per request.
- Swallowing errors in `catch {}` or logging and continuing with a half-written state.
- Running migrations automatically from every instance at startup instead of once per deploy.
- Using `prisma db push` or `synchronize: true` and losing the migration history.
- Reading `process.env.X` at import time in modules that tests import, which freezes config before tests set it.

## Definition of done

- The command `tsc --noEmit` passes under `strict` with no new `any`, `!` or `as` cast.
- ESLint passes with zero warnings and the formatter check is clean.
- Every new route validates body, params, query and response with the project's schema library.
- Every new route authenticates, and authorizes access to each object it reads or changes, with a test using a second user.
- Every promise is awaited or handled, and errors are mapped to responses in the central handler.
- Schema changes come with a committed migration that applies cleanly on a fresh and on an existing database.
- Queries load relations without N+1 and select only needed fields, and lists are paginated with a maximum.
- New configuration is added to the startup schema and to `.env.example`.
- Logs are structured, carry the request id and contain no secrets or personal data.
- Unit and integration tests pass non-interactively, and coverage meets the project's threshold.
- The lock file changed only through the package manager, and the audit shows no new high or critical issue.
- The production build and, when present, the container build succeed.

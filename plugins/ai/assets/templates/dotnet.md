# .NET engineering

## Recognize the project

- Find the solution (`*.sln` or `*.slnx`) and the projects (`*.csproj`). Read `global.json` for the pinned SDK, `<TargetFramework>` for the runtime, and `Directory.Build.props`, `Directory.Packages.props` (central package management) and `.editorconfig` for the shared settings and analyzers.
- Target the .NET release the project pins, LTS or STS, and do not use APIs or C# features newer than its `<TargetFramework>` and `<LangVersion>`.
- Identify the web style: `app.MapGet` and `MapGroup` mean minimal APIs, classes deriving from `ControllerBase` mean controllers, `Pages/` means Razor Pages, `.razor` components mean Blazor. Identify the data layer (`Microsoft.EntityFrameworkCore`, Dapper) and the test framework (`xunit`, `xunit.v3`, `NUnit`, `MSTest`).
- Read `Program.cs` end to end: it shows the services, the middleware order, authentication, authorization and the endpoint mapping. Read `appsettings.json` and the environment-specific files.
- Use the commands the project uses: `dotnet build`, `dotnet test`, `dotnet format`, and any scripts or CI workflows that pass extra properties such as `-c Release` or `-warnaserror`.

## Architecture

- Follow the architecture the project has, Clean Architecture or vertical slices, and do not mix them.
- Clean Architecture: `Domain` (entities, value objects, domain rules, no dependencies), `Application` (use cases, interfaces it needs, DTOs, validation), `Infrastructure` (EF Core, external clients, implementations of application interfaces) and the web project (endpoints, composition root). References point inward only, so `Domain` references nothing and the web project references everything for wiring.
- Vertical slices: one folder per feature holding the endpoint, the request and response types, the validator and the handler, sharing only the `DbContext` and cross-cutting services. Prefer this for new services unless the project already uses Clean Architecture.
- Do not add a repository layer over EF Core when the project uses the `DbContext` directly, since `DbSet` already is a repository and a unit of work. Do not add MediatR or another mediator unless the project already uses it.
- Endpoints are thin: bind, validate, call the handler or service, return a typed result.

## Project structure

```
src/
  Shop.Api/
    Program.cs               # Composition root: services, middleware, endpoint mapping
    Features/
      Orders/
        CreateOrder.cs       # Endpoint, request, response, validator and handler of one slice
        GetOrder.cs
        OrderAuthorizationHandler.cs # Resource-based authorization for orders
    Infrastructure/
      ShopDbContext.cs       # EF Core context and model configuration
      Configurations/        # IEntityTypeConfiguration<T> classes
      Migrations/            # Generated EF Core migrations, committed
    appsettings.json         # Non-secret defaults, overridden per environment
tests/
  Shop.Api.Tests/            # Unit tests of handlers and domain rules
  Shop.Api.IntegrationTests/ # WebApplicationFactory with Testcontainers
Directory.Build.props        # Shared properties: nullable, warnings as errors, analyzers
Directory.Packages.props     # Central package versions
global.json                  # Pinned SDK
```

- New code goes into its feature folder or the layer the project uses. One public type per file, the file named after the type, namespaces matching folders (file-scoped namespaces).

## Patterns and practices

- Keep `<Nullable>enable</Nullable>` and treat nullable warnings as errors. Never silence them with `!` (the null-forgiving operator) or `#nullable disable`. Model optional values explicitly.
- Use records for DTOs and value objects, `required` and `init` properties for construction, and `sealed` classes by default. Model closed sets as enums with validation at the boundary (`Enum.IsDefined` or the validator).
- Async all the way: every I/O method is `async Task` and awaited. Never `.Result`, `.Wait()` or `GetAwaiter().GetResult()` on a task, never `async void` except event handlers, and pass `CancellationToken` from the endpoint (`HttpContext.RequestAborted` or the bound `CancellationToken` parameter) to every EF Core and HTTP call. Use `ConfigureAwait(false)` in reusable libraries, not in ASP.NET Core application code.
- Dependency injection lifetimes: `DbContext` and per-request services are scoped, stateless services may be singletons, and a singleton must never capture a scoped service (a captive dependency). Enable `ValidateScopes` and `ValidateOnBuild` (on by default in Development) and create scopes with `IServiceScopeFactory` in `BackgroundService` and hosted services.
- Use `IHttpClientFactory` or typed clients for outbound HTTP, with timeouts and resilience (`Microsoft.Extensions.Http.Resilience`) when the project uses it. Never create and dispose `HttpClient` per call.
- Options pattern: bind sections with `AddOptions<T>().BindConfiguration("Section").ValidateDataAnnotations().ValidateOnStart()` so invalid configuration fails at startup. Inject `IOptions<T>`, `IOptionsSnapshot<T>` (scoped) or `IOptionsMonitor<T>` (changes at runtime) as needed.
- Validation: the built-in minimal API validation (`builder.Services.AddValidation()` on .NET 10 and newer), data annotations on controller models with `[ApiController]`, or FluentValidation if the project uses it. Every request type is validated before it reaches the handler.
- Errors: register `AddProblemDetails()` and an `IExceptionHandler` that maps domain exceptions to `ProblemDetails` with a stable error code. Return `TypedResults.NotFound()`, `ValidationProblem` and similar instead of throwing for expected outcomes. Never expose exception details outside Development.
- Logging: inject `ILogger<T>` and log with message templates (`logger.LogInformation("Order {OrderId} paid", id)`), never string interpolation, or with `[LoggerMessage]` source-generated methods on hot paths. Never log secrets or personal data.
- Localize user-facing text with `IStringLocalizer` and resource files when the project is localized.
- Documentation comments are XML comments (`///`) on public APIs, enabled with `<GenerateDocumentationFile>` where the project publishes them. Comments stay rare.

## Data, networking and persistence

- Change the schema only through EF Core migrations: `dotnet ef migrations add <Name>`, review the generated code and the SQL from `dotnet ef migrations script --idempotent`, and commit both the migration and the model snapshot. Never edit a migration already applied in production.
- Apply migrations once per deploy with an idempotent script or a migration bundle (`dotnet ef migrations bundle`), not with `Database.Migrate()` from every instance at startup. Never use `EnsureCreated` with migrations.
- Use `AsNoTracking()` for read-only queries, and project to DTOs with `Select` so only needed columns load. Load relations with `Include`, and use `AsSplitQuery()` when several collections would multiply rows.
- Prevent N+1 queries: never query inside a loop over results or rely on lazy-loading proxies. Check the SQL with logging (`LogTo` or the `Microsoft.EntityFrameworkCore.Database.Command` category).
- Raw SQL only through parameterized APIs: `FromSql($"... WHERE Id = {id}")` (EF Core 7 and newer) or `FromSqlInterpolated`, `ExecuteSql` and `SqlQuery`, which turn interpolations into parameters. Never `FromSqlRaw` or `ExecuteSqlRaw` with concatenated input. With Dapper, pass parameters as an object.
- Use `ExecuteUpdateAsync` and `ExecuteDeleteAsync` for bulk changes, and a transaction or a single `SaveChangesAsync` for multi-entity writes. Use concurrency tokens (`[Timestamp]` or `IsConcurrencyToken`) for entities edited concurrently, and handle `DbUpdateConcurrencyException`.
- Paginate every list with a maximum page size, preferably keyset on an indexed column.
- Configure entities with `IEntityTypeConfiguration<T>` classes and give strings a maximum length.

## Interface

- Razor encodes `@value` output. `Html.Raw` and `MarkupString` render raw HTML, so never pass them content a user influenced.
- Razor Pages and MVC forms get antiforgery tokens automatically. Minimal API endpoints that accept forms need `app.UseAntiforgery()` and validation, and Blazor forms render the token through `EditForm` with antiforgery middleware.
- In Blazor, authorization in components (`[Authorize]`, `AuthorizeView`) only hides interface. Every API a WebAssembly or interactive component calls authorizes on the server. Choose the render mode (static server, interactive server, WebAssembly, auto) the project uses and keep secrets out of WebAssembly code, which ships to the browser.
- Use the component library and theme the project has, and localize text with `IStringLocalizer`.

## Security

- Set a fallback policy that requires authenticated users (`AddAuthorizationBuilder().SetFallbackPolicy(new AuthorizationPolicyBuilder().RequireAuthenticatedUser().Build())`) and mark public endpoints with `AllowAnonymous` explicitly.
- Define named policies for roles and claims, and apply them with `RequireAuthorization("Policy")` or `[Authorize(Policy = "...")]`.
- Authorize every object with resource-based authorization: an `AuthorizationHandler<TRequirement, TResource>` that checks ownership, invoked through `IAuthorizationService.AuthorizeAsync(user, order, requirement)` after loading the resource. Scope queries by owner when listing.
- JWT bearer: configure `TokenValidationParameters` with the issuer, audience, signing keys and `ValidAlgorithms`, keep lifetime validation on, and use the authority's metadata when available. Never accept unsigned tokens or validate tokens by hand.
- Cookies: `HttpOnly`, `SecurePolicy = Always`, `SameSite`, and sign-out that clears the session. Use ASP.NET Core Identity for local accounts, which hashes passwords with PBKDF2 through `PasswordHasher<T>`.
- Data Protection keys must persist to shared storage (`PersistKeysToDbContext`, Azure Blob, a file share) and be protected at rest when several instances or restarts must read cookies and tokens, with a fixed `SetApplicationName`.
- Keep secrets out of `appsettings.json`: use User Secrets (`dotnet user-secrets`) in development and environment variables or a vault in production.
- Use `AddRateLimiter` and `RequireRateLimiting` on authentication and expensive endpoints, `UseHsts` and `UseHttpsRedirection`, and CORS policies with exact origins.
- Never deserialize untrusted data with `BinaryFormatter` (removed in modern .NET) or type-name handling in `Newtonsoft.Json`. Use `System.Text.Json` with DTOs.
- Resolve file paths with `Path.GetFullPath(Path.Combine(root, name))` and require the result to start with the root. Run processes with `ProcessStartInfo.ArgumentList`.
- Treat NuGet audit warnings (vulnerable packages) as failures and check with `dotnet list package --vulnerable --include-transitive`.

## Performance

- Measure with `dotnet-counters`, `dotnet-trace`, OpenTelemetry metrics and the EF Core SQL logs before tuning.
- Use `AsNoTracking`, projections, compiled queries for hot paths and `DbContext` pooling (`AddDbContextPool`) when the context holds no per-request state.
- Cache with `IMemoryCache`, `HybridCache` (.NET 9 and newer) or output caching, with explicit keys, expiry and invalidation, never per-user data under a shared key.
- Avoid allocations on hot paths with `Span<T>`, pooled buffers and source-generated `System.Text.Json` contexts where measurement shows the need.

## Tests

- Use the framework the project has (xUnit, NUnit or MSTest) and its assertion style. FluentAssertions 8 and newer requires a paid license for commercial use, so do not add or upgrade it without the reader's decision. Use the built-in asserts, Shouldly or AwesomeAssertions when the project already does.
- Unit test handlers, domain rules and validators with fakes or NSubstitute or Moq as the project uses.
- Integration test endpoints with `WebApplicationFactory<Program>`, replacing external services in `ConfigureTestServices` and running against a real database with Testcontainers (`Testcontainers.PostgreSql`, `Testcontainers.MsSql`), migrations applied and data reset between tests (Respawn or a fresh database per class). Never use the EF Core in-memory provider to test queries.
- Test authorization with two users for every object endpoint, plus validation failures and the `ProblemDetails` shape.
- Coverage with coverlet: `dotnet test --collect:"XPlat Code Coverage"` writes Cobertura files under `TestResults`, and ReportGenerator turns them into a readable report. Respect thresholds configured in the build or CI.
- Run `dotnet test --filter "FullyQualifiedName~Orders"` while iterating and the full `dotnet test` before reporting.

## Tooling and quality gates

- Keep `<TreatWarningsAsErrors>true</TreatWarningsAsErrors>`, `<Nullable>enable</Nullable>`, `<AnalysisLevel>` and `<EnforceCodeStyleInBuild>true</EnforceCodeStyleInBuild>` in `Directory.Build.props` as the project sets them. Never add `#pragma warning disable` or `<NoWarn>` to pass.
- Format with `dotnet format`, and check with `dotnet format --verify-no-changes` as CI does. The `.editorconfig` decides style and analyzer severities.
- Use the analyzers the project includes (the .NET SDK analyzers, StyleCop, Roslynator, SonarAnalyzer) and fix their findings.
- Manage versions in `Directory.Packages.props` when central package management is on, and add packages with `dotnet add package`.

## Build, configuration and release

- Configuration layers: `appsettings.json`, `appsettings.{Environment}.json`, User Secrets in development, environment variables (`Section__Key`) and the vault in production. Set `ASPNETCORE_ENVIRONMENT` explicitly in each environment.
- Publish with `dotnet publish -c Release`, and build images with a multi-stage `Dockerfile` from the official SDK and ASP.NET runtime images of the pinned version, or with the SDK container support (`dotnet publish /t:PublishContainer`), running as the non-root `app` user.
- Use `RestorePackagesWithLockFile` and `dotnet restore --locked-mode` in CI when the project has `packages.lock.json`.
- Map health checks (`MapHealthChecks`) for liveness and readiness, including the database for readiness.
- Version through `<Version>` in the project or `Directory.Build.props`, or the tool the project uses.
- CI runs restore, `dotnet build -c Release`, `dotnet format --verify-no-changes`, `dotnet test` with coverage and the vulnerability check on every pull request.

## Pitfalls

- Blocking on async code with `.Result` or `.Wait()`, causing thread pool starvation.
- Injecting a scoped `DbContext` into a singleton or a hosted service.
- Sharing one `DbContext` across threads or parallel tasks.
- Returning EF entities from endpoints, which leaks fields and causes serialization cycles.
- Tracking queries for read-only lists, and loading whole entities to show three fields.
- The `FromSqlRaw` with string interpolation, which looks parameterized but is not.
- Relying on component-level `[Authorize]` in Blazor WebAssembly without server checks.
- Data Protection keys kept in memory in a multi-instance deployment, which logs users out on every restart.
- Middleware in the wrong order: `UseAuthentication` must come before `UseAuthorization`, both after routing and before the endpoints.
- Applying migrations from every instance at startup.

## Definition of done

- The command `dotnet build -c Release` passes with warnings as errors and no new suppression.
- The command `dotnet format --verify-no-changes` is clean.
- Nullable reference types are respected without `!` or `#nullable disable`.
- Every new endpoint validates its input and returns typed results or `ProblemDetails`.
- Every endpoint requires authentication unless explicitly anonymous, and every object access uses resource-based authorization, with a test for a second user.
- Every async call is awaited and receives the request's `CancellationToken`.
- Service lifetimes are correct and no singleton captures a scoped service.
- Schema changes come as reviewed EF Core migrations, and read queries use `AsNoTracking` and projections without N+1.
- New configuration is bound to validated options with `ValidateOnStart`, and secrets stay out of `appsettings.json`.
- Unit and `WebApplicationFactory` integration tests pass with `dotnet test`, and coverlet coverage meets the project's threshold.
- No vulnerable package is introduced.

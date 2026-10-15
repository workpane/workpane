# PHP engineering

## Recognize the project

- Read `composer.json` first: `require.php` for the minimum version, the framework packages, `autoload` for the PSR-4 namespaces and `scripts` for the commands of the project. `composer.lock` is the truth of what is installed.
- Detect the framework: `artisan` and `laravel/framework` mean Laravel, `bin/console` and `symfony/framework-bundle` mean Symfony. Read `config/` and `routes/` (Laravel) or `config/packages/` and `config/services.yaml` (Symfony).
- Detect the PHP version from `require.php`, `.php-version`, the `Dockerfile`, `config.platform.php` in `composer.json` and the continuous integration. Do not use language features newer than the minimum, such as property hooks and asymmetric visibility of 8.4 in an 8.3 project.
- Find the quality tools in `require-dev` and their configuration: `phpstan.neon(.dist)`, `psalm.xml`, `.php-cs-fixer.dist.php`, `pint.json`, `rector.php`, `phpunit.xml(.dist)`, and `tests/Pest.php` for Pest.
- Run tools through Composer scripts or `vendor/bin/` (`vendor/bin/phpstan`, `php artisan test`, `bin/phpunit`), never a globally installed copy with another version.

## Architecture

- Follow the project's architecture. For new code, keep controllers thin: they receive a validated request, call one service or action, and return a response or resource. Business rules live in services, actions or domain classes, and queries live in models' scopes, repositories or query objects.
- Dependencies are injected through constructors and resolved by the container (Laravel's service container, Symfony autowiring). Never call `new` on a service inside another service, and never reach for facades or `app()` inside domain classes when constructor injection is possible.
- Use value objects and DTOs (readonly classes, enums) to carry data between layers instead of arrays with string keys.
- In Laravel, the request flow is route, middleware, `FormRequest`, controller, action or service, model, `JsonResource` or view. Events, listeners and queued jobs handle side effects.
- In Symfony, the request flow is route attribute, controller, a DTO mapped with `#[MapRequestPayload]` or a Form, a service, Doctrine entities and repositories, and a serializer or Twig template. Voters decide access.
- Organize by domain (`app/Domain/Billing` or `src/Billing`) when the project does, with a hexagonal split only when the domain justifies it.

## Project structure

```
app/                     # Laravel layout, Symfony keeps the same ideas under src/
  Http/
    Controllers/         # Thin controllers, one per resource
    Requests/            # FormRequest classes: authorize() and rules()
    Resources/           # API resources that shape JSON output
  Models/                # Eloquent models with $fillable, casts and scopes
  Policies/              # One policy per model
  Actions/               # Single-purpose use cases, when the project uses them
  Jobs/                  # Queued jobs
  Enums/                 # Backed enums for closed sets
config/                  # Configuration reading env() only here
database/
  migrations/            # Timestamped migrations, committed
  factories/             # Model factories for tests and seeding
routes/                  # web.php, api.php
resources/views/         # Blade templates
tests/
  Feature/               # HTTP and integration tests
  Unit/                  # Pure class tests
```

- New code goes into the folder its kind and domain already use. Follow the PSR-4 mapping exactly: one class per file, file name equals class name, namespace equals path.
- Never put logic in `routes/` closures, `helpers.php` files or Blade templates.

## Patterns and practices

- Start every file with `declare(strict_types=1);` when the project does (and in every new project). Type every parameter, return and property, use `never`, `void`, union and nullable types precisely, and use `readonly` properties and classes for values.
- Model closed sets as backed enums and validate them with `Rule::enum()` or `#[Assert\Choice]`. Never pass magic strings.
- Use `match` instead of `switch`, constructor property promotion, named arguments where they clarify, and first-class callables. Never use `extract`, variable variables or `@` error suppression.
- Throw domain exceptions (`OrderAlreadyPaid extends DomainException`) and map them to responses in one place: the exception handler configured in `bootstrap/app.php` (`withExceptions`) or `app/Exceptions/Handler.php` in Laravel, an exception event listener in Symfony. Never return `false` or `null` to signal failure where an exception or a result type fits.
- Read configuration through `config('services.stripe.key')` in Laravel. `env()` belongs only in `config/*.php`, because after `php artisan config:cache` it answers `null` elsewhere. In Symfony, bind parameters and secrets to services in `services.yaml` or with `#[Autowire(env: ...)]`.
- Log through the PSR-3 logger (`Log` or an injected `LoggerInterface`) with a message and a context array, never by concatenating values. Never log request bodies with passwords or tokens.
- Translate user-facing text with `__()` and `lang/` files in Laravel or the Translator and `translations/` in Symfony.
- Coding style follows PER Coding Style (the successor of PSR-12) through the project's formatter. Documentation comments are PHPDoc, used for what types cannot express, such as `@param list<Order> $orders` and `@return array<string, int>`, never repeating native types. Comments stay rare.

### Laravel specifics

- Validate with a `FormRequest` whose `authorize()` calls the policy and whose `rules()` lists every field. Use only `$request->validated()` or `$request->safe()->only([...])` to write models.
- Every model has a policy registered or discovered, and every controller action calls `$this->authorize()`, `Gate::authorize()` or the `can` middleware. A missing policy method denies, so add the method instead of skipping the check.
- Declare `$fillable` with exactly the assignable fields. Never set `$guarded = []`. Enable `Model::shouldBeStrict()` outside production so lazy loading, silently discarded attributes and missing attributes throw.
- Return `JsonResource` or `ResourceCollection` from APIs so output fields are explicit, and `$hidden` covers secrets.
- Use queued jobs for slow work, with `$tries`, `$backoff`, `$timeout` and `ShouldBeUnique` when duplicates matter. Jobs serialize models by key through `SerializesModels`, so they reload fresh data. Dispatch after commit (`afterCommit()` or `after_commit` in the queue config).

### Symfony specifics

- Use autowiring and autoconfiguration, private services by default and constructor injection. Never fetch services from the container in controllers.
- Map input to DTOs with `#[MapRequestPayload]` or `#[MapQueryString]` and validate them with Validator constraints, or use Forms for HTML.
- Decide access with Voters (`supports()` and `voteOnAttribute()`) and call `$this->denyAccessUnlessGranted('EDIT', $order)` or `#[IsGranted]` on every action that touches an object.
- Keep `access_control` in `security.yaml` as a coarse layer only. Per-object decisions belong in voters.

## Data, networking and persistence

- Change the schema only through migrations: `php artisan make:migration` and `php artisan migrate`, or `bin/console make:migration` (or `doctrine:migrations:diff`) and `doctrine:migrations:migrate`. Review generated SQL, commit migrations and never edit one that already ran in production.
- Write migrations safe for a running application: add nullable columns, backfill in batches, then add constraints. Run `php artisan migrate --force` once per deploy, not from every container.
- Prevent N+1 queries with eager loading (`with()`, `load()`, `withCount()`) in Eloquent and fetch joins or `addSelect` in Doctrine DQL. Enable `Model::preventLazyLoading()` outside production so a missed relation fails loudly.
- Iterate large tables with `chunkById()` or `lazyById()` in Eloquent and `toIterable()` with periodic `clear()` in Doctrine. Never `->get()` or `findAll()` an unbounded table.
- Wrap multi-step writes in `DB::transaction()` or `$entityManager->wrapInTransaction()`, and use `lockForUpdate()` or pessimistic locks for read-modify-write races.
- Raw SQL uses bindings: `DB::select('... where id = ?', [$id])`, `whereRaw('price > ?', [$min])`, Doctrine `setParameter()`, or PDO prepared statements with `PDO::ATTR_EMULATE_PREPARES` set to `false` and `PDO::ERRMODE_EXCEPTION`. Never interpolate input into SQL, `orderBy` column names or `DB::raw`. Validate sort columns against an allowlist.
- Use the framework HTTP client (`Http::timeout(5)->retry(...)` or Symfony `HttpClientInterface` with `timeout`) with explicit timeouts, and fake it in tests.
- Paginate every list with `paginate()` or `cursorPaginate()` and a maximum page size.

## Interface

- Blade `{{ $value }}` escapes with `htmlspecialchars`, `{!! $value !!}` does not. Never use `{!! !!}` on content a user influenced. Twig autoescapes, and `|raw` is the same danger.
- Every form that posts includes `@csrf` in Blade or the CSRF token of Symfony Forms (`csrf_token()` for manual forms). Use `@method('PUT')` for verb spoofing.
- Keep Blade and Twig free of queries and business logic. Prepare data in the controller or a view model, and use components (`<x-...>` or Twig components) for repeated markup.
- Build URLs with `route()`, `url()` or `path()`, and assets with `asset()` or `Vite`, never hard-coded paths.

## Security

- Hash passwords with `password_hash($password, PASSWORD_DEFAULT)` (or the framework hasher), verify with `password_verify`, and upgrade with `password_needs_rehash`. Compare secrets with `hash_equals` and generate tokens with `random_bytes` or `bin2hex(random_bytes(32))`, never `rand`, `mt_rand` or `uniqid`.
- Regenerate the session on login (`$request->session()->regenerate()` in Laravel, automatic in Symfony's authenticator) and invalidate it on logout.
- Never call `unserialize` on input. Use `json_decode($json, true, 512, JSON_THROW_ON_ERROR)`. Never use `eval`, `create_function`, `preg_replace` with `/e`, `include` of a path from input, or `extract($_POST)`.
- Run processes with Symfony `Process` or Laravel `Process` and an argument array, never `exec`, `shell_exec` or `system` with interpolated input.
- File uploads: validate with `mimes`, `max` and `File::types()` rules or `#[Assert\File]`, check the type from the content (`getMimeType()`, not `getClientMimeType()` or the client's extension), store outside the public web root with generated names (`store()` uses `hashName()`), and serve them through a controller that authorizes access.
- Path traversal: build paths from validated identifiers, use the filesystem abstraction (`Storage`, Flysystem) with a root, and refuse `..` and absolute paths.
- SSRF: validate URLs from input against allowed schemes and hosts and refuse private and loopback addresses before the HTTP client fetches them.
- Production has `APP_DEBUG=false` or `APP_ENV=prod`, a strong `APP_KEY` or `APP_SECRET` from the environment or the Symfony secrets vault, and no exposed debug toolbar, Telescope or Horizon dashboard without authorization.
- Run `composer audit` in CI and keep `composer.lock` committed.

## Performance

- Cache configuration, routes, views and events in production: `php artisan optimize` (or `config:cache`, `route:cache`, `view:cache`, `event:cache`). In Symfony, warm the cache with `bin/console cache:warmup` in the prod environment.
- Enable OPcache with `opcache.validate_timestamps=0` in production images, and preloading when the project uses it.
- Install with `composer install --no-dev --optimize-autoloader` (and `--classmap-authoritative` when nothing is generated at runtime).
- Index columns used in `where` and `orderBy`, select only needed columns, and use `withCount` or aggregates instead of loading relations to count them.
- Profile with Laravel Debugbar, Telescope or the Symfony profiler in development, never in production.

## Tests

- Use the framework the project has: PHPUnit or Pest. Feature tests go through HTTP (`$this->getJson()`, `actingAs()`, `assertForbidden()`) in Laravel and `WebTestCase` with `$client->loginUser()` in Symfony. Unit tests cover services, value objects and policies directly.
- Build data with model factories in Laravel and Foundry or fixtures in Symfony. Reset the database per test with `RefreshDatabase` (or `LazilyRefreshDatabase`) or DAMA Doctrine test bundle transactions.
- Fake external effects with `Http::fake()`, `Queue::fake()`, `Mail::fake()`, `Event::fake()` and `Storage::fake()`, and the Symfony `MockHttpClient`. Assert what was dispatched.
- Test every policy or voter for owner, other user and administrator, and every endpoint for a second user getting `403` or `404`.
- Test against the same database engine as production when queries depend on it.
- Coverage needs a driver: PCOV or Xdebug with `XDEBUG_MODE=coverage`. Run `php artisan test --coverage --min=<threshold>`, `vendor/bin/pest --coverage`, or `vendor/bin/phpunit --coverage-text --coverage-html coverage`, and read the report.

## Tooling and quality gates

- Static analysis with PHPStan (Larastan for Laravel, `phpstan/phpstan-symfony` and `phpstan/phpstan-doctrine` for Symfony) or Psalm at the level the project sets, aiming at the maximum level for new code. Never add to the baseline or add `@phpstan-ignore` to pass. Fix the code or the PHPDoc type.
- Format with Laravel Pint (`vendor/bin/pint --test` to check) or PHP-CS-Fixer (`vendor/bin/php-cs-fixer fix --dry-run --diff`), whichever the project configures.
- Rector (`vendor/bin/rector process --dry-run`) applies the project's upgrade and quality rules. Run it only with the project's `rector.php` and review its diff.
- Run `composer validate --strict` when you change `composer.json`, and change dependencies only through `composer require` or `composer update vendor/package`, never by editing the lock file.

## Build, configuration and release

- Keep `.env` ignored and `.env.example` complete. In Symfony, `.env` holds defaults and `.env.local` holds machine values, with real secrets in the vault or the environment.
- Build containers in stages: install Composer dependencies in a builder, build front-end assets with Vite in another, and copy both into a PHP-FPM or FrankenPHP image of the pinned version with OPcache enabled, running as a non-root user.
- Deploy with `composer install --no-dev`, the framework caches, `migrate --force` once, and a queue restart (`php artisan queue:restart` or a Horizon terminate) so workers load the new code.
- CI runs `composer validate`, `composer audit`, the formatter check, static analysis and tests with coverage on every pull request.

## Pitfalls

- Calling `env()` outside `config/`, which breaks after `config:cache`.
- The `$guarded = []` or `$request->all()` passed to `create()` or `update()`, which allows mass assignment.
- Authorizing in the route or the menu only, while the controller action skips the policy.
- Lazy loading relations in a loop or in Blade, producing N+1 queries.
- Using `{!! !!}` or `|raw` for user content.
- Loose comparisons (`==`, `in_array` without `true`) on security values. Use `===` and strict mode.
- Dispatching jobs inside a transaction that later rolls back.
- Trusting `getClientOriginalName()` or `getClientMimeType()` for uploads.
- Catching `\Throwable` and returning a generic success.
- Adding errors to the PHPStan baseline instead of fixing them.

## Definition of done

- Every new file declares `strict_types` (where the project does), and every parameter, return and property is typed.
- Static analysis passes at the project's level with no new baseline entry or ignore.
- The formatter check passes and Rector reports nothing new when the project uses it.
- Every new action validates through a `FormRequest`, a DTO with constraints or a Form, and writes only validated fields.
- Every action that touches a model is authorized by a policy or voter, with a test proving another user is refused.
- Every schema change has a committed migration that runs on a fresh and an existing database.
- Relations are eager loaded, lists are paginated and raw SQL uses bindings.
- Templates escape all user content and every posting form carries a CSRF token.
- Slow side effects run in queued jobs dispatched after commit.
- Configuration lives in config files reading the environment, documented in `.env.example`.
- Tests pass with `php artisan test`, Pest or PHPUnit, and coverage of the changed code meets the project's threshold.
- The command `composer audit` reports no new vulnerable package.

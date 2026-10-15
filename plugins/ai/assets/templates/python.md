# Python engineering

## Recognize the project

- Read `pyproject.toml` first: `requires-python`, the dependencies, the build backend, and the `[tool.*]` sections for `ruff`, `mypy`, `pyright`, `pytest` and `coverage`. Then read `setup.cfg`, `tox.ini`, `noxfile.py` or `Makefile` if they exist.
- Detect the dependency manager from the lock file: `uv.lock` is uv, `poetry.lock` is Poetry, `pdm.lock` is PDM, `requirements*.txt` compiled from `requirements*.in` is pip-tools. Run everything through that manager (`uv run pytest`, `poetry run pytest`) so the locked environment is used. Never `pip install` into the environment by hand.
- Detect the Python version from `.python-version`, `requires-python`, the `FROM` line of the `Dockerfile` and the continuous integration, and do not use syntax or standard library features newer than it.
- Identify the framework: `manage.py` and `settings` modules mean Django (check for `rest_framework` and `ninja`), `FastAPI(` means FastAPI, `Flask(` or `create_app` means Flask. Identify the worker: `celery`, `rq`, `dramatiq` or `arq`.
- Find the commands in `pyproject.toml` scripts, the `Makefile`, `noxfile.py`, `tox.ini` and the workflow files, and use them as declared.

## Architecture

- Follow the architecture of the project. For new code, keep views or routers thin: they parse input, call a service function and shape the response. Business rules live in services or the domain module, and queries live in managers, querysets or repositories.
- Dependencies point inward. Domain logic does not import `request`, the framework's response classes or the HTTP client. Pass what it needs as arguments.
- In Django, one app per bounded domain, not per technical layer. Put query logic in custom `QuerySet` methods exposed through the manager (`Order.objects.for_user(user).open()`), write logic in service functions when it spans several models or side effects, and keep model methods for behavior of one instance.
- In FastAPI, one `APIRouter` per feature, dependencies (`Depends`) for the database session, the current user and authorization, Pydantic models for every request and response, and services that receive the session explicitly.
- In Flask, an application factory (`create_app(config)`) that registers one blueprint per feature and initializes extensions with `init_app`, so tests build isolated apps.
- Introduce repository interfaces with `typing.Protocol` only when there are real alternative implementations. The Django ORM is already the repository for most Django code.

## Project structure

```
pyproject.toml           # Dependencies, tool configuration, entry points
uv.lock                  # Lock file, committed
src/shop/
  settings/              # Django: base.py, dev.py, prod.py, test.py reading env
  orders/                # One app or feature package
    models.py            # Models, managers and querysets
    services.py          # Write use cases and transactions
    selectors.py         # Read queries reused by views, when the project uses them
    api.py               # DRF views and serializers, or FastAPI router and schemas
    tasks.py             # Celery tasks calling services
    migrations/          # Generated migrations, committed
    templates/orders/    # Templates namespaced by app
  core/                  # Shared base classes, permissions, errors, logging setup
tests/
  conftest.py            # Shared fixtures: client, user, database session
  orders/
    factories.py         # factory_boy factories
    test_services.py
    test_api.py
```

- New code goes into the app or feature package it belongs to. A module reaching about 400 lines splits into a package by responsibility (`services/` with one module per use case).
- Follow the existing layout, `src/` or flat. Templates and static files are namespaced by app so names never collide.

## Patterns and practices

- Type hint every function signature and public attribute. Use built-in generics (`list[str]`, `dict[str, int]`), `X | None`, `typing.Self`, `TypedDict`, `Literal`, `Protocol` and the `type` statement of Python 3.12 for aliases when the project's minimum version allows it.
- Model values with `dataclasses` (`frozen=True, slots=True`) or Pydantic models at boundaries, and closed sets with `enum.StrEnum` or Django `TextChoices`. Never pass raw dicts through several layers.
- Raise specific exceptions defined per domain (`class OrderNotFound(Exception)`), catch them at the boundary and map them to responses in one place (DRF `EXCEPTION_HANDLER`, FastAPI `exception_handler`, Flask `errorhandler`). Never `except Exception: pass` and never use a bare `except:`.
- Use context managers for resources: `with open(...)`, `with transaction.atomic():`, `async with httpx.AsyncClient() as client:`. Give every HTTP call an explicit timeout, since `requests` has none by default.
- Log with the standard `logging` module through `logger = logging.getLogger(__name__)`, with lazy arguments (`logger.info("Order %s paid", order.id)`) or `structlog` if the project uses it. Never use `print` in application code.
- Read configuration from the environment once, validated: `pydantic-settings` `BaseSettings` in FastAPI and Flask, environment reads in the settings module for Django (with `django-environ` if present). Fail at startup on a missing required value.
- Translate user-facing strings with `gettext` (`django.utils.translation.gettext_lazy` in Django models and forms) when the project is localized.
- Docstrings follow PEP 257 in the style the project uses (Google, NumPy or reStructuredText), only on public APIs that need them. Comments stay rare.

### Async correctness

- In FastAPI, an `async def` endpoint must only await non-blocking calls. A blocking driver, `requests`, `time.sleep` or heavy CPU work inside it blocks every request. Declare such endpoints with `def` so they run in the thread pool, or use async libraries (`httpx.AsyncClient`, `asyncpg`, SQLAlchemy `AsyncSession`).
- Use the `lifespan` context manager to create and close shared clients and pools, not the deprecated startup and shutdown events.
- In Django async views, use the async ORM methods (`aget`, `acreate`, `async for`) or wrap sync code in `sync_to_async`. Never call the sync ORM from an async context.
- Run concurrent async work with `asyncio.TaskGroup` and bound it with a `Semaphore`. Never fire and forget a task without keeping a reference and handling its exception.

## Data, networking and persistence

- Change the schema only through migrations: `python manage.py makemigrations` then `migrate` for Django, Alembic `alembic revision --autogenerate` reviewed by hand then `alembic upgrade head` for SQLAlchemy. Commit migrations, never edit one that has run in production, and check in CI that none is missing (`makemigrations --check --dry-run`).
- Write migrations safe for a running application: add nullable fields first, backfill with a data migration in batches, then add constraints. Separate schema and data migrations.
- Prevent N+1 queries in Django: `select_related` for foreign keys and one-to-one, `prefetch_related` (with `Prefetch` objects to filter) for many-to-many and reverse relations, `only`, `values` or annotations when you need a few fields. In SQLAlchemy, use `selectinload` or `joinedload`. Verify with `assertNumQueries` or `django_assert_num_queries`.
- Wrap multi-step writes in `transaction.atomic()` or a session transaction, use `select_for_update()` for read-modify-write races and `F()` expressions for counters. Send emails and enqueue tasks with `transaction.on_commit` so they never fire for a rolled-back write.
- Paginate every list with a maximum page size. Use `iterator(chunk_size=...)` or keyset batches for large exports.
- Create one `httpx.Client` or `requests.Session` per process or lifespan, with timeouts and retries configured, not one per call.
- Celery and RQ tasks receive ids, never model instances, load fresh data, are idempotent, set `max_retries` and backoff, and use the JSON serializer. Use `acks_late` only with idempotent tasks.

## Interface

- In Django templates and Jinja, auto-escaping is the protection against XSS. Never apply `|safe`, `mark_safe` or `{% autoescape off %}` to content that came from a user. Build HTML in Python with `format_html`, not f-strings.
- Configure standalone Jinja with `autoescape=select_autoescape()`. Flask enables it for `.html` templates.
- Every form that posts includes `{% csrf_token %}` in Django or the Flask-WTF token. Render forms through Django forms or the project's components so labels, errors and help text stay attached to fields.
- Keep logic out of templates: compute values in the view or a template tag. Use `{% url %}` and `{% static %}`, never hard-coded paths.

## Security

- Django: keep `DEBUG = False`, an exact `ALLOWED_HOSTS`, `SECRET_KEY` from the environment, `SECURE_SSL_REDIRECT`, `SESSION_COOKIE_SECURE`, `CSRF_COOKIE_SECURE`, `SECURE_HSTS_SECONDS` and `SECURE_PROXY_SSL_HEADER` only behind a trusted proxy. Run `python manage.py check --deploy --settings=<prod settings>` and fix every warning.
- Never use `@csrf_exempt` on a view that authenticates through the session cookie. DRF `SessionAuthentication` enforces CSRF, token authentication does not need it.
- DRF permissions: set `DEFAULT_PERMISSION_CLASSES` to authenticated by default. `has_object_permission` runs only when the view calls `get_object()`, so list endpoints and custom actions must filter `get_queryset()` by the user. Scope every queryset to what the user may see, which also makes another user's object answer `404`.
- Serializers and forms list `fields` explicitly, never `fields = "__all__"` or `exclude`. Mark server-controlled fields `read_only_fields`. In Pydantic, set `model_config = ConfigDict(extra="forbid")` on input models and use separate input and output models.
- SQL injection: the ORM parameterizes for you, but `raw()`, `extra()`, `RawSQL` and `cursor.execute` must take parameters as the second argument, never f-strings or `%` formatting. In SQLAlchemy use `text("... :id")` with bound parameters.
- Never `pickle.loads`, `yaml.load` without `SafeLoader` (use `yaml.safe_load`), `marshal` or `shelve` on untrusted data. Parse XML with `defusedxml`. Never call `eval` or `exec` on input.
- Run processes with `subprocess.run([...], check=True)` and an argument list, never `shell=True` or `os.system` with input.
- SSRF: validate URLs from input with `urllib.parse`, allow only expected schemes and hosts, resolve the host and refuse private and loopback addresses with `ipaddress`, and disable redirects or re-check each hop.
- Path traversal: resolve paths with `Path(root, name).resolve()` and require `is_relative_to(root.resolve())`. Store uploads through Django `Storage` with generated names, validate size and content, never trust the client's file name or content type.
- Use `secrets` for tokens, `hmac.compare_digest` for comparisons, Django's password hashers (Argon2 when `argon2-cffi` is installed) or `argon2-cffi` directly elsewhere.
- Audit dependencies with `pip-audit` or the scanner the project uses, and lock hashes (`uv` records them, `pip-compile --generate-hashes` for pip-tools).

## Performance

- Profile before optimizing: Django Debug Toolbar or `django-silk` in development, `cProfile` or `py-spy` for hot paths, and the database's `EXPLAIN` for slow queries.
- Add database indexes for fields used in filters and ordering, declared in `Meta.indexes` or the SQLAlchemy model so migrations create them.
- Cache with the framework's cache API and explicit keys and timeouts. Never cache per-user responses under a shared key.
- Move slow work (emails, exports, external calls) to tasks. Keep request handlers under the timeout of the proxy.
- Run production with the server the project uses (Gunicorn, Uvicorn workers, Granian) and size workers from measurement, not guesswork.

## Tests

- Use pytest with the plugins the project has: `pytest-django` (`@pytest.mark.django_db`, `client`, `django_assert_num_queries`), `pytest-asyncio` or `anyio` for async code, `pytest-mock` for `mocker`.
- Put shared fixtures in `conftest.py` and build data with `factory_boy` factories (`UserFactory`, `OrderFactory`) instead of large fixtures files.
- Test FastAPI with `TestClient` or with `httpx.AsyncClient(transport=ASGITransport(app=app))`, and replace dependencies through `app.dependency_overrides`, cleared after each test.
- Test against the real database engine of production (PostgreSQL through the CI service or Testcontainers), not SQLite, when the code relies on its features.
- Test the authorization of every endpoint with two users, the validation errors and the query count of list endpoints.
- Freeze time with `time-machine` or `freezegun` when the project uses them, and intercept HTTP with `respx` or `responses`.
- Coverage: `pytest --cov=<package> --cov-branch --cov-report=term-missing`, or `coverage run -m pytest` then `coverage report -m`. Respect `fail_under` in the configuration.
- Run `pytest -q` the way CI runs it, with `-x` while iterating and the full suite before reporting.

## Tooling and quality gates

- Lint and format with Ruff: `ruff check .` and `ruff format --check .`. Fix with `ruff check --fix` only for safe fixes, then review the diff. Respect the selected rule sets (including `S` for security and `B` for bugbear when enabled).
- Type check with the checker the project configures, `mypy` (with `django-stubs` and `djangorestframework-stubs` for Django) or `pyright`, in strict mode where the project uses it. Never add `# type: ignore` or `# noqa` to pass. Fix the type.
- Run pre-commit hooks with `pre-commit run --all-files` when the project has `.pre-commit-config.yaml`.
- Treat warnings as errors in tests (`filterwarnings = ["error"]` when configured) and fix deprecation warnings you introduce.

## Build, configuration and release

- Split settings by environment, all reading secrets from the environment. Provide `.env.example`, keep `.env` ignored.
- Build containers in stages: install the locked dependencies (`uv sync --locked --no-dev`, `poetry install --only main`, `pip install --require-hashes -r requirements.txt`) in a builder, copy the virtual environment and the code into a slim image of the pinned Python, run as a non-root user and set `PYTHONDONTWRITEBYTECODE=1` and `PYTHONUNBUFFERED=1`.
- Run `collectstatic` at build time for Django, and run migrations once per deploy as a release step, not from every worker on start.
- Version packages in `pyproject.toml` and build with `uv build` or `python -m build` when the project publishes a library.
- CI runs lock check, Ruff, the type checker, migrations check and tests with coverage on every pull request.

## Pitfalls

- Mutable default arguments (`def f(items=[])`). Use `None` and create the list inside.
- Blocking calls inside `async def` endpoints, which freeze the whole server.
- Forgetting `select_related` and `prefetch_related`, so a list of 50 rows runs 101 queries.
- Relying on `has_object_permission` for list views, which never call it.
- The `fields = "__all__"` on serializers, which exposes new columns and accepts writes to them.
- Saving model instances in tasks or caches, which then act on stale data.
- Comparing naive and aware datetimes. Keep `USE_TZ = True` and use `django.utils.timezone.now()` or `datetime.now(UTC)`.
- Catching broad exceptions around a database write inside `atomic()`, which leaves the transaction broken.
- Editing a migration that already ran, or deleting migrations to "clean up".
- Importing settings values at module level in a way tests cannot override.

## Definition of done

- Ruff lint and format checks pass, and the type checker passes with no new ignore comment.
- Every new endpoint authenticates, scopes its queryset to the user and has a test proving another user cannot reach the object.
- Serializers, forms and Pydantic models list their fields explicitly and forbid unknown input where the framework allows.
- Every schema change has a committed migration, and `makemigrations --check` or the Alembic check is clean.
- Lists use `select_related` or `prefetch_related` as needed, are paginated and have a query count test when they matter.
- No raw SQL, template, subprocess or deserialization call takes unparameterized input.
- Async code makes no blocking calls, and shared clients are created in the lifespan or app factory.
- Tasks take ids, are idempotent and are enqueued on commit.
- Configuration is read from the environment and documented in `.env.example`.
- Tests pass through the project's runner, and coverage of the changed code meets the project's threshold.
- For Django, `check --deploy` raises no new warning.

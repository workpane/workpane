# Ruby on Rails engineering

## Recognize the project

- Read `Gemfile` and `Gemfile.lock` for the Rails and Ruby versions and the gems in use, `.ruby-version` or `.tool-versions` for the interpreter, and `config/application.rb` for `config.load_defaults`, which decides framework behavior. Never use an API newer than the locked Rails version.
- Detect the test framework (`spec/` with `rspec-rails` or `test/` with Minitest), the front end (`importmap-rails`, `jsbundling-rails`, `cssbundling-rails`, Propshaft or Sprockets), the job backend (`solid_queue`, `sidekiq`, `good_job`), authorization (`pundit`, `cancancan` or project policies) and authentication (the Rails generator, `devise` or custom).
- Find the commands in `bin/` (`bin/setup`, `bin/dev`, `bin/rails`, `bin/rubocop`, `bin/brakeman`, and `bin/ci` with `config/ci.rb` on recent Rails), the `Procfile.dev`, `Rakefile` and the CI workflows.
- Read `config/routes.rb`, `db/schema.rb` or `db/structure.sql`, `app/models` and two or three controllers of the feature area before changing anything.

## Architecture

- Follow MVC as Rails intends: controllers translate HTTP into calls on models and render responses, models hold domain behavior, validations, associations and scopes, and views only present.
- Keep controllers thin: load and authorize the record, call one model method or object, respond. Business rules never live in controllers, views, helpers or callbacks that hide side effects.
- Keep models rich but focused. Extract a concern only when behavior is shared by several models and cohesive, and extract a plain Ruby object in `app/models` (a value object, a policy, a query, a form object) when a model grows a responsibility of its own.
- Add service objects, form objects or query objects only when they pay off: a workflow spanning several models, a form that does not map to one model, a reused complex query. Follow the folder and call convention the project already uses (`app/services`, `.call`), and do not wrap a single `create` in a service.
- Prefer explicit calls over callbacks for side effects such as emails, jobs and external calls. Callbacks are for keeping the record itself consistent (normalizing, deriving fields).
- Respect Zeitwerk: one constant per file, the file path matches the constant name, and no `require` of application code.

## Project structure

```
app/
├── controllers/             # Thin controllers, concerns for shared filters
├── models/                  # Active Record models, value objects, query and form objects
├── views/                   # Templates and partials, Turbo Stream templates
├── components/              # ViewComponent classes when the project uses them
├── javascript/controllers/  # Stimulus controllers
├── jobs/                    # Active Job classes
├── mailers/                 # Mailers and their views under views/
├── policies/                # Pundit policies when Pundit is used
└── services/                # Multi-model workflows, only if the project uses them
config/
├── credentials/             # Encrypted credentials per environment
├── initializers/            # Framework and gem configuration
└── locales/                 # I18n YAML files
db/
├── migrate/                 # Versioned migrations
└── schema.rb                # Generated, committed, never edited by hand
spec/ or test/               # Model, request, system, job and mailer tests, factories
```

- Use the generators (`bin/rails generate model`, `migration`, `controller`) with the project's options and delete what they create that the task does not need.
- Keep routes resourceful (`resources :orders, only: %i[index show create]`), nest at most one level, and add custom actions as new resources rather than extra verbs when possible.
- Document public classes and methods with comments only where the project writes them (RDoc or YARD style), and keep other comments rare.

## Patterns and practices

- Use strong parameters for every write: `params.expect(order: [:quantity, :note])` on Rails 8 or `params.require(:order).permit(:quantity, :note)` before it. Never permit `:role`, `:admin`, `:user_id` or ownership fields from user input, and never call `permit!`.
- Scope every lookup by the current user or tenant: `current_user.orders.find(params[:id])`, never `Order.find(params[:id])` for owned data. A missing record then raises `ActiveRecord::RecordNotFound`, which renders 404.
- Authorize every action. With Pundit call `authorize @order` and `policy_scope(Order)`, and add `after_action :verify_authorized` and `verify_policy_scoped` in the base controller. With CanCanCan use `load_and_authorize_resource` and `accessible_by(current_ability)`. With project policies, follow them on every endpoint including JSON and Turbo actions.
- Use model validations for user-facing rules and database constraints (`null: false`, foreign keys, unique indexes, check constraints) for integrity. A uniqueness validation without a unique index is a race condition.
- Use `normalizes` for input normalization and `generates_token_for` for signed purpose tokens on Rails 7.1 or newer.
- Wrap multi-record changes in `ActiveRecord::Base.transaction` (or `Model.transaction`) and keep network calls and job enqueuing outside or after commit.
- Return `422` with the re-rendered form on validation failure (`status: :unprocessable_entity`, or `:unprocessable_content` where the project's Rack version uses it) and redirect with `status: :see_other` after `DELETE`, so Turbo handles both.
- Keep every user-facing string in `config/locales` and call `t(".title")` with lazy lookup in views. Run `i18n-tasks` when the project uses it.
- Configure through credentials and environment-specific files, never `if Rails.env.production?` scattered in code.
- Use `Rails.logger` with tagged or structured logging as configured, and keep `config.filter_parameters` covering passwords, tokens, secrets and personal fields.

## Data, networking and persistence

- Prevent N+1 queries with `includes`, `preload` or `eager_load`, enable `strict_loading` where the project uses it, and run Bullet in development and tests when the project has it. Check `log/development.log` or the test log for repeated queries.
- Select only what you need (`pluck`, `select`, `exists?` instead of `present?` on a relation, `count` versus `size` deliberately), and iterate large sets with `find_each` or `in_batches`.
- Write reversible migrations: `change` with reversible methods, or `up` and `down` when not. Add an index for every foreign key and every column used in lookups, ordering or uniqueness.
- Make migrations safe on a live database: avoid column defaults that rewrite the table on databases that still do so, add indexes concurrently on PostgreSQL with `algorithm: :concurrently` and `disable_ddl_transaction!`, split column removal into ignoring it (`self.ignored_columns`) and dropping it in a later deploy. Follow `strong_migrations` when the project uses it.
- Never edit a migration that has run elsewhere, never edit `schema.rb` by hand, and commit the schema produced by `bin/rails db:migrate`.
- Data backfills run in jobs or tasks in batches, not inside schema migrations that reference model classes.
- Make jobs idempotent and pass identifiers or records (serialized through GlobalID), never large objects. Configure `retry_on` and `discard_on` for the errors each job expects, and enqueue only after the data the job reads is committed.
- On Rails 8 the default stack is Solid Queue, Solid Cache and Solid Cable on the database. Keep their separate databases and configuration as generated.
- Call external HTTP services through one client per service with timeouts, retries only for idempotent requests, and errors mapped to domain failures.

## Interface

- Build pages with Hotwire: Turbo Drive for navigation, Turbo Frames for parts that update independently, Turbo Streams for targeted updates from responses or broadcasts, and morphing page refreshes (`turbo_refreshes_with method: :morph`) where the project uses them.
- Use Stimulus for behavior that needs JavaScript: one small controller per behavior, configured with `data-controller`, targets and values, never inline scripts.
- Stream broadcasts only to authorized subscribers with `turbo_stream_from` on a record or signed name tied to the user, never to a global stream that leaks other users' data.
- Use the form builders (`form_with model:`), the label helpers and the error display pattern the project has, so fields stay accessible and errors sit next to their field.
- Use partials or ViewComponent for repeated markup, the project's CSS approach (Tailwind, plain CSS, Bootstrap) and its design tokens.

## Security

- Leave CSRF protection on (`protect_from_forgery` is the default for `ActionController::Base`) and keep `csrf_meta_tags` in the layout. API controllers inheriting `ActionController::API` authenticate with tokens instead of cookies.
- Never call `html_safe` or `raw` on anything containing user input, and never render user input with `render inline:`. Use `sanitize` with an allowlist when rich text is required, or Action Text.
- Never interpolate into SQL: use `where(column: value)`, `where("price > ?", value)` or named binds. Pass sort columns and directions through an allowlist.
- Never pass user input to `constantize`, `send`, `public_send`, `render`, `redirect_to` without an allowlist or `url_from`, or to `system` and backticks. Use `YAML.safe_load`, never `Marshal.load` on untrusted data.
- Store secrets with `bin/rails credentials:edit --environment production`, read them with `Rails.application.credentials.dig(...)`, and keep `config/master.key` and the environment keys out of the repository.
- Set `config.force_ssl = true` in production, configure the Content Security Policy initializer, and use `has_secure_password` or the project's authentication with session reset on login.
- Rate limit sign-in, password reset and expensive endpoints with `rate_limit` (Rails 7.2 and newer) or Rack::Attack when the project uses it.
- Validate Active Storage uploads by content type and size, and serve private files through authorized controllers or expiring URLs.

## Performance

- Find slow queries with the logs, `EXPLAIN` (`relation.explain`) and the APM the project uses, then add the index or rewrite the query.
- Cache with fragment caching (`cache record do`) and Russian doll caching keyed on records with `touch: true` on associations, and `Rails.cache.fetch` with explicit expiry for computed data.
- Use counter caches for counts shown in lists, and move slow work (email, exports, external calls) to jobs.
- Paginate every list with the project's paginator and an upper limit on page size.

## Tests

- Follow the project's framework. With RSpec: model specs, request specs for controllers and APIs (controller specs only where the project keeps them), system specs with Capybara, job and mailer specs, and shared examples sparingly. With Minitest: model, controller or integration tests and `ApplicationSystemTestCase`.
- Build data with FactoryBot (`build`, `build_stubbed` where persistence is not needed, `create` only when required) or fixtures as the project does. Keep factories minimal and add traits for variants.
- Test authorization explicitly: another user's record returns 404 or 403, and a regular user cannot reach admin actions. Test policies directly when Pundit is used.
- Write system tests for critical user flows, with the headless driver the project configures, and wait with Capybara matchers (`have_content`) instead of `sleep`.
- Stub external HTTP with WebMock or VCR, and assert enqueued jobs with `have_enqueued_job` or `assert_enqueued_with`.
- Run `bundle exec rspec` or `bin/rails test`, and `bin/rails test:all` to include system tests with Minitest.
- Measure coverage with SimpleCov started at the very top of `spec/spec_helper.rb` or `test/test_helper.rb` (`SimpleCov.start "rails"` with `enable_coverage :branch`), read `coverage/index.html` or the summary, and configure merging when tests run in parallel.

## Tooling and quality gates

- Run `bin/rubocop` (often `rubocop-rails-omakase`, or a config with `rubocop-rails`, `rubocop-rspec` and `rubocop-performance`) and fix offenses. Use `-a` for safe autocorrection and review every change.
- Run `bin/brakeman` with no new warnings, and justify any ignore entry in `config/brakeman.ignore`.
- Run `bundle audit` or the project's dependency scanner, and `bin/importmap audit` with importmaps.
- Run `erb_lint`, `i18n-tasks` and `database_consistency` when the project has them.

## Build, configuration and release

- Keep environment differences in `config/environments/*.rb` and credentials. Production needs `RAILS_MASTER_KEY` or the environment key file from the secret store.
- Run `bin/rails db:prepare` on deploy, and order deploys so that new code works with both the old and the new schema.
- Precompile assets in the build (`bin/rails assets:precompile`) and build the container with the generated multi-stage `Dockerfile` and Kamal or the project's deploy tool.
- Update gems with `bundle update --conservative gem_name`, read changelogs, run the full suite, and follow upgrade guides with `bin/rails app:update` for framework upgrades.

## Pitfalls

- The `Model.find(params[:id])` on owned data instead of a scoped find is the most common Rails IDOR.
- The `update(params[:order])` without strong parameters, or permitting ownership and role fields, enables mass assignment.
- Callbacks that send emails or call services run on every save, inside transactions, and in tests.
- The `default_scope` surprises every query and is hard to escape. Use named scopes.
- The `after_save` enqueuing a job before commit lets the job run before the data exists.
- Queries in views and serializers cause N+1 that tests do not notice without Bullet or strict loading.
- The `rescue => e` swallowing errors, or rescuing `Exception`, hides bugs and signals.
- Time handled without zones: use `Time.current`, `Date.current` and `in_time_zone`, never `Time.now` or `Date.today`.
- Changing `config.load_defaults` or a framework default without reading what it changes.
- Strings in views instead of locale files, and `html_safe` on interpolated strings.

## Definition of done

- Every new or changed action authenticates, authorizes the record and scopes lookups to the current user or tenant.
- Writes go through strong parameters that permit only user-editable fields.
- Migrations are reversible or explicitly irreversible, indexed, safe for a live database, and `db/schema.rb` matches them.
- No N+1 query was introduced, as checked with Bullet, strict loading or the logs.
- Side effects run after commit, and jobs are idempotent with explicit retry rules.
- Turbo responses use the right status codes, and broadcasts reach only authorized streams.
- User-facing text lives in locale files.
- No `html_safe`, `raw`, string-built SQL or dynamic `send` on user input.
- The command `bin/rubocop` and `bin/brakeman` report nothing new.
- The test suite passes, including system tests for changed flows, and SimpleCov coverage of the changed code was read.
- Secrets live in credentials or the environment, and nothing secret is committed or logged.

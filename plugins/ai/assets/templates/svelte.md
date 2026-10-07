# Svelte engineering

## Recognize the project

- Read `package.json` for the versions of `svelte`, `@sveltejs/kit`, `@sveltejs/adapter-*`, `vite`, `typescript`, the test libraries and the `scripts` (`dev`, `build`, `preview`, `check`, `lint`, `format`, `test`). Use those scripts.
- Detect the package manager from the lock file and the pinned Node version, and never create a second lock file.
- A `svelte.config.js` with `@sveltejs/kit` and a `src/routes` folder is a SvelteKit application. A Vite project with `@sveltejs/vite-plugin-svelte` and no Kit is a plain Svelte single page application or a component library.
- Check the majors, because they change the rules. Svelte 5 uses runes, while Svelte 4 code (`export let`, `$:` statements, `on:click`) still compiles in legacy mode. SvelteKit 2 uses `$lib`, `$app/stores` alongside `$app/state`, and the `$env/*` modules. SvelteKit 3 replaces `$lib` with `#lib`, removes `$app/stores`, moves environment variables to `$app/env/private` and `$app/env/public` declared in `src/env.ts`, requires Vite 8 and Node 22, and deprecates `invalidateAll` in favor of `refreshAll`. Write the idioms of the installed version.
- Read `svelte.config.js` (adapter, `kit.csp`, `kit.csrf`, aliases, `experimental` flags), `vite.config.ts`, `tsconfig.json`, `eslint.config.js`, `playwright.config.ts` and `src/app.d.ts` for the `App.Locals`, `App.PageData` and `App.Error` types.
- New projects are scaffolded with `npx sv create`, and integrations are added with `npx sv add`.

## Architecture

- Write components with runes in Svelte 5. Use the legacy syntax only in a project that has not migrated, never mix both in one component, and migrate with `npx sv migrate svelte-5` only when asked.
- In SvelteKit, the route tree is the architecture: `+page.svelte` renders, `+page.ts` or `+layout.ts` load data on server and client (universal load), `+page.server.ts` or `+layout.server.ts` load data only on the server and declare form actions, `+server.ts` exposes endpoints, and `+error.svelte` renders failures.
- Choose server load when the code needs secrets, the database, cookies or private APIs, or returns data that must not reach the client code. Choose universal load for public APIs that may be called directly from the browser on client navigation, or to return values that cannot be serialized, such as component constructors.
- Keep server-only code in `src/lib/server` (or any `server` directory in SvelteKit 3), which SvelteKit refuses to import into client code. The database client, secrets and data access functions live there.
- Put business rules in plain TypeScript modules without Svelte imports, so they are tested without rendering. Components orchestrate, they do not compute business outcomes.
- Mutations go through form actions, which work without JavaScript and enhance progressively. Use `+server.ts` endpoints for clients other than the application's own forms, and remote functions (`query`, `form`, `command` from `$app/server`) only when the project enables `experimental.remoteFunctions`.

## Project structure

```
src/
  app.html                 # Page template
  app.d.ts                 # App.Locals, App.PageData and App.Error types
  hooks.server.ts          # handle, handleFetch, handleError on the server
  hooks.client.ts          # handleError in the browser
  lib/                     # Imported through $lib (#lib in SvelteKit 3)
    components/            # Shared components and design system primitives
    features/
      orders/              # Components, state and schemas of one feature
    server/                # Server-only modules: database, auth, services
    state/                 # Shared runes state in .svelte.ts modules
    utils/                 # Pure functions
  routes/
    (app)/                 # Route group sharing a layout without a URL segment
      orders/
        +page.svelte       # Order list
        +page.server.ts    # Server load and form actions
        [id]/+page.svelte  # One order
    api/
      orders/+server.ts    # Endpoint for external clients
static/                    # Files served as they are
tests/                     # Playwright end-to-end tests
```

- Route files hold only routing concerns. Move components and logic used by a route into `src/lib`, and keep a feature's parts together there.
- Name components in PascalCase (`OrderList.svelte`), modules with runes outside components with the `.svelte.ts` extension, and tests beside the code as `*.test.ts` or `*.svelte.test.ts` when they use runes.

## Patterns and practices

- Declare props with `let { title, items = [], onselect }: Props = $props()` and a `Props` type. Mark two-way props with `$bindable()`. Never mutate a prop that is not bindable.
- Use `$state` for reactive state. Objects and arrays become deep proxies, so mutate them directly (`items.push(item)`). Use `$state.raw` for large data replaced as a whole.
- Use `$derived` for every value computed from state, and `$derived.by` for multi-line computations. Never compute derived values with `$effect`.
- Use `$effect` only to synchronize with the outside world, such as a third-party library, a canvas or a subscription, and return its cleanup. Effects do not run on the server. `$effect.pre` runs before the DOM updates.
- Handle events with attributes (`onclick={handle}`), pass callbacks as props instead of `createEventDispatcher`, and compose markup with snippets (`{#snippet}` and `{@render}`) instead of slots in new code.
- Shared state lives in `.svelte.ts` modules exporting objects or classes with `$state` fields. Export an object or a getter, never a reassigned primitive `$state` variable, which loses reactivity across modules.
- Stores (`writable`, `readable`, `derived`) remain valid for streams and interop with libraries that expect the store contract. For new shared state, prefer runes in `.svelte.ts` modules unless the project standardizes on stores.
- On the server, a module-level variable is shared by every request. Never keep user data in module-level state or a shared `.svelte.ts` instance during server rendering. Use `event.locals`, the data returned from `load`, or `setContext` and `getContext` scoped to the component tree.
- Put per-request values, such as the authenticated user, into `event.locals` in the `handle` hook, typed in `App.Locals`, and read them in server load functions and actions.
- In load functions, use the `fetch` passed to `load` so cookies, relative URLs and server rendering work, start independent requests in parallel, and throw `error(404, 'Not found')` or `redirect(303, '/login')` from `@sveltejs/kit` for control flow.
- In form actions, validate `await request.formData()` with a schema (Zod or Valibot), return `fail(400, { errors, values })` for invalid input without echoing secrets, and redirect after success. Enhance forms with `use:enhance` from `$app/forms`.
- Read page state from `page` in `$app/state` (SvelteKit 2.12 and newer). `$app/stores` is the older form and is removed in SvelteKit 3.
- Localize with the library the project uses, such as Paraglide or `svelte-i18n`, with message functions and locale-aware formatting.
- Documentation comments use JSDoc or TSDoc (`/** ... */`) on exported module APIs and component props where the project already writes them. Comments stay rare.

## Data, networking and persistence

- Data returned from a server load is serialized to the client, so return only the fields the page needs and never secrets, hashes or other users' data.
- Rerun loads after a mutation with the built-in invalidation of form actions, `invalidate(url)` with `depends` keys, or `invalidateAll` in SvelteKit 2 and `refreshAll` in SvelteKit 3.
- Endpoints in `+server.ts` export `GET`, `POST` and the other methods, validate input, return `json(data)` with an explicit status, and authorize from `event.locals`.
- Use `handleFetch` to rewrite or authenticate requests to internal APIs made during server rendering, and never forward the user's cookies to a third-party host.
- Keep sessions in `HttpOnly`, `Secure`, `SameSite` cookies through `cookies.set` with an explicit `path`, and never store tokens in `localStorage`.

## Interface

- Use the component library the project has, such as shadcn-svelte, Skeleton, Flowbite Svelte or Bits UI, with its theme tokens. Style with scoped `<style>` blocks, Tailwind with tokens, or custom properties, as the project does. No raw colors or arbitrary values in components.
- Implement dark mode through custom properties and `prefers-color-scheme` or a class set before hydration to avoid a flash.
- Keep the compiler's accessibility warnings (`a11y_*`) at zero. Use native `button`, `a` and `label`, bind labels to inputs, and give icon buttons accessible names. Never silence a warning with `svelte-ignore` to pass a check.
- Use `transition:` and `animate:` sparingly and respect `prefers-reduced-motion`. Use `@sveltejs/enhanced-img` or the image pipeline of the project, always with dimensions.
- SvelteKit manages focus and announces page changes on navigation. Keep a unique `<title>` per page in `<svelte:head>` so the announcement is meaningful.

## Security

- Svelte escapes text expressions. Never pass user content to `{@html}`. When rendering HTML from outside, such as Markdown, sanitize it with DOMPurify on the side that renders it, with a strict allowlist.
- Check the scheme of user-provided URLs bound to `href` or `src`, allowing only `http:`, `https:` and the schemes the feature needs, since `javascript:` URLs execute.
- In SvelteKit 2, import secrets only from `$env/static/private` or `$env/dynamic/private`, and expose only `PUBLIC_` variables through `$env/static/public` or `$env/dynamic/public`. In SvelteKit 3, declare variables in `src/env.ts` with `defineEnvVars`, mark only browser-safe ones `public: true`, and import secrets from `$app/env/private`. SvelteKit refuses private modules in client code, so never work around that refusal.
- SvelteKit rejects cross-origin form submissions by default. Keep that check, and allow other origins only by listing them exactly in `csrf.trustedOrigins`. The older `csrf.checkOrigin: false` disabled the protection entirely and is removed in SvelteKit 3.
- Authorize in every server load, form action and endpoint, not only in a layout. A parent layout's server load does not protect a child's actions or endpoints, and form actions can be called directly.
- Configure a Content Security Policy with `kit.csp` (mode `auto`, `hash` or `nonce`) so SvelteKit adds hashes or nonces to its own inline scripts, and set the other security headers in the `handle` hook with `setHeaders` or `response.headers`.
- Return generic messages from `handleError` and log the details on the server. Never send stack traces to the client.

## Performance

- Prefer server rendering with hydration, and prerender pages that do not change per request with `export const prerender = true`. Turn off `ssr` only for pages that truly cannot render on the server.
- Stream slow, non-essential data by returning promises from a server load and rendering them with `{#await}`, so the page shows immediately.
- Keep `$effect` and deep `$state` proxies off large datasets: use `$state.raw` and replace the value. Virtualize long lists.
- Use link preloading (`data-sveltekit-preload-data`) deliberately, and lazy import heavy components and libraries with dynamic `import()`.
- Inspect the bundle with `rollup-plugin-visualizer` or the analyzer the project uses, and measure Core Web Vitals on the production build with `vite preview`, never on the development server.

## Tests

- Unit and component tests use Vitest with `@testing-library/svelte` or `vitest-browser-svelte`, whichever the project has. Query by role and label, drive interactions with `user-event`, and assert what the user sees.
- Test runes logic in `.svelte.test.ts` files so the compiler processes the runes, and wrap code that creates effects in `$effect.root` and clean it up.
- Test load functions, actions and endpoints as functions: build the event object they need (`params`, `locals`, `request`, `cookies`) and assert on returned data, `fail` results, thrown `error` and `redirect` statuses.
- Mock the network at the boundary with Mock Service Worker when the project has it, and keep the database behind a module in `src/lib/server` that tests can replace.
- End-to-end tests use Playwright against `vite preview` or the built server, as configured in `playwright.config.ts`, with role-based locators and web-first assertions. Cover progressive enhancement by testing critical forms with JavaScript disabled when the project relies on it.
- Run once with `npx vitest run`, and measure coverage with `npx vitest run --coverage` and `@vitest/coverage-v8`, written to `coverage/`.

## Tooling and quality gates

- Type check with `svelte-check`, usually through `npm run check`, which runs `svelte-kit sync` first so the generated `$types` exist. Plain `tsc` does not check `.svelte` files. Keep its output at zero errors and zero warnings with `--fail-on-warnings` where the project uses it.
- Import the generated types (`PageLoad`, `PageServerLoad`, `Actions`, `RequestHandler`, `PageProps`) from `./$types` instead of declaring them by hand.
- Lint with ESLint flat config and `eslint-plugin-svelte` with the TypeScript rules, and format with Prettier and `prettier-plugin-svelte`. Check both in continuous integration.
- Never add `svelte-ignore`, `eslint-disable`, `@ts-ignore` or `any` to pass a check.

## Build, configuration and release

- Pick the adapter of the target deliberately: `adapter-node` for a Node server or container, `adapter-static` for a fully prerendered site (with a `fallback` page for a single page application), and `adapter-vercel`, `adapter-netlify` or `adapter-cloudflare` for those platforms. `adapter-auto` only detects supported platforms and should be replaced with the explicit adapter in production.
- Build with `vite build` and verify with `vite preview` or `node build` for `adapter-node`. Configure the public origin of `adapter-node` the way the installed version documents, since SvelteKit 3 replaces the `ORIGIN` variable with `kit.paths.origin`.
- Static environment variables are inlined at build time, and dynamic ones are read at runtime. Use dynamic variables when one build is deployed to several environments.
- Commit `.env.example` with names only, keep `.env` ignored, and validate variables at startup with a schema.
- Upgrade majors with `npx sv migrate` and the migration guide of the version, in a change of its own.

## Pitfalls

- Using `$effect` to compute derived state, or updating state inside an effect that reads it, which loops.
- Exporting a reassigned `$state` primitive from a module, or destructuring reactive objects and losing reactivity.
- Keeping user data in module-level state on the server, leaking it between requests.
- Putting secrets in universal `load` functions, `+page.ts` or public environment variables, or returning private fields from a server load.
- Protecting a section only in `+layout.server.ts` and leaving its actions and endpoints open.
- Using the global `fetch` in `load`, which loses cookies and duplicates requests during hydration.
- Rendering unsanitized HTML with `{@html}`, or disabling the origin check of form submissions.
- Mixing legacy syntax and runes in one component, or writing `$app/stores` and `$lib` in a SvelteKit 3 project.
- Running `tsc` instead of `svelte-check` and believing components are type checked.
- Shipping with `adapter-auto` to a target it does not detect.

## Definition of done

- The check `svelte-check` reports zero errors and warnings, including accessibility warnings, with no `svelte-ignore` added.
- ESLint and Prettier pass with no new disable comment.
- Components use the runes and imports of the installed Svelte and SvelteKit versions, with typed `$props`.
- Derived values use `$derived`, and every `$effect` synchronizes with something outside and cleans up.
- Server-only code sits under a `server` directory, and no module-level state holds per-request data.
- Every server load, action and endpoint validates input and authorizes from `event.locals`.
- Secrets come only from the private environment modules, and server loads return only the fields the page needs.
- No `{@html}` renders unsanitized content, the cross-origin form check stays on, and `kit.csp` or the project's headers are in place.
- Forms work through actions with `use:enhance` and show validation errors returned by `fail`.
- Vitest and Playwright tests cover the change, pass without watch mode and meet the coverage threshold.
- The command `vite build` succeeds with the explicit adapter of the target, and the build was exercised with a preview.

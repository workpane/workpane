# Vue engineering

## Recognize the project

- Read `package.json` first: the versions of `vue`, `nuxt`, `vue-router`, `pinia`, `typescript`, `vue-tsc`, the form and test libraries, and the `scripts` block with the real commands for `dev`, `build`, `test`, `lint`, `typecheck` and `format`. Use those scripts.
- Detect the package manager from the lock file (`package-lock.json`, `pnpm-lock.yaml`, `yarn.lock`, `bun.lock`) and honor the `packageManager` field and the pinned Node version. Never create a second lock file.
- A `nuxt.config.ts` makes it a Nuxt application. Check the major version the project pins: Nuxt 4 keeps application code in `app/` with `server/` and `shared/` at the root, while Nuxt 3 projects usually keep everything at the root. A `vite.config.ts` with `index.html` and `src/main.ts` is a Vite single page application created by `create-vue`.
- Confirm the Vue major is 3. A Vue 2 project (Options API, `Vue.extend`, Vuex) follows different rules, and migrating it is a task of its own.
- Read `tsconfig*.json` (strict mode on), `eslint.config.*`, `vitest.config.*`, `playwright.config.*`, and in Nuxt the `modules` and `runtimeConfig` of `nuxt.config.ts`.

## Architecture

- Write components with the Composition API and `<script setup lang="ts">`. Follow the Options API only in a project that still uses it throughout, and never mix both styles in one component.
- Keep the dependency direction one way: pages compose features, features use shared components and composables, and business rules live in plain TypeScript modules that do not import Vue, so they are tested without mounting anything.
- Separate server state from client state. Data fetched from an API belongs to the data layer (`useFetch` and `useAsyncData` in Nuxt, a query library such as TanStack Vue Query or Pinia Colada when the project has one). Pinia holds client state shared across distant components, such as the session, preferences or a cart.
- Extract reusable stateful logic into composables named `useSomething` that return refs and functions. A composable owns its side effects and cleans them up with `onScopeDispose` or `onUnmounted`.
- In Nuxt, code under `server/` runs only on the server and is the place for secrets, the database and third-party calls. Pages and components reach it through `/api` routes, never by importing server code.

## Project structure

```
app/                       # Nuxt 4 source directory (src/ in a Vite app)
  pages/                   # File-based routes
    orders/
      [id].vue             # Page of one order
  layouts/                 # Page layouts
  components/              # Auto-imported shared components
    base/                  # Design system primitives
  composables/             # Auto-imported composables
  features/
    orders/
      components/          # Components of this feature
      composables/         # Composables of this feature
      schema.ts            # Zod schemas and inferred types
  stores/                  # Pinia stores
  middleware/              # Route middleware
  plugins/                 # Nuxt plugins
  utils/                   # Auto-imported pure functions
server/
  api/                     # API endpoints, such as orders/[id].get.ts
  utils/                   # Server-only helpers, database client
shared/                    # Types and schemas used by app and server
public/                    # Static files served as they are
tests/                     # Unit and component tests when not colocated
e2e/                       # Playwright tests
```

- New code goes into its feature. Move a component or composable to the shared folders only when a second feature uses it.
- Name components in PascalCase with multi-word names (`OrderList.vue`, never `List.vue`), and base components with a consistent prefix such as `Base` or `App`. Composables live in files named after them.
- In Nuxt, respect the auto-import directories and their naming rules instead of adding manual registrations, and do not create a folder that shadows one Nuxt scans unless you mean it.

## Patterns and practices

- Declare props with `defineProps<{ ... }>()` and defaults through reactive props destructure (Vue 3.5 and newer) or `withDefaults`, events with `defineEmits<{ ... }>()`, and two-way bindings with `defineModel()`. Never mutate a prop.
- Use `ref` for state by default, `reactive` only for objects you never replace, and `shallowRef` for large data replaced as a whole. Never destructure a `reactive` object or a store directly, because the result loses reactivity. Use `toRefs` or `storeToRefs`.
- Use `computed` for derived values and keep it free of side effects. Use `watch` only for side effects in response to a change, with explicit sources, and prefer `watchEffect` only when the dependencies are obvious. Do not use a watcher to copy one piece of state into another.
- Template refs use `useTemplateRef` in Vue 3.5 and newer, or a `ref` with the same name in older projects.
- Write Pinia stores with the setup syntax when the project does: `ref` for state, `computed` for getters and functions for actions. Keep stores small and focused, one per domain, and call one store from another inside actions, not at module level.
- In Nuxt with server rendering, never keep shared mutable state in a module-level variable, because it is shared across requests and leaks one user's data to another. Use `useState`, Pinia (which Nuxt instantiates per request) or `provide` and `inject`.
- Use `provide` and `inject` with typed `InjectionKey` symbols for dependency injection within a subtree, not for global state.
- Every `v-for` has a stable `:key` from the data. Never combine `v-if` and `v-for` on the same element. Filter in a `computed` instead.
- Routing in a Vite application uses Vue Router with typed routes, lazy-loaded route components (`component: () => import(...)`) and navigation guards that return a route location or `false` instead of calling `next`. Nuxt uses file-based routes in `pages/`, `definePageMeta` and route middleware.
- Route guards and Nuxt middleware improve the experience but are not security. The API authorizes every request.
- Forms use VeeValidate with Zod through `toTypedSchema` from `@vee-validate/zod` or the adapter the project uses, and the same schema validates on the server. Bind fields through `useForm` and `defineField` or the components of the project.
- Handle errors with `onErrorCaptured` in a boundary component or `app.config.errorHandler`, and in Nuxt with `error.vue`, `createError` and `showError`. Show a recovery action, never a raw error.
- Localize with `vue-i18n` or `@nuxtjs/i18n` when the project has them, using message keys and the pluralization of the library.
- Documentation comments use TSDoc or JSDoc (`/** ... */`) on exported composables and public APIs where the project already writes them. Comments stay rare.

## Data, networking and persistence

- In Nuxt pages and components, load data with `useFetch` or `useAsyncData` so it is fetched once on the server and hydrated on the client. Give `useAsyncData` a unique, stable key, and use `pick` or `transform` to keep only the fields the page needs in the payload.
- Use `$fetch` for calls triggered by user actions, such as a form submission, and inside server routes. Never call `useFetch` in an event handler or a function that runs after setup.
- Handle the `status` and `error` returned by `useFetch` and `useAsyncData`, and refresh with `refresh()` or `refreshNuxtData(key)` after a mutation.
- Server routes in `server/api` use `defineEventHandler`, with the method in the file name (`orders.post.ts`). Validate input with `readValidatedBody`, `getValidatedQuery` or `getValidatedRouterParams` and a Zod schema, throw `createError` with a status code for expected failures, and return only the fields the client needs.
- In a Vite application, call the API through one typed client module, validate untrusted responses with Zod at the boundary, and keep loading, error and empty states in the component or the query library.
- Keep session tokens in `HttpOnly` cookies set by the server. In Nuxt, read and write cookies on the server with `getCookie`, `setCookie` or `useCookie`, and never put a token in `localStorage` or in public runtime configuration.

## Interface

- Use the component library the project uses, such as Nuxt UI, PrimeVue, Vuetify, Quasar, Element Plus or an internal kit, and its theming tokens. Build a new primitive only when the library has none.
- Style with scoped styles (`<style scoped>`), CSS Modules or Tailwind, as the project does, with design tokens from custom properties or the theme configuration. No raw colors or arbitrary values in components.
- Implement dark mode through the token layer, with `@nuxtjs/color-mode` or `useDark` from VueUse when the project has them, without a flash of the wrong theme.
- Use native elements for semantics (`button`, `a`, `label`, `fieldset`), bind every label to its input, and rely on the accessible components of the library for dialogs, menus and comboboxes. Return focus to the trigger when a dialog closes.
- Use `<NuxtLink>` or `<RouterLink>` for navigation and `<NuxtImg>` when the project has `@nuxt/image`, always with dimensions.
- Use `<Teleport>` for overlays and `<Suspense>` only where async setup really needs it. Wrap browser-only widgets in `<ClientOnly>` in Nuxt.

## Security

- Vue escapes interpolation and bindings. Never pass user content to `v-html`. When rendering HTML from outside, such as Markdown, sanitize it with DOMPurify before binding it, with a strict allowlist.
- Never compile templates from user input at runtime, and never bind user content into `:style` or event handler attributes.
- Check the scheme of every user-provided URL bound to `:href` or `:src`. Vue does not block `javascript:` URLs, so allow only `http:`, `https:` and the schemes the feature needs.
- Only `VITE_` variables in Vite and `runtimeConfig.public` in Nuxt reach the browser, and both are public. Keep secrets in the private keys of `runtimeConfig`, override them with `NUXT_` environment variables at runtime, and read them with `useRuntimeConfig(event)` in server routes only.
- Treat every server route as a public endpoint: validate the input, authenticate, authorize per object and rate limit sensitive operations. Do not rely on a page middleware to protect an API route.
- Set a Content Security Policy and the security headers the project uses, for example through `routeRules` headers or the `nuxt-security` module when the project has it.
- The payload of `useFetch` and `useAsyncData` is serialized into the HTML. Never load private fields into it.

## Performance

- Lazy load route components and heavy components with `defineAsyncComponent`, or with the `Lazy` prefix in Nuxt.
- Use `shallowRef` and `markRaw` for large immutable data and for third-party instances such as charts and maps, so Vue does not make them deeply reactive.
- Use `v-once` and `v-memo` only where profiling shows repeated rendering of static or rarely changing parts. Virtualize long lists.
- In Nuxt, choose the rendering of each route with `routeRules`, such as prerendering, caching with stale-while-revalidate or client-only, and keep the payload small with `pick`.
- Inspect the bundle with `npx nuxi analyze` in Nuxt or `rollup-plugin-visualizer` in Vite before and after adding a dependency.
- Measure with the Vue DevTools performance tab and Core Web Vitals on a production build, not the development server.

## Tests

- Unit and component tests use Vitest with `@vue/test-utils` (`mount`, `shallowMount` only when the project does) or `@testing-library/vue`, whichever the project has. Find elements by role, label or text, trigger events with `await wrapper.trigger(...)` or `user-event`, and await `nextTick` or `flushPromises` before asserting.
- Test stores with a fresh `createPinia()` and `setActivePinia` per test, and mount components with `createTestingPinia` from `@pinia/testing` to stub actions or set initial state.
- Test composables by calling them inside a test component or an effect scope when they use lifecycle hooks, and directly when they do not.
- In Nuxt, use `@nuxt/test-utils` with the `nuxt` Vitest environment and `mountSuspended` for components that rely on auto-imports, `useFetch` or plugins, and `registerEndpoint` or `mockNuxtImport` to replace what you do not own.
- Mock the network at the boundary with Mock Service Worker when the project has it. Test server route handlers by calling their validation and data functions directly or through `@nuxt/test-utils/e2e`.
- End-to-end tests use Playwright against a production build, with role-based locators and web-first assertions. Cover critical journeys and authorization between two users.
- Run once with `npx vitest run`, and measure coverage with `npx vitest run --coverage` and `@vitest/coverage-v8`, written to `coverage/`.

## Tooling and quality gates

- Type check templates and scripts with `vue-tsc --noEmit` (or `vue-tsc --build` with project references) in a Vite project and `nuxi typecheck` in Nuxt. Plain `tsc` does not check `.vue` files.
- Lint with ESLint flat config and `eslint-plugin-vue` at the `flat/recommended` level or stricter, with `@vue/eslint-config-typescript`, or `@nuxt/eslint` in Nuxt. Keep `vue/multi-word-component-names`, `vue/no-v-html` and `vue/require-v-for-key` on.
- Format with Prettier or the formatter the project uses, and check it in continuous integration.
- Trust the command line checks over the editor. Never add `eslint-disable`, `@ts-ignore` or `any` to pass a check.

## Build, configuration and release

- Build with `vite build` or `nuxi build`, and verify the result with `vite preview` or `node .output/server/index.mjs` before reporting behavior or performance.
- Generate a fully static Nuxt site with `nuxi generate` only when no route needs the server at request time.
- Choose the Nitro preset of the host (Node server, a serverless platform or a static host) through the `nitro.preset` option or the environment of the host, and keep it consistent with how the project deploys.
- A Vite build bakes `VITE_` variables in at build time, so each environment needs its own build or configuration loaded at runtime. Nuxt reads `NUXT_` overrides when the server starts.
- Commit a `.env.example` with names only, and keep `.env` ignored.
- Upgrade Nuxt and Vue majors following their migration guides and codemods in a change of its own, after running `npx nuxi upgrade` when the project uses it.

## Pitfalls

- Destructuring `reactive` objects, props in old versions, or Pinia stores without `storeToRefs`, then wondering why the view does not update.
- Forgetting `.value` in script code, or adding it in templates where refs unwrap automatically.
- Module-level state on the server in Nuxt that leaks between requests.
- Calling `useFetch` outside setup, or giving two different calls the same `useAsyncData` key.
- Putting secrets in `runtimeConfig.public` or a `VITE_` variable.
- Using `v-html` with content that is not sanitized, or binding a `javascript:` URL.
- Watchers that copy state, deep watchers on large objects, and side effects inside `computed`.
- Accessing `window`, `document` or `localStorage` during server rendering instead of in `onMounted` or `<ClientOnly>`, which causes hydration mismatches.
- Mutating props or emitting events that are not declared.
- Running `tsc` instead of `vue-tsc` and believing templates are type checked.

## Definition of done

- The type check with `vue-tsc` or `nuxi typecheck` reports no error with strict mode on, and no `any` or `@ts-ignore` hides one.
- ESLint with `eslint-plugin-vue` and the formatter pass with no new disable comment.
- Components use `<script setup lang="ts">`, typed props, emits and models, and never mutate a prop.
- Reactivity is preserved: no destructured reactive objects or stores, derived values are `computed`, watchers only run side effects.
- Server data comes through `useFetch`, `useAsyncData` or the query library with loading, error and empty states handled.
- No module-level mutable state is shared across requests on the server.
- Server routes validate input with Zod, authorize the caller and return only the fields needed.
- No secret sits in `runtimeConfig.public` or a `VITE_` variable, and no private field reaches the payload.
- No `v-html` renders unsanitized content and no user URL is bound without a scheme check.
- Components use the project's library and tokens, work with the keyboard and screen readers, and avoid hydration mismatches.
- Vitest and Playwright tests cover the change and pass without watch mode, and coverage meets the project threshold.
- The production build succeeds without warnings and was exercised with a preview or the built server.

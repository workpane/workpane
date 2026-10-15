# React engineering

## Recognize the project

- Read `package.json` first: the versions of `react`, `react-dom`, `next`, `vite`, `react-router`, `typescript`, the state, form and test libraries, and the `scripts` block, which holds the real commands for `dev`, `build`, `test`, `lint`, `typecheck` and `format`. Use those scripts instead of calling tools directly.
- Detect the package manager from the lock file: `package-lock.json` for npm, `pnpm-lock.yaml` for pnpm, `yarn.lock` for Yarn, `bun.lock` or `bun.lockb` for Bun. Use only that one, honor the `packageManager` field and the Node version in `.nvmrc`, `.node-version` or `engines`, and never create a second lock file.
- Identify the kind of application. A `next.config.*` with an `app` directory is a Next.js App Router project, a `pages` directory is the Pages Router, a `vite.config.*` with `index.html` is a Vite single page application, and a `react-router.config.*` is React Router in framework mode. Each has different rules below.
- Read `tsconfig.json` and confirm `strict` is on, then `eslint.config.*`, the test and Playwright configurations, the Tailwind setup and `components.json` when shadcn/ui is present.

## Architecture

- Keep the dependency direction one way: routes and pages compose features, features use shared components, hooks and libraries, and shared code never imports from a feature. Business rules live in plain TypeScript modules that know nothing about React, so they are tested without rendering.
- Separate server state from client state. Data that belongs to the server is fetched and cached by the data layer of the framework or TanStack Query, never copied into a global store. Client state stays as local as possible.
- In a Vite single page application, the browser is the only runtime: every secret, every authorization decision and every validation that matters lives in the API, and the client only calls it.
- In Next.js App Router, components are Server Components by default. Fetch data, read secrets and talk to the database in Server Components, Server Functions and route handlers, and add `"use client"` only at the leaves that need state, effects, event handlers or browser APIs. Keep the client boundary low in the tree, and pass Server Components into Client Components as `children` instead of importing them.
- Put server-only modules, such as the database client and data access functions, behind `import "server-only"` so an accidental import from a Client Component fails the build. Mark browser-only modules with `import "client-only"` the same way.
- Centralize data access in a data access layer of functions that authenticate, authorize and return minimal data transfer objects. Pages and Server Functions call that layer instead of querying the database directly.

## Project structure

```
src/
  app/                    # Routes (Next.js) or the router and providers (Vite)
    (marketing)/          # Route group without a URL segment
    orders/
      [id]/page.tsx       # Server Component page that loads one order
      actions.ts          # Server Functions of this route
      loading.tsx         # Streaming fallback
      error.tsx           # Error boundary of the segment
  features/
    orders/
      components/         # Components used only by this feature
      hooks/              # Hooks of this feature
      api.ts              # Queries, mutations and their keys
      schema.ts           # Zod schemas and inferred types
      orders.test.ts      # Tests beside the code they cover
  components/
    ui/                   # Design system primitives, shadcn/ui components
  hooks/                  # Hooks shared by several features
  lib/                    # Framework-agnostic utilities and clients
  server/                 # Data access layer, server-only code
  styles/                 # Global CSS, tokens and theme
e2e/                      # Playwright end-to-end tests
public/                   # Static files served as they are
```

- New code goes into the feature it belongs to. Move it to `components`, `hooks` or `lib` only when a second feature needs it.
- One component per file, named in PascalCase after the component, with hooks named `useSomething` in camelCase files. Follow the file naming the project already uses, such as kebab-case files.
- Never import another feature's internals. Use its public entry or move the shared part out.

## Patterns and practices

- Write function components only. Type props with an explicit `type` or `interface`, never `React.FC` when the project does not use it, and never `any`. Use discriminated unions for variants instead of several optional booleans.
- Follow the rules of hooks: call hooks only at the top level of a component or another hook, never in conditions, loops or after an early return, and keep the `react-hooks` lint rules enabled with exhaustive dependencies. Fix a dependency warning by restructuring, never by disabling the rule.
- Do not use effects to derive data. Compute values during render, transform data where it is fetched, and handle user actions in event handlers. An effect is for synchronizing with something outside React, such as a subscription, a timer or a non-React widget, and it returns its cleanup.
- Never fetch in a bare `useEffect` with `useState`. Use TanStack Query, the loader of the router, or a Server Component, which handle caching, races, cancellation and errors.
- Keep components pure: the same props and state give the same output, render never mutates outside variables, and state is updated immutably.
- When the project enables the React Compiler, do not add `useMemo`, `useCallback` or `memo` by hand. Without it, memoize only what a profiler shows is expensive or what a memoized child needs to stay stable.
- Give every list item a stable `key` from the data, never the index of a list that changes.
- State placement: `useState` or `useReducer` in the component that owns it, lifted only to the closest common parent, context only for values that change rarely and are read widely, such as the theme, the session or the locale. Split contexts so a frequent change does not re-render the whole tree.
- Use Zustand or Redux Toolkit only for client state that many distant components share and change, when the project already has one or the need is real. Select narrow slices to avoid re-renders, and never put server data there.
- Keep state that should survive a reload or be shared by link, such as filters and pagination, in the URL.
- Forms use React Hook Form with `zodResolver` from `@hookform/resolvers/zod`, and the same Zod schema validates on the server. Infer types with `z.infer` instead of declaring them twice. In Next.js, forms can post to a Server Function through the `action` prop with `useActionState` for the result and `useFormStatus` for the pending state.
- Handle errors with error boundaries at route or feature level (`error.tsx` in Next.js, `errorElement` or the route error boundary in React Router) and show a recovery action. Show loading with `Suspense` boundaries, `loading.tsx` or the pending state of the query.
- Documentation comments use TSDoc (`/** ... */`) on exported APIs only where the project already writes them. Comments stay rare.

## Data, networking and persistence

- With TanStack Query, define query keys in one factory per feature, keep `queryFn` thin over a typed API client, and invalidate or update the exact keys a mutation affects. Validate responses from untrusted APIs with Zod at the boundary.
- Set `staleTime` deliberately, and use optimistic updates with a rollback in `onError` only where latency matters.
- In Next.js, the `params`, `searchParams`, `cookies()` and `headers()` APIs are asynchronous and must be awaited. Read the caching model of the installed version before relying on defaults: `fetch` is not cached by default since Next.js 15, and Next.js 16 adds Cache Components with the `"use cache"` directive, `cacheLife` and `cacheTag` when `cacheComponents` is enabled.
- After a mutation, revalidate exactly what changed with `revalidatePath`, `revalidateTag` or `updateTag` as the installed version provides, and never mark a whole route dynamic just to avoid thinking about caching.
- Use route handlers (`route.ts`) for webhooks, public APIs and anything a non-React client calls. Use Server Functions for mutations triggered by the application's own forms and buttons.
- Every Server Function is a public HTTP endpoint that anyone can call with any arguments. Validate its input with Zod, authenticate and authorize inside the function, and return a typed result with field errors instead of throwing for expected failures.
- The middleware of Next.js is the `middleware.ts` file, renamed `proxy.ts` from Next.js 16. Use it for redirects, rewrites, headers and coarse session checks only. Never make it the only authorization check, and repeat the check in the data access layer.
- Keep tokens in `HttpOnly` cookies set by the server. Never store access tokens, refresh tokens or personal data in `localStorage` or `sessionStorage`.

## Interface

- Use the component library the project has: shadcn/ui primitives in `components/ui`, Radix, MUI, Chakra, Mantine or an internal library. Add shadcn/ui components with its CLI (`npx shadcn@latest add <component>`) and adapt them to the tokens instead of writing a new primitive.
- Style the way the project does. With Tailwind, use the theme tokens declared in its configuration, which Tailwind 4 declares in CSS with `@theme`, never arbitrary values such as `bg-[#3b82f6]` or `p-[13px]`. Merge conditional classes with the project's helper, usually `cn` built on `clsx` and `tailwind-merge`. With CSS Modules, use the custom properties of the theme.
- Implement dark mode through the token layer, such as a `dark` class toggling custom properties, without a flash of the wrong theme on load. Use `next/image` and `next/font` in Next.js.
- Prefer native elements: `button` for actions, `a` with `href` (or the router's `Link`) for navigation, `label` bound to every input, `fieldset` and `legend` for groups. Never put `onClick` on a `div`.
- Use the accessible primitives of the library for dialogs, menus, comboboxes, tabs and tooltips, which manage focus, `aria` attributes and keyboard handling. Return focus to the trigger when a dialog closes and announce asynchronous results through a live region.

## Security

- React escapes text in JSX. Never pass user content to `dangerouslySetInnerHTML`. When HTML from outside must be rendered, such as Markdown or rich text, sanitize it with DOMPurify on the side that renders it and keep the allowlist strict.
- Never render a user-provided URL into `href`, `src` or `action` without checking its scheme. A `javascript:` URL executes code, so allow only `http:`, `https:` and `mailto:` or the schemes the feature needs.
- Only variables with the `NEXT_PUBLIC_` prefix in Next.js or `VITE_` in Vite (or the configured `envPrefix`) reach the browser, and they are inlined into the public bundle at build time. Never give a secret one of those prefixes, never import a server environment variable from a Client Component, and treat every value in the bundle as public.
- Props passed from a Server Component to a Client Component are serialized into the page. Pass only the fields the component shows, never whole database records with hashes, tokens or internal fields.
- Protect Server Functions and route handlers as any API: validation, authentication, authorization per object, rate limiting on sensitive operations. Next.js checks the `Origin` of Server Function requests against the host, so set the `allowedOrigins` option of the Server Actions configuration only to exact trusted origins behind a proxy.
- Set a Content Security Policy with nonces in Next.js through the proxy or middleware and `headers` in `next.config.*`. Avoid inline scripts in a single page application, and keep `script-src` free of `unsafe-inline` and `unsafe-eval` in production.
- Keep `react`, `react-dom`, `next` and the React Server Components packages on patched versions. Critical vulnerabilities in Server Components and middleware have been fixed in patch releases, and a lagging patch level is a real exposure.

## Performance

- Measure against Core Web Vitals at the 75th percentile: Largest Contentful Paint within 2.5 seconds, Interaction to Next Paint within 200 milliseconds and Cumulative Layout Shift below 0.1.
- Keep JavaScript off the client in Next.js by leaving components on the server. Split large client-only parts with `next/dynamic` or `React.lazy` and `Suspense`, and lazy load routes in a single page application.
- Stream slow data behind `Suspense` boundaries so the shell renders immediately, and start independent requests in parallel instead of awaiting them one after another.
- Analyze the bundle before and after adding a dependency, with `@next/bundle-analyzer`, the bundle analyzer of the installed Next.js version, or `rollup-plugin-visualizer` for Vite.
- Virtualize long lists with TanStack Virtual or the library the project uses. Use `useTransition` or `useDeferredValue` to keep input responsive during expensive renders.

## Tests

- Unit and component tests run with Vitest or Jest, whichever the project uses, with React Testing Library. Query by role, label and text (`getByRole`, `getByLabelText`), drive interactions with `@testing-library/user-event`, and assert what the user sees, never component internals or state.
- Mock the network at the boundary with Mock Service Worker (`msw`) when the project has it, and give each test a fresh `QueryClient` with retries disabled.
- Test business rules and Zod schemas as plain functions. Test hooks through a component that uses them, or `renderHook` when the hook is the public interface.
- Async Server Components are not supported by Testing Library rendering. Test their data functions directly and cover the page with Playwright.
- End-to-end tests run with Playwright under `e2e` against a production build, using role-based locators and web-first assertions such as `await expect(locator).toBeVisible()`, never fixed waits. Cover the critical journeys, authorization between two users and form validation.
- Add automated accessibility checks with `@axe-core/playwright` or `jest-axe` where the project has them.
- Run the suite once without watch mode, for example `npx vitest run` or `npx jest --ci`. Coverage comes from `npx vitest run --coverage` with `@vitest/coverage-v8` or `npx jest --coverage`, written to `coverage/`. Read the report for the files you changed.

## Tooling and quality gates

- Type check with `tsc --noEmit`, or `tsc -b` for project references, through the project's `typecheck` script. Next.js also type checks during `next build`.
- Lint with ESLint flat config, including `eslint-plugin-react-hooks`, `jsx-a11y` and the TypeScript rules the project enables. Next.js 16 removed `next lint`, so run `eslint .` directly in those projects.
- Format with Prettier, or Biome when the project uses it instead, and check formatting in continuous integration with `prettier --check .`.
- Never add `eslint-disable`, `@ts-ignore` or `@ts-expect-error` to pass a check, and keep `--max-warnings 0` where the project sets it.

## Build, configuration and release

- Build with the project's script, `next build` or `vite build`, and run the production build locally with `next start` or `vite preview` before reporting a performance or behavior result.
- Validate environment variables at startup with a Zod schema in one module, split into server and public variables, so a missing value fails the build rather than a request. Commit a `.env.example` with names and no values.
- A Vite build is static: environment values are baked in at build time, so each environment needs its own build or a runtime configuration fetched from the server.
- Check in the Next.js build output that each route rendered as static or dynamic as intended.
- Deploy with the target the project uses, such as a managed Next.js host, a Node server with `output: "standalone"` in a container, or a static host for a Vite build with a fallback to `index.html` for client routes.

## Pitfalls

- Adding `"use client"` to a page or a layout to make a hook work, which turns the whole subtree into client JavaScript.
- Importing a server module, a secret or the database client into a Client Component, or exposing a secret with a public prefix.
- Treating a Server Function as private because no button calls it, and skipping authorization in it.
- Relying on middleware or the proxy as the only authentication check.
- Fetching in `useEffect` without cancellation, causing race conditions and duplicate requests, or syncing derived state with an effect.
- Copying server data into Zustand, Redux or context and keeping it in sync by hand.
- Passing a new object as a context value on every render.
- Reading `window` during server rendering, or rendering dates and random values differently on server and client, which breaks hydration.

## Definition of done

- The `tsc` check reports no error with `strict` on, and no `any`, `@ts-ignore` or cast hides one.
- ESLint passes with the hooks and accessibility rules and no new disable comment, and Prettier reports no difference.
- Server data flows through TanStack Query, the router loaders or Server Components, and client state lives as low as possible.
- Client boundaries in Next.js sit at the leaves, and server-only code is guarded by `server-only`.
- Every Server Function and route handler validates input with Zod and authorizes the caller on the server.
- No secret carries `NEXT_PUBLIC_` or `VITE_`, and no secret or private field reaches the client bundle or the serialized props.
- No `dangerouslySetInnerHTML` renders unsanitized content and no user URL reaches `href` unchecked.
- Unit, component and Playwright tests cover the change and pass without watch mode, and coverage meets the project threshold.
- The production build with `next build` or `vite build` succeeds without warnings, and the bundle did not grow without a reason you can state.

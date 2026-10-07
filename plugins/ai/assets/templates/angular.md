# Angular engineering

## Recognize the project

- Read `package.json` for the versions of `@angular/core`, `@angular/cli`, `rxjs`, `typescript` and `zone.js`, and for the `scripts`. Read `angular.json` for the projects, the builders (`@angular/build:application`, `@angular-devkit/build-angular:*`, `@angular/build:unit-test`, Karma or Jest), the configurations, `fileReplacements` and the budgets.
- Detect the package manager from the lock file and the `cli.packageManager` entry of `angular.json`, and never create a second lock file. Run commands through `npx ng` or the scripts.
- Read `tsconfig.json` and confirm `strict` and the `angularCompilerOptions` `strictTemplates`, `strictInjectionParameters` and `strictInputAccessModifiers`. Read `src/main.ts` and `src/app/app.config.ts` to learn how the application bootstraps and which providers it registers.
- Learn the generation of the code base before writing: standalone components or `NgModule`, signals or RxJS state, `@if` and `@for` or `*ngIf` and `*ngFor`, `inject()` or constructor injection, zoneless or Zone.js, and the file naming (Angular 20 and newer generate `user-profile.ts` without the `.component` suffix). Follow what the project does, and migrate only when asked, with the official schematics (`ng generate @angular/core:control-flow-migration`, `standalone-migration`, `inject-migration`, `signal-input-migration`).
- Use the Angular CLI for new parts (`ng generate component`, `service`, `guard`, `interceptor`) so files follow the project's schematics defaults.

## Architecture

- Build with standalone components, the default since Angular 19. Bootstrap with `bootstrapApplication` and an `ApplicationConfig` that declares `provideRouter`, `provideHttpClient` and the other providers. Introduce no new `NgModule`.
- Organize by feature: each feature owns its routes, pages, components, services and models, loaded lazily from the root routes. Shared presentational components, directives and pipes live in a shared area and never import a feature.
- Split smart and presentational components: a page or container injects services and holds state, presentational components receive `input()` values and emit `output()` events, and know nothing about HTTP or the store.
- Keep business rules in plain TypeScript functions and classes, and data access in services that wrap `HttpClient`. Components never call `HttpClient` directly.
- Hold state in signals: `signal` for writable state, `computed` for derived state, `linkedSignal` for state that resets from a source. Expose read-only signals from services with `asReadonly()` and mutate them only through service methods. Use NgRx (SignalStore or Store) only when the project already has it.
- Use RxJS for streams of events over time: debounced search, websockets, cancellation and combination of requests. Convert at the edges with `toSignal` and `toObservable`.
- Run without Zone.js when the project is zoneless (`provideZonelessChangeDetection()`, the default for new applications from Angular 21). Then change detection runs only from signals, template events, the `async` pipe and `markForCheck`, so never rely on a `setTimeout` or a third-party callback updating a plain field.

## Project structure

```
src/
  app/
    app.config.ts            # Root providers
    app.routes.ts            # Root routes with lazy features
    app.ts                   # Root component
    core/                    # Singletons: interceptors, guards, auth, error handler
    shared/                  # Reusable components, directives and pipes
      ui/                    # Design system primitives
    features/
      orders/
        orders.routes.ts     # Routes of the feature, loaded lazily
        order-list/          # Page component with its template and styles
        order-detail/
        data/
          order-api.ts       # Service wrapping HttpClient
          order.ts           # Interfaces and types
        order-store.ts       # Signal-based state of the feature
  environments/              # Build-time configuration per environment
  styles/                    # Global styles, tokens and theme
public/                      # Static assets copied as they are
e2e/                         # Playwright or Cypress tests
```

- New code goes into its feature. Promote it to `shared` only when a second feature uses it, and to `core` only when it is an application-wide singleton.
- Keep one component, directive, pipe or service per file, named in kebab-case after its class, with the template and styles beside it in files of the same name when they are more than a few lines.
- Never import from another feature's internals. Expose what other code may use from the feature's routes or a public file.

## Patterns and practices

- Declare inputs with `input()` and `input.required()`, outputs with `output()`, two-way bindings with `model()`, and queries with `viewChild()` and `contentChild()`. Use the decorator forms only in a project that has not migrated.
- Components use `ChangeDetectionStrategy.OnPush`, which is the default from Angular 22 when `changeDetection` is not set. Do not mutate an input object in place, replace it.
- Inject with `inject()` in field initializers. Provide application-wide services with `@Injectable({ providedIn: 'root' })`, feature-scoped services in the `providers` of the feature route, and configuration through typed `InjectionToken` values.
- Write templates with the built-in control flow: `@if`, `@for` with a mandatory `track` expression from the data identity, `@switch`, `@let`, and `@defer` for deferred parts. Never track by `$index` in a list that changes.
- Keep templates free of logic and function calls that compute on every check. Use `computed` signals or pure pipes.
- RxJS discipline: no nested `subscribe`. Compose with `switchMap` for latest-wins requests, `concatMap` for ordered writes, `exhaustMap` for submit buttons and `mergeMap` only for independent parallel work. Prefer the `async` pipe or `toSignal` to manual subscriptions, and end any manual subscription with `takeUntilDestroyed()`. Handle errors with `catchError` inside the inner stream so the outer stream survives.
- Use `effect()` only for side effects outside the signal graph, such as logging, storage or a third-party widget, never to copy one signal into another. Use `computed` or `linkedSignal` for that.
- Load async data into signals with `resource`, `rxResource` or `httpResource` when the installed version provides them and the project uses them, and handle their loading, error and value states.
- Write guards and resolvers as functions (`CanActivateFn`, `CanMatchFn`, `ResolveFn`) using `inject()`, returning a `UrlTree` from `router.createUrlTree` to redirect. Guards protect navigation only, the server still authorizes every request.
- Lazy load features with `loadChildren: () => import('./features/orders/orders.routes')` and single pages with `loadComponent`. Bind route parameters to inputs with `withComponentInputBinding()` when the project enables it.
- Forms are typed reactive forms (`FormGroup`, `FormControl<T>`, `NonNullableFormBuilder`) or signal forms in projects that adopted them. Never use `UntypedFormGroup` in new code. Write validators as functions, show errors after the control is touched, and disable the submit while pending.
- Handle unexpected errors with a custom `ErrorHandler` provided in the application config, and expected HTTP failures in the service or the interceptor with a typed result.
- Localize with `@angular/localize` (`i18n` attributes and `$localize`) or the library the project uses, such as Transloco.
- Documentation comments use TSDoc or JSDoc (`/** ... */`) on public APIs of shared libraries where the project already writes them. Comments stay rare.

## Data, networking and persistence

- Register `provideHttpClient(withInterceptors([...]))` with functional interceptors (`HttpInterceptorFn`). Angular 22 uses the `fetch` backend by default, so `withFetch()` is no longer needed there, and earlier versions need it for server rendering.
- Write interceptors for one concern each: attach credentials to requests for the application's own API only (check the URL, never send a token to a third party), map errors, report progress or retry idempotent requests with a bound.
- Type every request (`http.get<Order>(...)`) and validate responses from untrusted APIs at the boundary, since the generic type is not a runtime check.
- Cancel stale requests with `switchMap` or by letting `httpResource` and `rxResource` restart, and never let two requests race to write the same state.
- With server rendering, enable `provideClientHydration()` with `withHttpTransferCacheOptions` as needed so requests made on the server are not repeated in the browser, and guard browser-only code with `afterNextRender` or `isPlatformBrowser`.
- Keep session tokens in `HttpOnly` cookies set by the server when the architecture allows. Never store tokens in `localStorage`.

## Interface

- Use the component library the project has, such as Angular Material with the Component Dev Kit, PrimeNG or an internal kit, and its theming tokens (Material 3 uses Sass mixins and CSS custom properties).
- Keep component styles encapsulated with the default emulated encapsulation, use design tokens from custom properties, and never hard-code colors or spacing. Use `:host` for the component's own box.
- Use the CDK for accessible behavior: `cdkTrapFocus`, `FocusMonitor`, `LiveAnnouncer`, overlays, `cdk-virtual-scroll-viewport` and the `a11y` utilities. Use native `button` and `a` elements, label every form control, and give icon buttons an `aria-label`.
- Use `NgOptimizedImage` (`ngSrc`) with width and height, `priority` on the largest image of the view, and `@defer (on viewport)` for content below the fold.
- Implement dark mode through the theme tokens with `prefers-color-scheme` or a class on the root.

## Security

- Angular escapes interpolation and sanitizes values bound to `innerHTML`, `href`, `src` and `style` by default. Keep this protection: bind with `[innerHTML]` only content that has been sanitized, and never build templates from user input.
- Never call `DomSanitizer.bypassSecurityTrustHtml`, `bypassSecurityTrustUrl`, `bypassSecurityTrustResourceUrl`, `bypassSecurityTrustScript` or `bypassSecurityTrustStyle` on data that comes from users or external services. When one is required, such as an embed URL from an allowlist, validate the exact value first and keep the call in one reviewed place.
- Never touch the DOM directly with `ElementRef.nativeElement.innerHTML` or `document`, which bypasses sanitization. Use bindings or `Renderer2`.
- Enable Trusted Types and a Content Security Policy where the project serves its own headers, and use `autoCsp` in `angular.json` or nonce support (`ngCspNonce`, `CSP_NONCE`) for inline styles and scripts.
- Use the cross-site request forgery support of `HttpClient`, configured with `withXsrfConfiguration` to match the server's cookie and header names, for cookie-based sessions.
- The `environment.ts` files are compiled into the public bundle. Never put a secret there, and treat every value in them as public.
- Ahead-of-time compilation is the only mode for production. Never ship the JIT compiler or compile templates at runtime.

## Performance

- Keep every component `OnPush` and state in signals, so change detection touches only what changed.
- Lazy load every feature route, defer heavy parts with `@defer`, and keep the initial bundle within the budgets in `angular.json`. Tighten budgets rather than raising them to pass a build.
- Analyze bundles with `ng build --stats-json` and a source map explorer or the analyzer the project uses before adding a dependency.
- Use server rendering, prerendering and incremental hydration (`withIncrementalHydration()`) for public pages that need fast first paint, as the project's `@angular/ssr` setup allows.
- Profile with the Angular DevTools profiler before optimizing, and virtualize long lists.

## Tests

- Use the runner the project configures: Vitest through the `@angular/build:unit-test` builder (the default for new projects from Angular 21), Karma with Jasmine in older projects, or Jest. Never switch runners in a feature change.
- Test components with `TestBed` and standalone imports, or with Angular Testing Library when the project has it. Query by role and label, drive input with real events, and call `fixture.detectChanges()` or `await fixture.whenStable()` before asserting.
- Test services with `TestBed.inject`. Test HTTP with `provideHttpClient()` and `provideHttpClientTesting()`, then `HttpTestingController` to expect each request, flush a response and `verify()` that no request is left.
- Test signal state directly by setting signals and reading `computed` values. Test functional guards and interceptors with `TestBed.runInInjectionContext`.
- Use component harnesses from the CDK for Angular Material components instead of querying their internal DOM.
- End-to-end tests run with Playwright or Cypress, whichever the project uses, against a production build, with role-based locators.
- Run once with `ng test --no-watch`, and collect coverage with `ng test --no-watch --coverage` for the `unit-test` builder or `--code-coverage` for Karma, written to `coverage/`.

## Tooling and quality gates

- Lint with `ng lint` through `angular-eslint` (added with `ng add angular-eslint`) with the template accessibility rules enabled, and format with Prettier or the formatter the project uses.
- Keep `strict` TypeScript and `strictTemplates` on, so template bindings are type checked by the build. Treat the extended diagnostics as errors where the project configures them.
- Never use `any`, the non-null assertion operator to silence a template error, `$any()` in templates, `eslint-disable` or `@ts-ignore` to pass a check.
- Keep dependencies on aligned versions of the framework packages, and upgrade with `ng update @angular/core @angular/cli` one major at a time, in a change of its own, following the update guide.

## Build, configuration and release

- Build with `ng build`, which uses the production configuration by default, and check the budgets and warnings in its output. Serve the result locally, or run the server bundle for `@angular/ssr`, before reporting behavior or performance.
- Configure environments with `fileReplacements` and `src/environments/` (create them with `ng generate environments`), or load runtime configuration from the server with `provideAppInitializer` when one build must serve several environments.
- Deploy a client-only build to a static host with a fallback to `index.html` for deep links, and a server-rendered build to a Node host or the platform the project targets.
- Commit `.env.example` or the documented configuration names, never secrets, and set the base href per deployment with `--base-href` when the application is not served from the root.

## Pitfalls

- Nested `subscribe` calls and subscriptions without `takeUntilDestroyed`, which leak and race.
- Calling functions in templates, or mutating objects in place under `OnPush`, then wondering why the view is stale.
- Updating plain fields from a timer or a third-party callback in a zoneless application.
- Using `effect()` to synchronize signals, creating loops and glitches.
- A `@for` without a meaningful `track`, or tracking by index in a list that changes.
- Bypassing the sanitizer with `bypassSecurityTrust*` on external data, or writing `innerHTML` through `nativeElement`.
- Secrets in `environment.ts`, and guards treated as authorization.
- Adding a new `NgModule` or decorator-based input to a code base that has moved to standalone and signals.
- Sending the authentication header to every URL from an interceptor.
- Raising budgets or disabling `strictTemplates` to make a build pass.

## Definition of done

- The command `ng build` passes with strict TypeScript and `strictTemplates`, within the budgets, with no new warning.
- The command `ng lint` and the formatter pass with no new disable comment, `any` or `$any()`.
- New components are standalone, `OnPush`, use `input()`, `output()` and `inject()`, and the built-in control flow with a real `track`.
- State lives in signals with read-only exposure, and RxJS is used without nested subscriptions or leaks.
- Features are lazy loaded and guards, resolvers and interceptors are functional.
- Forms are typed reactive forms or signal forms with visible validation states.
- No sanitizer bypass touches external data, and no secret sits in an environment file.
- Interceptors send credentials only to the application's own API, and the server authorizes every request.
- Components use the project's library and tokens and pass keyboard and screen reader checks.
- Unit tests with the project's runner and end-to-end tests cover the change, pass without watch mode and meet the coverage threshold.
- The production build was exercised in a browser, with server rendering and hydration working when the project uses them.

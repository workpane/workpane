# Flutter engineering

## Recognize the project

- Read `pubspec.yaml` first: the Dart SDK constraint under `environment`, the Flutter constraint, the dependencies and the dev dependencies. Then read `pubspec.lock`, `analysis_options.yaml`, `l10n.yaml` and `build.yaml` when present.
- Check the Flutter version the project pins in `.fvmrc`, `.fvm/fvm_config.json`, `.tool-versions`, `.flutter-version` or the CI workflow. When a version manager such as FVM is configured, run every command through it (`fvm flutter test`).
- Detect a monorepo by `melos.yaml` or a `workspace` entry in the root `pubspec.yaml`, and run commands in the package that owns the change.
- Identify the state management (`flutter_riverpod`, `hooks_riverpod`, `flutter_bloc`, `provider`), the router (`go_router`, `auto_route`), the HTTP client (`dio`, `http`), code generation (`freezed`, `json_serializable`, `riverpod_generator`, `go_router_builder`) and the lint package (`flutter_lints`, `very_good_analysis`).
- Check which platform folders exist (`android`, `ios`, `web`, `macos`, `windows`, `linux`), the flavors in `android/app/build.gradle.kts` and the iOS schemes, and the entry points such as `lib/main_development.dart`.
- The commands are `flutter pub get`, `flutter analyze`, `dart format`, `flutter test`, `dart run build_runner build --delete-conflicting-outputs` and `flutter run --flavor <name> -t <entry point>`. Look for a `Makefile`, `melos` scripts or CI steps that wrap them.

## Architecture

- Separate the UI layer from the data layer, following the architecture guide of Flutter: views (widgets) render state, view models or controllers (a Riverpod notifier, a Bloc or Cubit, a `ChangeNotifier`) hold the state and logic of a screen, repositories are the source of truth for each type of data, and services wrap one external source such as an HTTP API, a database or a platform plugin.
- Add a domain layer of use cases only when logic is shared across features or too complex for a view model.
- Dependencies point down: widgets know view models, view models know repositories, repositories know services. A service never imports a widget, and domain models never import Flutter.
- Choose state management by what the project already uses. For a new project without one, pick Riverpod for apps that need compile-safe dependency injection, caching of async data and fine-grained rebuilds, Bloc for teams that want explicit events, traceable state transitions and strict separation, and Provider with `ChangeNotifier` only for small apps. Never introduce a second one beside the existing one.

## Project structure

```
lib/
  main_development.dart           # Entry point per flavor, builds the app with its config
  main_production.dart
  app/                            # App widget, router, theme, dependency wiring
  core/                           # Shared models, errors, extensions of the domain, constants
  data/
    services/                     # API clients, database, platform channel wrappers
    repositories/                 # Repositories and their implementations
    models/                       # DTOs with fromJson and toJson
  features/
    orders/
      view/                       # Screens and widgets of the feature
      view_model/                 # Notifiers, Blocs or ChangeNotifiers and their state
      domain/                     # Feature models and use cases when needed
  l10n/                           # ARB files and generated localizations
test/                             # Mirrors lib with unit and widget tests
  goldens/                        # Golden images when the project keeps them apart
integration_test/                 # End-to-end tests on a device
assets/                           # Images, fonts and data declared in pubspec.yaml
```

- New features get a folder under `features` with view, view model and, when needed, domain. Shared widgets go to a design system folder or package only when two features need them.
- One public widget or class per file, named in `snake_case.dart` after the type. Generated files (`*.g.dart`, `*.freezed.dart`) sit beside their source and are committed or ignored as the project already does.

## Patterns and practices

### Dart

- Use sound null safety without `!` on values that can really be null. Check, narrow with patterns or return early instead.
- Model closed sets with `sealed class` hierarchies or enhanced `enum`s and handle them with exhaustive `switch` expressions, so a new case fails to compile until handled.
- Use records for small grouped return values and destructuring patterns (`final (user, token) = await login()`), and classes for anything with a name in the domain.
- Make models immutable: `final` fields, `const` constructors, and `freezed` for value equality, `copyWith` and unions when the project uses it. Freezed classes are declared `sealed` or `abstract` as current versions require.
- Mark every widget constructor `const` when possible and instantiate with `const`, which lets Flutter skip rebuilding the subtree.
- Prefer `final` locals, avoid `dynamic`, and never cast JSON with `as` without validating its shape.

### State

- With Riverpod, define providers with code generation (`@riverpod`) when the project does, use `Notifier` and `AsyncNotifier` for mutable state, `ref.watch` in `build`, `ref.read` in callbacks, `ref.listen` for side effects, and render `AsyncValue` with exhaustive handling of data, loading and error.
- With Bloc, use `Cubit` for simple state and `Bloc` with sealed events for flows, emit immutable states, keep side effects in event handlers, use `BlocBuilder` with `buildWhen` and `BlocListener` for navigation and snackbars, and choose event transformers deliberately for concurrent events.
- With Provider, expose `ChangeNotifier` view models through `ChangeNotifierProvider`, call `notifyListeners` after each change and select narrow values with `context.select`.
- Keep `build` methods pure: no network calls, no state mutation and no object creation that should live in state.
- Split large `build` methods into small widget classes rather than helper methods that return widgets, so Flutter can rebuild them independently.

### Async, navigation and errors

- After every `await` in a widget, check `context.mounted` or `mounted` before using `BuildContext`. The lint `use_build_context_synchronously` flags it.
- Dispose every controller, stream subscription, `AnimationController` and `FocusNode` you create in `dispose`, or let the state management own its lifecycle.
- Use `go_router` with declarative routes, nested `ShellRoute` or `StatefulShellRoute` for persistent navigation bars, `redirect` for authentication gates and typed routes through `go_router_builder` where the project uses it. Navigate with `context.go` for location changes and `context.push` for stacked pages, and parse every path and query parameter as untrusted.
- Represent failures as typed results or domain exceptions mapped in repositories, and show localized messages. Never show `e.toString()` to the user.
- Catch uncaught errors with `FlutterError.onError` and `PlatformDispatcher.instance.onError`, reported to the crash service the project uses.
- Log with `package:logging` or the logger the project uses, never `print` in committed code, and keep personal data out of logs.

### Documentation comments

- Dart uses `///` documentation comments in Markdown, with identifiers referenced as `[name]`, written only on public APIs where the project already does. Comments stay rare.

## Data, networking and persistence

- Use `dio` with a single configured instance (base URL, timeouts, interceptors for authentication, token refresh and logging in debug) or `http` with a wrapping client, whichever the project has. Never create clients inside widgets.
- Generate JSON code with `json_serializable` and `freezed`, decode in the data layer and map DTOs to domain models. Run `dart run build_runner build --delete-conflicting-outputs` after changing annotated classes.
- Parse large JSON payloads off the UI isolate with `compute` or `Isolate.run`.
- Persist with the store the project uses, such as `drift` or `sqflite` for relational data, with versioned migrations and tests, and `shared_preferences` only for small non-sensitive settings.
- Repositories expose `Future` for one-shot calls and `Stream` for observed data, and own caching and retry policy.

## Interface

- Use Material 3 through `ThemeData(colorScheme: ColorScheme.fromSeed(...))` or the theme of the design system, with light and dark themes, and read colors and text styles from `Theme.of(context)`. Add custom tokens with `ThemeExtension` instead of hard-coding values.
- Use Cupertino widgets or adaptive constructors such as `Switch.adaptive` where the product wants platform-specific looks on iOS.
- Build responsive layouts with `LayoutBuilder` and `MediaQuery.sizeOf(context)` breakpoints, and use `SafeArea` for notches and system bars. Read only the `MediaQuery` aspect you need to avoid unnecessary rebuilds.
- Accessibility: give icon buttons a `tooltip` or `semanticLabel`, wrap custom controls in `Semantics` with a label and role, exclude decorative images with `excludeFromSemantics`, keep tap targets at 48 by 48 logical pixels, and check layouts with large text scaling through `MediaQuery.textScalerOf`. Never clamp text scaling to hide a layout bug.
- Localize with `flutter_localizations` and `gen-l10n`: messages in ARB files with ICU plurals and placeholders, accessed as `AppLocalizations.of(context)`, and dates and numbers through `intl`. Never concatenate translated fragments.
- Declare every asset and font in `pubspec.yaml`, with `2.0x` and `3.0x` variants for raster images.

## Security

- Store tokens and secrets only with `flutter_secure_storage`, which uses the Keychain on iOS and Keystore-backed encryption on Android. Never in `shared_preferences`, files or Hive boxes without encryption.
- Never put API secrets in Dart code, assets, `--dart-define` or `--dart-define-from-file` values. They are compiled into the app and extractable. Use them only for non-secret configuration.
- Keep cleartext traffic off in the Android network security configuration and App Transport Security on in iOS. Pin certificates only when the threat model requires it, with a rotation plan.
- Validate everything that arrives through deep links, platform channels and push payloads.
- Platform channels: treat arguments from Dart as untrusted in native code and vice versa, use Pigeon for typed channels when the project does, and return structured errors with `PlatformException` codes.
- Build releases with `--obfuscate --split-debug-info=<folder>` where the project protects its code, and keep the symbol files for crash reports out of the app bundle.
- Apply the native security rules of each platform in the `android` and `ios` folders: exported components, backup rules, usage descriptions and the privacy manifest.

## Performance

- Profile in profile mode (`flutter run --profile`) with DevTools on a real device. Debug mode performance means nothing.
- Use `ListView.builder`, `GridView.builder` or slivers for long or unbounded lists, never a `Column` inside a `SingleChildScrollView` for large data.
- Keep rebuilds narrow: `const` widgets, small widgets, selectors (`ref.watch(provider.select(...))`, `context.select`, `buildWhen`) and `RepaintBoundary` around expensive independent painting.
- Avoid `Opacity` and `saveLayer` effects in animations, prefer `FadeTransition` and `AnimatedOpacity`, and keep clipping to what needs it.
- Size images with `cacheWidth` and `cacheHeight` or `ResizeImage`, and use a cached network image package where the project has one.
- Move heavy work to an isolate with `Isolate.run`.

## Tests

- Unit tests use `package:test` APIs through `flutter_test` for view models, repositories and models. Fake services by implementing their interface, and use `mocktail` or `mockito` only for what you cannot fake.
- Bloc tests use `bloc_test` (`blocTest` with `build`, `act` and `expect`). Riverpod tests use `ProviderContainer` with `overrides`, or `ProviderScope(overrides: ...)` in widget tests.
- Widget tests use `testWidgets`, `pumpWidget` with the real theme and localizations, `pump` and `pumpAndSettle`, and finders by key, type, text or semantics label. Wrap the widget in the same providers and router setup the app uses through a shared test helper.
- Golden tests use `matchesGoldenFile`, are updated only deliberately with `flutter test --update-goldens`, and run on the platform the goldens were made on, since font rendering differs between systems.
- Integration tests live in `integration_test` with `IntegrationTestWidgetsFlutterBinding.ensureInitialized()` and run with `flutter test integration_test -d <device>`.
- Files end in `_test.dart` and mirror the path of the code under `lib`. Test names state the behavior.
- Run coverage with `flutter test --coverage`, which writes `coverage/lcov.info`. Read it with `lcov --summary coverage/lcov.info` or render it with `genhtml coverage/lcov.info -o coverage/html`, and exclude generated files the way the project already does.

## Tooling and quality gates

- Run `flutter analyze` (or `dart analyze` in pure Dart packages) with zero issues. Keep the lint package the project includes in `analysis_options.yaml` and its stricter settings such as `strict-casts`, `strict-inference` and `strict-raw-types`. Never add an `// ignore:` comment to make the analyzer quiet.
- Format with `dart format .` and check with `dart format --output=none --set-exit-if-changed .`. Respect the page width the project configures.
- Run `dart fix --dry-run` to see automated fixes for deprecations when upgrading.
- Check that generated code is current by running `build_runner` before analysis in CI, as the project does.
- Keep `pubspec.lock` committed for apps. Add dependencies with `flutter pub add` and check them with `flutter pub outdated`.

## Build, configuration and release

- Use flavors (`--flavor development`) mapped to Android product flavors and iOS schemes with their own bundle identifiers, app names and icons, plus an entry point or `--dart-define-from-file=config/development.json` for non-secret configuration.
- Set the version in `pubspec.yaml` as `name+build`, such as `1.4.0+42`, and increase the build number for every store upload.
- Build Android with `flutter build appbundle --release --flavor production` and iOS with `flutter build ipa --release --flavor production --export-options-plist=<plist>`, signed with keys and profiles provided by CI secrets. Never commit `key.properties`, keystores or certificates.
- Keep the native projects healthy: run `pod install` through Flutter, keep the minimum iOS and Android versions consistent with the plugins, and commit native changes that plugins require.
- Build web with `flutter build web` and desktop with `flutter build macos`, `windows` or `linux` only when the project ships those platforms.

## Pitfalls

- Using `BuildContext` after an `await` without checking `mounted`.
- Calling APIs, creating controllers or starting streams inside `build`.
- Forgetting to dispose controllers and subscriptions, which leaks memory and calls `setState` after dispose.
- Using `setState` on a large stateful widget so the whole screen rebuilds on every keystroke.
- Nesting unbounded lists in a `Column` without `Expanded` or a fixed height, which throws layout errors at runtime.
- Editing generated files instead of their source and running `build_runner`.
- Mixing state management solutions or putting business logic in widgets.
- Treating `--dart-define` values as secret.
- Hard-coding colors, text styles and strings instead of the theme and ARB files.
- Updating golden files to make a failing test pass without checking that the new image is right.

## Definition of done

- The command `flutter analyze` reports no issues and `dart format --set-exit-if-changed` passes.
- Generated code is regenerated and consistent with its sources.
- New screens follow the layering and the state management of the project, with `const` constructors where possible.
- Every async gap that uses `BuildContext` checks `mounted`, and every controller is disposed.
- Text comes from ARB files, colors and styles from the theme, and screens work in dark mode, with large text scaling and on the smallest and largest supported widths.
- Interactive widgets have semantics labels and 48 pixel targets.
- Secrets live in `flutter_secure_storage`, nothing sensitive is in `--dart-define`, assets or logs.
- Unit, widget and, for critical flows, integration tests pass, golden changes were reviewed, and `coverage/lcov.info` was read.
- The release build for every affected platform and flavor succeeds.
- Native changes for Android and iOS are committed with the Dart change that needs them.

# Android engineering

## Recognize the project

- Read `settings.gradle.kts`, the root `build.gradle.kts`, `gradle/libs.versions.toml`, `gradle.properties` and the `build.gradle.kts` of each module first. Groovy `build.gradle` files and versions written inline mean an older setup, so follow them as they are unless the task is the migration.
- Note the Android Gradle Plugin, Kotlin and Gradle wrapper versions, `compileSdk`, `minSdk`, `targetSdk`, the JVM toolchain and whether the project uses KSP or kapt. Check `gradle/wrapper/gradle-wrapper.properties` for the Gradle version.
- Check for convention plugins in `build-logic` or `buildSrc`. When they exist, shared configuration goes there, not into each module.
- Read `AndroidManifest.xml` of the app and of each module, `res/xml/network_security_config.xml`, `proguard-rules.pro` and the product flavors and build types.
- Detect the interface toolkit: Compose (`buildFeatures { compose = true }` or the `org.jetbrains.kotlin.plugin.compose` plugin) or Views with XML layouts. Do not rewrite Views into Compose unless asked, and use `ComposeView` or `AndroidView` at the boundary when both live together.
- Always run Gradle through the wrapper: `./gradlew assembleDebug`, `./gradlew testDebugUnitTest`, `./gradlew connectedDebugAndroidTest`, `./gradlew lint`, and the ktlint, detekt and coverage tasks the build declares. Find the exact task names with `./gradlew tasks` and in the CI workflow.

## Architecture

- Follow the recommended app architecture of Android: a UI layer, an optional domain layer and a data layer, with dependencies pointing down only.
- UI layer: composables render a single immutable `UiState` and send events up. A `ViewModel` per screen exposes `StateFlow<UiState>`, holds the logic of the screen and survives configuration changes.
- Domain layer: use cases with a single `operator fun invoke`, added only when logic is reused by several view models or is complex enough to deserve its own home. Do not create a use case that only forwards to a repository.
- Data layer: repositories are the single source of truth for each type of data, expose `Flow` for observed data and `suspend` functions for one-shot operations, and coordinate data sources such as a Room DAO and a network service. Data sources are never called from the UI layer.
- Prefer an offline-first flow when the product needs it: the UI observes the database, the repository refreshes the database from the network.
- Modularize larger apps by feature (`:feature:orders`) and by layer of shared code (`:core:data`, `:core:database`, `:core:network`, `:core:designsystem`, `:core:model`). A feature module never depends on another feature module. Navigation between features goes through route types in a shared module or the app module.
- When the project uses MVI, Circuit, Decompose or another established pattern, follow it.

## Project structure

```
app/                              # Application class, MainActivity, top-level navigation, DI wiring
build-logic/convention/           # Convention plugins shared by modules
core/
  model/                          # Plain Kotlin domain models
  data/                           # Repositories and their interfaces
  database/                       # Room database, entities, DAOs, migrations
  datastore/                      # DataStore for preferences
  network/                        # Retrofit or Ktor services, DTOs, interceptors
  designsystem/                   # Theme, tokens, shared composables
  testing/                        # Fakes, test rules, test data
feature/
  orders/
    src/main/kotlin/.../orders/   # OrdersScreen, OrdersViewModel, OrdersUiState, OrdersRoute
    src/test/                     # ViewModel and logic tests on the JVM
    src/androidTest/              # Compose UI tests on device
gradle/libs.versions.toml         # Version catalog
```

- New screens go into their feature module, with the screen, its view model, its state and its route side by side. Shared composables go to the design system only when two features use them.
- Package by feature, never by technical kind across the whole app.
- Keep resources (`strings.xml`, drawables) in the module that uses them.

## Patterns and practices

### State and the UI layer

- Expose state as `StateFlow` built with `stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), initial)` or a private `MutableStateFlow` updated with `update { }`. Never expose a mutable flow.
- Collect it in Compose with `collectAsStateWithLifecycle()`, never `collectAsState()` for lifecycle-bound UI.
- Model screen state as an immutable `data class`, or a `sealed interface` for exclusive states such as loading, content and error. Use immutable collections or annotate stable types so Compose can skip recomposition.
- Handle one-off results such as navigation or a snackbar as state the UI consumes and acknowledges, not as a `SharedFlow` that can drop events while the UI is stopped.
- Split each screen into a stateful `OrdersRoute` that gets the view model and a stateless `OrdersScreen(uiState, onAction)` that previews and tests can call directly.
- Use `SavedStateHandle` for state that must survive process death, such as route arguments and form input.

### Dependency injection

- Use Hilt: `@HiltAndroidApp` on the application, `@AndroidEntryPoint` on activities, `@HiltViewModel` with `@Inject constructor`, `hiltViewModel()` in composables, and `@Module` with `@Binds` for interfaces and `@Provides` for third-party types. Scope only what must be shared, such as `@Singleton` for the database and the HTTP client.
- When the project uses Koin, kotlin-inject or manual injection, follow it.

### Coroutines and Flow

- Launch from `viewModelScope` or `lifecycleScope`, never `GlobalScope`. Use `rememberCoroutineScope` only for event handlers in composables.
- Inject dispatchers instead of hard-coding `Dispatchers.IO`, and make every suspend function main-safe by switching with `withContext` inside the data layer.
- Never catch `CancellationException` without rethrowing it. `runCatching` around suspend calls swallows it, so catch the specific exceptions instead.
- Use `flowOn` for upstream work, `combine` and `flatMapLatest` to derive state, and `distinctUntilChanged` to avoid redundant work.

### Navigation, errors and logging

- Use Navigation Compose with type-safe routes: `@Serializable` objects and data classes as destinations, `composable<OrderDetail>`, `navController.navigate(OrderDetail(id))` and `backStackEntry.toRoute<OrderDetail>()`. Do not build string routes in new code. When the project adopted Navigation 3, follow its back stack model instead.
- Pass only identifiers in routes and load the data in the destination view model.
- Map exceptions to domain errors in the repository and render them as localized messages from `strings.xml`. Never show `exception.message` to the user.
- Log through the logger the project uses, such as Timber, with logging trees planted only in debug builds, and never log tokens or personal data.

### Documentation comments

- Kotlin uses KDoc (`/** ... */` with `@param` and `@return`), written only on public APIs where the project already writes it. Comments stay rare.

## Data, networking and persistence

- Use Room through KSP with `suspend` DAO functions and `Flow` queries. Every schema change bumps the version with an `AutoMigration` or a written `Migration`, `exportSchema = true` keeps the schema JSON under version control, and `MigrationTestHelper` tests the upgrade. Never use `fallbackToDestructiveMigration` on user data.
- Keep entities, DTOs and domain models separate and map between them at the data layer.
- Use DataStore (Preferences or Proto) for key-value settings instead of `SharedPreferences`, and expose it as a `Flow`. Keep one `DataStore` instance per file.
- Use Retrofit with OkHttp and kotlinx.serialization, or Ktor client, whichever the project has. Configure connect, read and call timeouts, add authentication in an interceptor or authenticator, and refresh tokens in an OkHttp `Authenticator` that serializes concurrent refreshes.
- Add the logging interceptor only in debug builds and redact the `Authorization` header.
- Use WorkManager for deferrable work that must survive process death, with constraints and unique work names. Never use a plain `Service` for that.

## Interface

- Build with Material 3 through the Compose BOM: `MaterialTheme` with a `ColorScheme`, typography and shapes from the design system, dynamic color where the product allows it, and light and dark schemes. Never use literal colors or `sp` and `dp` values outside the design system tokens.
- Go edge to edge, which is enforced from target SDK 35: call `enableEdgeToEdge()` and apply `WindowInsets` padding through `Scaffold` or the inset modifiers instead of fixed margins.
- Support predictive back with `BackHandler` or `PredictiveBackHandler` and avoid intercepting back without reason.
- Build adaptive layouts with window size classes and the Material 3 adaptive components (`NavigationSuiteScaffold`, `ListDetailPaneScaffold`) for tablets, foldables and desktop windows. Do not lock orientation.
- Accessibility: give icons a `contentDescription` or `null` when decorative, merge related nodes with `Modifier.semantics(mergeDescendants = true)`, use `Modifier.clickable` with a `role` and `onClickLabel`, keep touch targets at 48 dp, and use `sp` for text so font scaling works up to 200 percent.
- Put every string in `strings.xml`, plurals in `<plurals>` read with `pluralStringResource`, and format with placeholders, never by concatenation. Support right-to-left with `start` and `end`.
- Provide `@Preview` composables for each state, with light, dark and large font variants where the project uses them.
- Hoist state, pass lambdas down, give every composable a `modifier: Modifier = Modifier` parameter applied to its root, and use `remember`, `derivedStateOf` and `key` correctly instead of recomputing in composition.

## Security

- Declare `android:exported` explicitly on every activity, service and receiver, keep it `false` unless another app must start the component, and protect exported ones with permissions with `android:protectionLevel="signature"` when only your apps call them. Validate every extra of an incoming `Intent` as untrusted.
- Use `PendingIntent.FLAG_IMMUTABLE` unless mutation is required, and explicit intents for internal components.
- Keep cleartext traffic off with a `network_security_config.xml` that sets `cleartextTrafficPermitted="false"`, allows user-added certificate authorities only in `<debug-overrides>`, and pins keys with `<pin-set>` and an expiration only when the threat model needs pinning.
- Generate keys in the Android Keystore with `KeyGenParameterSpec`, prefer StrongBox when available, and bind sensitive keys to user authentication with `setUserAuthenticationParameters`. Use `BiometricPrompt` with a `CryptoObject` so authentication unlocks a key, not a boolean.
- Encrypt sensitive values with a Keystore-held key, for example through Tink, before storing them in DataStore or files. Do not add `EncryptedSharedPreferences` to new code, since its library is deprecated.
- Set `android:allowBackup` and `dataExtractionRules` to exclude tokens and sensitive databases from cloud backup and device transfer.
- WebView: keep JavaScript off unless needed, never add `addJavascriptInterface` for untrusted content, load local content with `WebViewAssetLoader`, and leave file access off.
- Use `FLAG_SECURE` on screens that show secrets. Verify App Links with `assetlinks.json` and `autoVerify`.
- Never ship API secrets in `BuildConfig`, resources or native code. They are extractable.
- Release builds are never `debuggable`, and R8 is enabled.

## Performance

- Ship a Baseline Profile generated by a Macrobenchmark module, and measure startup and jank with Macrobenchmark and the Android Studio profilers on a release build, never a debug build.
- Keep composition cheap: read state as late as possible, use lambda-based modifiers such as `Modifier.offset { }` for values that change every frame, provide stable `key`s in `LazyColumn` items and set `contentType` for mixed lists.
- Inspect recomposition counts with the Layout Inspector before optimizing, and keep the strong skipping mode of the Compose compiler working by passing stable types.
- Do no disk or network work on the main thread. StrictMode in debug builds catches it.
- Load images with Coil or the loader the project uses, sized to their container.
- Watch app size with the APK Analyzer and keep resource shrinking on.

## Tests

- Local tests in `src/test` use JUnit 4 or 5 as the project does, `kotlinx-coroutines-test` with `runTest`, a `MainDispatcherRule` that sets `Dispatchers.setMain(StandardTestDispatcher())`, and Turbine (`flow.test { awaitItem() }`) for flows.
- Test view models against fake repositories that implement the same interface, kept in `core:testing`. Prefer fakes over Mockito or MockK mocks for your own types, and use mocks only for what you cannot fake.
- Test DAOs with an in-memory Room database and test migrations with `MigrationTestHelper`.
- Test composables with `createComposeRule()` or `createAndroidComposeRule<Activity>()`, finding nodes by semantics or `testTag`, never by position. Run them on device in `src/androidTest`, or on the JVM with Robolectric where the project configures it.
- Use Robolectric for Android framework behavior in local tests, and screenshot testing (Roborazzi, Paparazzi or the Compose Preview Screenshot Testing plugin) where the project has it.
- Name tests by behavior, such as `placeOrder_withEmptyCart_emitsError`.
- Measure coverage with the tool the build applies: Kover with `./gradlew koverHtmlReport` and `./gradlew koverVerify`, or JaCoCo through `enableUnitTestCoverage = true` and `./gradlew createDebugUnitTestCoverageReport`, plus `createDebugCoverageReport` for device tests. Read the report in `build/reports`.

## Tooling and quality gates

- Format and lint Kotlin with ktlint (`./gradlew ktlintCheck`, or the Spotless task `./gradlew spotlessCheck`) as configured, with Compose rules when the project adds them, and run detekt (`./gradlew detekt`) with its baseline untouched unless the task is the cleanup.
- Run Android Lint with `./gradlew lint` and keep `warningsAsErrors` and `abortOnError` as configured. Never add a finding to `lint-baseline.xml` to make a build pass.
- Keep Kotlin warnings at zero, and set `allWarningsAsErrors` where the project does.
- Add dependencies only through `libs.versions.toml`, reference them as `libs.*` aliases, and use the Compose BOM instead of versions per Compose artifact.

## Build, configuration and release

- Use build types for debug and release and product flavors for environments or brands, with `applicationIdSuffix` for side-by-side installs. Read environment values through `buildConfigField` or resources set per flavor, never hard-coded in code.
- Enable R8 in release with `isMinifyEnabled = true` and `isShrinkResources = true`. Keep rules narrow in `proguard-rules.pro`, and test the release build, since reflection, serialization and JNI break only there. Upload the mapping file to the crash reporter.
- Set `versionCode` and `versionName` in one place, usually the version catalog or the CI, and increase `versionCode` for every upload.
- Target the API level Google Play currently requires and handle each new behavior change of that level when raising `targetSdk`.
- Ship Android App Bundles with `./gradlew bundleRelease`. Use Play App Signing with an upload key kept out of the repository. CI reads the keystore and its passwords from secrets, and `signingConfigs` read them from environment variables or `gradle.properties` outside version control.
- Publish through the Play Console tracks, internal first, with the tool the project uses, such as Gradle Play Publisher or fastlane supply.

## Pitfalls

- Collecting flows in composables without lifecycle awareness, or launching coroutines in composition instead of `LaunchedEffect`.
- Passing a `ViewModel`, a `NavController` or a `Context` deep into composables instead of state and lambdas.
- Holding an `Activity` or `View` context in a view model or a singleton, which leaks it.
- Forgetting process death: state that lives only in memory vanishes when the system kills the app in the background.
- Swallowing `CancellationException`, or using `GlobalScope` and `runBlocking` in app code.
- Adding a Room column without a migration, or using destructive migration on user data.
- Exported components without validation, mutable pending intents and cleartext traffic left on for a convenience.
- Testing only the debug build, so R8 and resource shrinking failures appear in production.
- Using hard-coded strings, colors and dimensions instead of resources and theme roles.

## Definition of done

- The command `./gradlew` builds every variant the change affects with zero Kotlin and lint warnings.
- The release build runs with R8 enabled and the new code survives minification.
- New screens use a `ViewModel` exposing `StateFlow` of an immutable `UiState`, collected with `collectAsStateWithLifecycle`.
- Strings, plurals, colors and dimensions come from resources and the Material 3 theme, and the screen works in dark mode, at 200 percent font scale, edge to edge and on large screens.
- Every interactive element has semantics and a 48 dp target.
- Room changes ship with a migration and its test, and the exported schema is committed.
- No secret is stored in plain preferences, logs or the APK, every component declares `android:exported`, and cleartext stays off.
- Unit tests with fakes, Turbine and `runTest` cover the logic, Compose UI tests cover the critical interactions, and the Kover or JaCoCo report was read.
- ktlint, detekt and Android Lint pass without new baseline entries.
- Dependencies are declared in the version catalog.

# iOS engineering

## Recognize the project

- Look for `*.xcodeproj`, `*.xcworkspace`, `Package.swift`, `project.yml` (XcodeGen), `Project.swift` (Tuist), `Podfile` and `Cartfile`. A workspace beside a `Podfile` means CocoaPods owns part of the build, so open and build the workspace, never the bare project.
- Read the deployment target, the Swift language mode and the build settings first: `IPHONEOS_DEPLOYMENT_TARGET`, `SWIFT_VERSION`, `SWIFT_STRICT_CONCURRENCY`, `SWIFT_DEFAULT_ACTOR_ISOLATION`, `SWIFT_TREAT_WARNINGS_AS_ERRORS`, and any `.xcconfig` files the targets reference. In packages, read `swift-tools-version` and `swiftSettings`.
- Read `Info.plist`, the `.entitlements` files, `PrivacyInfo.xcprivacy`, the asset catalogs and `Localizable.xcstrings` before touching features that use capabilities, networking, storage or text.
- List the schemes and destinations with `xcodebuild -list` and `xcrun simctl list devices available`. Never guess a scheme name or a simulator model.
- Find the real commands in the `Makefile`, `fastlane/Fastfile`, `ci_scripts` (Xcode Cloud), `.github/workflows` or scripts folder. A typical build and test is `xcodebuild test -scheme App -destination 'platform=iOS Simulator,name=<an available device>'`, and a package is `swift build` and `swift test`.
- Check which test framework each target uses: `import XCTest` or `import Testing`. Write new tests in the framework the target already uses.

## Architecture

- Default to SwiftUI views over observable models: a view is a function of state, a model annotated `@Observable` and isolated to `@MainActor` owns the state and the actions of one screen or feature, and services below it do the work.
- Keep three layers with one dependency direction: presentation (views and their models), domain (value types, rules and protocols that describe what the feature needs) and data (network clients, persistence, Keychain, system frameworks). The domain imports neither SwiftUI nor the data frameworks.
- Split large apps into local Swift packages, one per feature plus shared `Core`, `DesignSystem` and `Networking` packages. Package boundaries make the dependency direction a compile error instead of a convention.
- When the project uses The Composable Architecture, VIPER, MVVM with Combine or coordinators, follow it. Do not introduce a second architecture beside it.
- Inject dependencies through initializers, or through `@Environment` with custom `EnvironmentValues` entries for things views need. Avoid new singletons, and wrap the existing ones behind a protocol only where a test needs to replace them.

## Project structure

```
App/
  App.xcodeproj                 # Thin app target, signing and capabilities
  App/
    AppMain.swift               # @main App, scene and dependency wiring
    Info.plist
    PrivacyInfo.xcprivacy       # Required reason APIs and collected data
    Resources/                  # Asset catalogs, Localizable.xcstrings
Packages/
  Core/                         # Domain models, errors, protocols, utilities of the domain
  Networking/                   # HTTP client, endpoints, DTOs and decoding
  Persistence/                  # SwiftData or Core Data stack and repositories
  DesignSystem/                 # Colors, typography, spacing, reusable views
  Features/
    Orders/
      Sources/Orders/           # Views, observable models, feature navigation
      Tests/OrdersTests/        # Tests of the models and rules of the feature
fastlane/                       # Lanes for build, test, beta and release when used
.swiftlint.yml                  # Lint rules
.swift-format                   # Formatter configuration when swift-format is used
```

- New feature code goes into its feature package or folder, never into the app target. The app target only composes features and owns entitlements, resources and the entry point.
- One main type per file, named after the file. Extensions that add protocol conformance may live in the same file or in `Type+Protocol.swift` as the project already does.
- Reusable views go to the design system only when a second feature needs them.

## Patterns and practices

### Concurrency

- Write code that compiles cleanly in the Swift 6 language mode with complete concurrency checking. Fix data races at the type: make values `Sendable`, isolate mutable state to an actor or to `@MainActor`, and never silence a diagnostic with `@unchecked Sendable`, `nonisolated(unsafe)` or `@preconcurrency` unless you can state why it is safe in one comment.
- Use `async`/`await` and structured concurrency: `async let` and task groups for parallel work, `.task` and `.task(id:)` in SwiftUI so work is cancelled with the view. Avoid unstructured `Task { }` except at a real boundary, and store and cancel it when it can outlive its owner.
- Check `Task.isCancelled` or call `try Task.checkCancellation()` in long loops, and treat `CancellationError` as a normal outcome, never as an error shown to the user.
- Use an `actor` for shared mutable state accessed from several tasks, such as a cache or a token refresher. Remember that actors are reentrant: state read before an `await` may have changed after it.
- Do not mix Combine, completion handlers, `DispatchQueue` and async code in new work. Bridge old callback APIs with `withCheckedThrowingContinuation` and resume exactly once.
- Never block the main actor with synchronous file, network, database or image work.

### State in SwiftUI

- Use `@Observable` models with `@State` in the view that owns them, `@Bindable` to bind to their properties, and `@Environment` for shared models. Do not use `ObservableObject`, `@Published` and `@StateObject` in new code unless the deployment target is below iOS 17.
- Keep views small and their `body` cheap and free of side effects. Derive values instead of duplicating state, and keep state at the lowest view that needs it.
- Model screen state as an `enum` with associated values, such as loading, loaded and failed, rather than several booleans that can contradict each other.
- Use `NavigationStack` with a typed path and `navigationDestination(for:)`, and `NavigationSplitView` for regular widths. Do not use `NavigationView`.

### Errors, logging and configuration

- Model errors as typed `enum`s conforming to `Error`, map transport and decoding errors to domain errors at the data layer, and show localized messages in the view. Never `try!` or force unwrap outside tests and compile-time-known resources.
- Log with `Logger` from `os` with a subsystem and a category per area. Mark interpolated values containing personal data `privacy: .private`, which is the default for dynamic strings, and never log tokens.
- Keep environment values such as base URLs in `.xcconfig` files per configuration, exposed through `Info.plist` keys. Secrets do not belong in the app at all.

### Documentation comments

- Swift uses `///` documentation comments with Markdown and `- Parameter`, `- Returns`, `- Throws` fields. Write them only on public APIs of packages where the project already does, and keep comments rare.

## Data, networking and persistence

- Use `URLSession` with `async` methods and `Codable`. Decode into DTOs at the edge and map them to domain models, so a server rename does not ripple through views. Configure `JSONDecoder` date and key strategies once in the client.
- Set timeouts on the session configuration, check the `HTTPURLResponse` status code explicitly and handle cancellation, offline and server errors as distinct cases.
- Use SwiftData (`@Model`, `ModelContainer`, `ModelContext`, `@Query`) for new local storage when the target allows it, and Core Data when the project already uses it. Never mix the two over the same store without a deliberate plan.
- A `ModelContext` and managed objects are not `Sendable`. Pass identifiers such as `PersistentIdentifier` or `NSManagedObjectID` across actors, do background work in a `@ModelActor` or a background context, and save on the context that made the change.
- Write a migration for every schema change: a `VersionedSchema` with a `SchemaMigrationPlan` in SwiftData, or a new model version with a mapping in Core Data. Test it against a store created by the previous version.
- Use `UserDefaults` or `@AppStorage` only for small non-sensitive preferences. Store files in Application Support or Caches, never in Documents unless the user should see them in Files.

## Interface

- Follow the Human Interface Guidelines: standard navigation, system controls, SF Symbols, system materials and the standard spacing of lists and forms. Prefer system components over custom ones.
- Use semantic colors and named colors from the asset catalog with dark and high contrast variants, and the text styles of Dynamic Type (`.font(.body)`, `.headline`). Use `@ScaledMetric` for custom sizes that should grow with text.
- Test every screen at the largest accessibility text sizes. Switch from horizontal to vertical layout with `ViewThatFits` or the `dynamicTypeSize` environment value instead of truncating.
- Give every control an accessible name with `accessibilityLabel` when its content does not say it, add `accessibilityHint` and `accessibilityValue` where they help, combine related elements with `accessibilityElement(children: .combine)` and hide decorative images with `accessibilityHidden(true)` or `Image(decorative:)`.
- Respect `accessibilityReduceMotion`, `accessibilityReduceTransparency` and Bold Text. Touch targets are at least 44 by 44 points.
- Support iPad properly when the target includes it: size classes, `NavigationSplitView`, multitasking windows of any size, pointer and keyboard shortcuts through `.keyboardShortcut`, and no layout that assumes a portrait phone.
- Localize with String Catalogs. `Text("Key")` and `String(localized:)` are extracted automatically, use the plural and device variations of the catalog instead of building strings in code, and format numbers, dates and measurements with `FormatStyle` APIs.
- Use UIKit through `UIViewRepresentable` or `UIViewControllerRepresentable` only for what SwiftUI lacks, with a `Coordinator` for delegates, and keep the bridge thin.
- Provide `#Preview` blocks with fake data for every screen state the project previews.

## Security

- Store tokens, passwords and keys only in the Keychain through `SecItemAdd`, `SecItemCopyMatching` and `SecItemUpdate`, with the narrowest accessibility that works, usually `kSecAttrAccessibleWhenUnlockedThisDeviceOnly` or `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly` for background work. Never in `UserDefaults`, plists, files or Core Data.
- Protect a local secret with biometrics by binding it to the Keychain with `SecAccessControlCreateWithFlags` and `.biometryCurrentSet`, not by checking a boolean result of `LAContext.evaluatePolicy`, which can be bypassed on a compromised device.
- Use CryptoKit for hashing, signing and authenticated encryption, and the Secure Enclave for keys that must not leave the device.
- Keep App Transport Security on. Do not add `NSAllowsArbitraryLoads`. A domain exception in `NSExceptionDomains` needs a written reason. Add certificate or public key pinning only through `NSPinnedDomains` or a `URLSessionDelegate` when the threat model requires it, with a backup key and a rotation plan.
- Set the data protection class of sensitive files with `.completeFileProtection` when writing them.
- Validate every universal link, custom URL scheme and `onOpenURL` input as untrusted, and never perform an action from a link without the confirmation the action deserves.
- Keep `PrivacyInfo.xcprivacy` true: declare every required reason API the app and its packages use, such as `UserDefaults`, file timestamps and system boot time, with an approved reason, and every type of collected data. Add a purpose string (`NSCameraUsageDescription` and its peers) for every protected resource and request permission only at the moment of use.
- Hide sensitive screens in the app switcher snapshot when the product demands it, and exclude sensitive files from backups with `isExcludedFromBackup` when they can be recreated.
- Never ship API secrets in the binary. They can be extracted. Put them behind the server.

## Performance

- Profile with Instruments (Time Profiler, SwiftUI, Allocations, Leaks, Hangs) on a real device in the Release configuration before optimizing.
- Keep `body` cheap: no formatting with new formatters, sorting, filtering or decoding inside it. Precompute in the model.
- Use `List` or `LazyVStack` and `LazyVGrid` for long collections, with stable `id`s, never `ForEach` over indices of mutable arrays.
- Downsample large images with ImageIO before display, cache them, and load them with `AsyncImage` or the image pipeline the project uses.
- Avoid retain cycles in closures stored by long-lived objects with `[weak self]`, and confirm with the memory graph debugger that a dismissed screen deallocates.
- Watch launch time: defer work out of `App.init` and the first scene, and avoid synchronous work in `application(_:didFinishLaunchingWithOptions:)`.

## Tests

- Use Swift Testing for new tests where the project adopted it: `@Test`, `@Suite`, `#expect`, `#require`, parameterized `@Test(arguments:)` and traits. Use XCTest for UI tests with `XCUIApplication` and for performance tests, which Swift Testing does not cover.
- Test observable models and domain logic directly. Inject protocol-based fakes for networking, persistence and clocks, and use an in-memory `ModelConfiguration(isStoredInMemoryOnly: true)` for SwiftData tests.
- Stub HTTP with a custom `URLProtocol` registered on an ephemeral `URLSessionConfiguration`, never by hitting real servers.
- Mark tests of `@MainActor` code `@MainActor` and await async APIs instead of using expectations with sleeps.
- Find UI elements by `accessibilityIdentifier`, never by visible text that changes with localization.
- Put tests in a test target per module, mirroring the source folders, with names that state the behavior, such as `placingAnOrderWithAnEmptyCartFails`.
- Run coverage with `xcodebuild test -scheme App -destination '<destination>' -enableCodeCoverage YES -resultBundlePath build/Tests.xcresult`, then read it with `xcrun xccov view --report build/Tests.xcresult`. For a package, run `swift test --enable-code-coverage` and read the JSON path printed by `swift test --show-codecov-path`.

## Tooling and quality gates

- Format with the formatter the project configures: swift-format (`swift format lint --strict --recursive Sources`, `swift format --in-place --recursive Sources`) through the toolchain, or SwiftFormat (`swiftformat --lint .`). Never mix the two.
- Lint with SwiftLint (`swiftlint lint --strict`) when `.swiftlint.yml` exists, and fix the violation instead of adding a `swiftlint:disable` comment.
- Keep the build free of warnings and treat them as errors where the project sets `SWIFT_TREAT_WARNINGS_AS_ERRORS`. Run the Xcode static analyzer with `xcodebuild analyze` for Objective-C or C code.
- Resolve packages with `xcodebuild -resolvePackageDependencies` and commit `Package.resolved` so builds are reproducible.

## Build, configuration and release

- Use build configurations and `.xcconfig` files for environments, such as Debug, Staging and Release, each with its own bundle identifier suffix, display name and base URL. Avoid `#if DEBUG` scattered through features.
- Keep the version in `MARKETING_VERSION` and the build number in `CURRENT_PROJECT_VERSION`, incremented for every upload. Do not edit them by hand in `Info.plist` when the build settings own them.
- Use automatic signing for development and explicit signing with the team, certificates and provisioning profiles in continuous integration, provided by fastlane match, Xcode Cloud or the keychain of the runner. Never commit certificates, profiles or `.p8` keys.
- Archive with `xcodebuild archive -scheme App -configuration Release -archivePath build/App.xcarchive` and export with `xcodebuild -exportArchive -exportOptionsPlist ExportOptions.plist`. Upload to TestFlight with an App Store Connect API key through the tool the project uses.
- Keep entitlements minimal and matched to the capabilities enabled in the developer account. A capability added in code without its entitlement fails only on device.
- Fill in App Privacy details, export compliance (`ITSAppUsesNonExemptEncryption`) and the required usage strings before submission.

## Pitfalls

- Building the `.xcodeproj` when a `.xcworkspace` exists, or a scheme that is not shared, so continuous integration cannot see it.
- Editing `project.pbxproj` by hand carelessly. Prefer XcodeGen or Tuist when the project uses them, and keep file references and target membership consistent when you add files.
- Updating UI from a background task, or marking everything `@MainActor` and `nonisolated` at random until the compiler is quiet.
- Starting work in `onAppear` with an unstructured `Task` that is never cancelled, instead of `.task`.
- Holding a `ModelContext` or a managed object across actors.
- Using `@State` for a model passed in from a parent, which ignores later values, or creating a model inside `body`.
- Hard-coding strings, colors and font sizes, which breaks localization, dark mode and Dynamic Type.
- Forgetting the privacy manifest entry for a new required reason API, which App Store Connect rejects at upload.
- Assuming a simulator result holds on device for Keychain, push, background modes, camera and performance.

## Definition of done

- The project builds for every scheme it ships with zero warnings in the Swift language mode it declares.
- No new concurrency diagnostic is silenced, and no UI work runs off the main actor.
- New screens use the design system, semantic colors and Dynamic Type, and work at the largest accessibility size, in dark mode and in landscape and iPad widths when supported.
- Every control has an accessible name and VoiceOver reads the screen in a sensible order.
- Every user-facing string is in the String Catalog with its plural variations.
- Secrets live in the Keychain with the narrowest accessibility, ATS is unchanged or its exception is justified, and nothing sensitive is logged.
- The file `PrivacyInfo.xcprivacy`, usage descriptions and entitlements match what the change uses.
- Schema changes ship with a tested migration.
- Unit tests cover the models and rules, UI tests cover the critical flow when the project has them, and the coverage report from `xccov` was read.
- SwiftLint and the formatter pass in strict mode.
- The file `Package.resolved` and the project file are consistent and committed together with the change.

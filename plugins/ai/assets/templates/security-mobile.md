# Mobile application security review

You review the security of the iOS or Android application in the working directory, native or cross-platform, against OWASP MASVS v2 and with the test methods of the OWASP MASTG. The review is defensive and limited to this code and the builds it produces. You read sources, manifests, build files and the packaged artifacts the project builds, and you confirm behavior on an emulator, a simulator or a test device the reader controls.

## Rules of engagement

- Never attack a backend in production, a store listing or a third-party SDK service. A server URL found in the app is not a target.
- Never extract or print a secret's value. Report its location and type, and redact the value.
- Dynamic instrumentation, such as Frida or objection, runs only on a build and device the reader owns and only when the task asks for it.
- Do not install tools system-wide unasked. Prefer what the project and the machine already have, and say which tools you ran.

## 1. Recognize the app and its surface

- Platforms and stacks: `*.xcodeproj`, `Package.swift`, `Podfile` and `Info.plist` for iOS, `build.gradle(.kts)`, `AndroidManifest.xml` and `settings.gradle(.kts)` for Android, `pubspec.yaml` for Flutter, `package.json` with `react-native` or `expo` for React Native, `.csproj` with MAUI, and Capacitor or Cordova configuration for hybrid apps.
- Build variants: flavors, schemes and configurations, and which one ships. A debug-only weakness matters only if it can reach a release build, so read how release differs.
- Read first: the Android manifest after merge (`./gradlew :app:processReleaseManifest` writes it under `build/intermediates`), `Info.plist`, the `.entitlements` files, `network_security_config.xml`, `PrivacyInfo.xcprivacy`, ProGuard or R8 rules, and the dependency list.
- Map the surface: every exported component, deep link, universal or app link, custom URL scheme, WebView, JavaScript bridge, push payload handler, share extension, widget, file provider, clipboard use and every place the app stores or sends data.
- Static tools when available: Android lint with security checks, MobSF or `mobsfscan` for source, `semgrep` rules for Kotlin, Java and Swift, `apkanalyzer` and `jadx` for a built APK, `plutil -p` and `codesign -d --entitlements -` for an iOS build. Read every result in code before it becomes a finding.

## 2. Examine by MASVS category

### Storage (MASVS-STORAGE)

- Sensitive data at rest, such as tokens, keys, personal data and health or financial data, lives in the Keychain on iOS and in files encrypted with a key from the Android Keystore. `UserDefaults`, `SharedPreferences`, `NSUserDefaults`, plists, SQLite, Realm, Core Data, Room, `AsyncStorage` and plain files are findings when they hold secrets unencrypted.
- iOS Keychain items use the narrowest accessibility, such as `kSecAttrAccessibleWhenUnlockedThisDeviceOnly` or `kSecAttrAccessibleWhenPasscodeSetThisDeviceOnly`, never `kSecAttrAccessibleAlways`. Files with sensitive data use `NSFileProtectionComplete`.
- On Android the Jetpack Security crypto library is deprecated, so do not add `EncryptedSharedPreferences` to new code. Use Keystore-backed keys through Tink or the platform `Cipher` with DataStore or files, and follow what the project already uses when it is sound.
- Backups: `android:allowBackup`, `android:fullBackupContent` and, from Android 12, `android:dataExtractionRules` either disable backup or exclude sensitive files. On iOS, exclude sensitive files with `isExcludedFromBackup` and keep secrets in `ThisDeviceOnly` Keychain items.
- Logs: search for `Log.d`, `Log.v`, `println`, `print(`, `NSLog`, `os_log` with `%{public}`, `console.log`, `debugPrint` and `Timber` trees planted in release, carrying tokens, requests or personal data. Release builds strip verbose logs through R8 rules or build flags.
- Screenshots and the app switcher: screens with sensitive data set `FLAG_SECURE` on Android, and on iOS cover the window when the scene resigns active so the snapshot shows nothing private.
- Clipboard: sensitive values are not copied, or are copied with `UIPasteboard` options `localOnly` and an expiration, and with `ClipDescription.EXTRA_IS_SENSITIVE` on Android 13 and later. Fields for passwords and codes disable autocorrect and keyboard learning.
- Caches: `URLCache`, WebView caches, image caches and keyboard caches that keep responses with personal data.

### Cryptography (MASVS-CRYPTO)

- No keys, salts or IVs hardcoded in code, resources, `BuildConfig`, `Info.plist`, `.xcconfig` or bundled JSON. A key shipped in the app is public.
- Keys are generated in the Android Keystore or the Secure Enclave and Keychain where the platform allows, with purposes and user authentication requirements set at creation.
- Algorithms and modes: AES-GCM or ChaCha20-Poly1305, unique nonces, no ECB, no CBC without authentication, no `SecureRandom` seeded with a constant, no `Random` or `arc4random` misuse for tokens. CryptoKit and Tink are preferred over hand-assembled primitives.

### Authentication and session (MASVS-AUTH)

- Authentication and authorization happen on the server. A local check such as a boolean `isLoggedIn` or a PIN compared on the device guards nothing on its own.
- Logout and password change invalidate the session and refresh token on the server, not only on the device.
- Access tokens are short lived, refresh tokens are stored in the secure store, rotated on use and revoked on logout, and refresh races do not leak or duplicate tokens.
- Biometrics: on iOS, `LAContext.evaluatePolicy` returning `true` is a boolean that instrumentation can flip, so a secret released by biometrics must be a Keychain item protected by `SecAccessControlCreateWithFlags` with `.biometryCurrentSet` or `.userPresence`. On Android, `BiometricPrompt.authenticate` must receive a `CryptoObject` whose Keystore key requires user authentication and is invalidated by new enrolment, and the unlocked cipher must be used for the protected operation.
- OAuth in apps uses the system browser through `ASWebAuthenticationSession` or Custom Tabs with PKCE, never an embedded WebView that can read credentials, and never a client secret bundled in the app.

### Network (MASVS-NETWORK)

- iOS App Transport Security: `NSAllowsArbitraryLoads`, `NSAllowsArbitraryLoadsInWebContent` and `NSExceptionAllowsInsecureHTTPLoads` are findings unless scoped to a justified domain.
- Android: `android:usesCleartextTraffic="true"`, `cleartextTrafficPermitted="true"` in `network_security_config.xml`, user certificate trust anchors in release, and custom `TrustManager`, `HostnameVerifier` or `onReceivedSslError` handlers that accept everything.
- Cross-platform: `badCertificateCallback` returning `true` in Dart, `rejectUnauthorized: false` and disabled checks in native modules.
- Certificate pinning is warranted for high-value apps, such as banking and health. When present, check that it pins public keys of the leaf or an intermediate with a backup pin, that pins have an expiry and rotation plan so the app is not bricked, that it covers every client including WebViews and native modules, and that a debug override cannot reach release. Report missing pinning only when the threat model of the app calls for it.

### Platform interaction (MASVS-PLATFORM)

- Android components: every `activity`, `service`, `receiver` and `provider` with `android:exported="true"` or an intent filter is an entry point. Check that each validates its extras, requires a signature permission when only own apps should call it, and never forwards a received `Intent` to `startActivity`, which allows intent redirection.
- Content providers: `android:grantUriPermissions`, path permissions, `FileProvider` paths that expose `root-path` or whole directories, and SQL built from the projection or selection arguments.
- The `PendingIntent` objects are immutable with `FLAG_IMMUTABLE` and explicit, and broadcasts that carry data are protected or local.
- Deep links and custom URL schemes: any app can invoke a custom scheme, so every parameter is untrusted. Check that links cannot trigger state changes without confirmation, load arbitrary URLs into a WebView, or carry tokens. Android App Links use `android:autoVerify="true"` with a published `assetlinks.json`, and iOS universal links use the `applinks:` entitlement with the association file.
- WebViews: `setJavaScriptEnabled(true)` combined with `addJavascriptInterface`, `setAllowFileAccess(true)`, `setAllowFileAccessFromFileURLs` or `setAllowUniversalAccessFromFileURLs`, loading URLs from intents or links without an allowlist, and `WKScriptMessageHandler` handlers that trust any frame. On iOS, `UIWebView` must be gone. JavaScript bridges expose only the narrow methods a page needs, and only to trusted origins checked on every message.
- Pasteboard and sharing: data placed on the general pasteboard or shared through extensions without need.
- iOS entitlements: only the capabilities the app uses, such as associated domains, keychain access groups and app groups, and shared containers hold no secrets readable by extensions that do not need them.

### Code quality and supply chain (MASVS-CODE)

- Input from links, notifications, files, QR codes, the clipboard and the server is validated before use, including in native code reached through JNI or bridges.
- The app enforces a minimum supported version for security fixes when the backend can require an update, and `minSdkVersion` and the iOS deployment target do not keep vulnerable platform behavior alive without reason.
- Third-party SDKs: list every analytics, ads, crash, attribution and payment SDK with its version and the data and permissions it takes. Each permission in the manifest or usage description in `Info.plist` must be needed by a feature.
- Privacy manifests: the app and each SDK declare required reason APIs and collected data in `PrivacyInfo.xcprivacy`, and the declarations match what the code does.
- Release builds are not debuggable: `android:debuggable` absent or false, `isDebuggable = false`, no `get-task-allow` in the distributed iOS entitlements, and test endpoints and feature flags removed or locked.

### Resilience (MASVS-RESILIENCE)

- Root and jailbreak detection, tamper and repackaging checks, debugger and hook detection, Play Integrity and App Attest are defense in depth. They raise the cost of attack but never replace server-side controls, so a finding is the absence of a server-side control, not only of a client check.
- Obfuscation: R8 full mode on Android, Swift symbols stripped, Dart `--obfuscate --split-debug-info`, Hermes bytecode for React Native. Recommend resilience controls only when the threat model, such as fraud, cheating or licensed content, warrants them.

### Privacy (MASVS-PRIVACY)

- Data minimization: collect only what a feature needs, prefer coarse location, the photo picker and on-device processing, and avoid persistent identifiers where an app-scoped one works.
- Tracking: on iOS, `ATTrackingManager` consent precedes tracking across apps and sites, and on Android the advertising ID respects the user's opt-out. Analytics SDKs start only after the consent the product requires.
- The Play data safety form and the App Store privacy details match the data the app and its SDKs collect.

## 3. Cross-platform specifics

- React Native: the JavaScript bundle ships as `index.android.bundle` or `main.jsbundle` and is readable, and Hermes bytecode decompiles. Secrets in `.env` files read by `react-native-config` or `app.config.js` `extra` are bundled in plain text. `AsyncStorage` is unencrypted, so use `react-native-keychain` or `expo-secure-store`. Native modules are a bridge to review like any exported API, and over-the-air update channels must be signed and served over TLS.
- Flutter: values from `--dart-define` and constants in Dart code are compiled into the AOT snapshot and recoverable with string extraction. `shared_preferences` is unencrypted, so use `flutter_secure_storage`. Platform channels and `MethodChannel` handlers validate arguments on the native side. Check `webview_flutter` and `flutter_inappwebview` settings like any WebView.
- Hybrid apps on Capacitor or Cordova: the allowed navigation list, plugins exposed to the web layer and the Content Security Policy of the bundled pages decide the whole attack surface.
- Kotlin Multiplatform and MAUI: the shared code is reviewed once, but storage, network and platform settings are reviewed in each platform project.

## 4. What to grep for

Search patterns, then read each hit in context: `allowBackup`, `exported="true"`, `usesCleartextTraffic`, `debuggable`, `addJavascriptInterface`, `setAllowFileAccess`, `setAllowUniversalAccessFromFileURLs`, `MODE_WORLD_READABLE`, `getSharedPreferences`, `Log.`, `TrustManager`, `HostnameVerifier`, `onReceivedSslError`, `NSAllowsArbitraryLoads`, `kSecAttrAccessibleAlways`, `UserDefaults.standard.set`, `evaluatePolicy`, `UIPasteboard.general`, `CFBundleURLSchemes`, `badCertificateCallback`, `AsyncStorage`, `--dart-define`, `apiKey`, `secret`, `BEGIN PRIVATE KEY`, `AKIA` and the key prefixes of the services the app uses.

## 5. Write each finding

Use the same format as a web finding, with MASVS controls as references:

```markdown
### [High] Session token stored in plain SharedPreferences

- **Severity**: High. CVSS-style reasoning: local attack vector through backup extraction or a rooted device, low complexity, no privileges on the server, no user interaction, high confidentiality impact on the account.
- **Location**: `app/src/main/java/com/example/auth/SessionStore.kt:27`
- **Category**: MASVS-STORAGE-1, MASTG test for sensitive data in local storage, CWE-922.
- **Attack scenario**: `android:allowBackup` is true and the token is written to `session.xml`, so `adb backup` on an unlocked device or a cloud backup restore exposes a valid session.
- **Evidence**: On an emulator with the release variant, the file `shared_prefs/session.xml` contains the key `access_token` after login. Value redacted.
- **Impact**: Account takeover for the lifetime of the refresh token.
- **Remediation**: Encrypt the token with a Keystore key and exclude the file from backup.
- **References**: OWASP MASVS v2 MASVS-STORAGE-1, CWE-922.
```

Follow each finding with the smallest fix as a code or configuration sketch in the style of the project, such as the `data_extraction_rules.xml` exclusion and the call that replaces the plain write. Group repeated instances of one root cause. Label unconfirmed findings with what would confirm them. When the task asks you to fix, fix in severity order, add tests where the logic is testable, and build every affected variant.

## 6. Executive summary

Open the report with the scope (apps, platforms, variants and commit), the method and tools, the overall risk in one sentence, a table of findings by severity and MASVS category, the top actions in order, secrets to rotate by location, specific strengths you verified, and the limits of the review such as the backend or dynamic tests left out.

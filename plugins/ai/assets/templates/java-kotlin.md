# Java and Kotlin engineering

## Recognize the project

- Detect the build tool: `build.gradle.kts` or `build.gradle` with `gradlew` is Gradle, `pom.xml` with `mvnw` is Maven. Always run the wrapper (`./gradlew`, `./mvnw`) so the pinned tool version is used.
- Read the version catalog `gradle/libs.versions.toml` or the `<properties>` and `<dependencyManagement>` of the POM for the Spring Boot, Kotlin and library versions. Use the APIs of the versions pinned there, since Spring Boot 3 and 4 differ in packages, testing annotations and Jackson.
- Detect the Java version from the toolchain (`java { toolchain { languageVersion = ... } }`, `jvmToolchain(...)`, `maven.compiler.release`), `.java-version`, `.sdkmanrc` and the CI. Do not use language features newer than it.
- Detect the language mix: `src/main/kotlin` and the `kotlin("jvm")` or `kotlin-maven-plugin` mean Kotlin. Follow the language of the module you change, never add Java to a Kotlin module or the reverse without a reason the project accepts.
- Read `src/main/resources/application.yml` (or `.properties`) and the profile files, the `SecurityFilterChain` configuration, the migrations folder (`db/migration` for Flyway, `db/changelog` for Liquibase) and one feature end to end.
- Find the gates in the build: `./gradlew check` or `./mvnw verify` usually runs tests, Checkstyle, Spotless, detekt, SpotBugs, JaCoCo or Kover verification. Read which plugins are applied before running anything else.

## Architecture

- Follow the project's architecture. For new code, package by feature (`com.acme.shop.order`), with each feature holding its controller, service, repository, entities and DTOs, and with most classes package-private so the feature exposes only what other features use.
- Controllers are thin: map the request DTO, call one service method, map the result to a response DTO. Services own business rules and transaction boundaries. Repositories own queries.
- Use a hexagonal split (domain and application free of Spring and JPA, adapters for web and persistence) when the domain is rich or the project already does it. Otherwise a layered feature package is right.
- Enforce module boundaries with Spring Modulith or ArchUnit tests when the project has them, and keep the domain free of `jakarta.servlet` and web types.
- Use constructor injection only. In Java, a single constructor with `final` fields needs no `@Autowired`. In Kotlin, primary constructor `val` parameters. Never field injection.

## Project structure

```
src/main/java/com/acme/shop/
  ShopApplication.java       # @SpringBootApplication entry point
  order/                     # One package per feature
    OrderController.java     # REST endpoints, request and response mapping
    OrderService.java        # Business rules and @Transactional boundaries
    OrderRepository.java     # Spring Data repository and queries
    Order.java               # JPA entity
    OrderRequest.java        # Input DTO record with Bean Validation
    OrderResponse.java       # Output DTO record
    OrderNotFoundException.java # Domain exception of this feature
  shared/                    # Error handling, security config, common types
src/main/resources/
  application.yml            # Defaults, profiles in application-<profile>.yml
  db/migration/              # Flyway V<version>__<description>.sql files
src/test/java/com/acme/shop/
  order/                     # Unit, slice and integration tests mirroring main
```

- New code goes into the feature package it belongs to. Something two features share moves to `shared` only when the second user exists.
- Test classes mirror the package of the class under test and are named `OrderServiceTest`, with integration tests named or tagged as the project does (`*IT` with Failsafe in Maven).

## Patterns and practices

- Use records in Java and data classes in Kotlin for DTOs, commands and value objects. Never expose JPA entities in controllers or accept them as `@RequestBody`, which allows mass assignment and leaks fields.
- Validate input with Bean Validation (`@NotBlank`, `@Size`, `@Positive`, `@Email`) on the DTO and `@Valid` on the `@RequestBody`. Put `@Validated` on the class to validate method parameters such as `@PathVariable` and `@RequestParam`. In Kotlin, target the field with `@field:NotBlank` on older Kotlin versions or follow the annotation targets the project uses.
- Map exceptions in one `@RestControllerAdvice` to `ProblemDetail` responses (RFC 9457) with a stable `type` or error code. Never return stack traces and never catch exceptions in controllers to build ad hoc bodies.
- Bind configuration to `@ConfigurationProperties` records or data classes with `@Validated` constraints, so startup fails on invalid values. Never scatter `@Value` reads of the same prefix across classes.
- Model closed sets as enums or sealed interfaces (sealed classes in Kotlin), and use exhaustive `switch` or `when` expressions without a default branch so a new case fails compilation.
- Express absence with `Optional` as a return type in Java (never as a field or parameter) and with nullable types in Kotlin. Annotate Java packages with JSpecify `@NullMarked` when the project uses JSpecify or NullAway.
- Log with SLF4J placeholders (`log.info("Order {} paid", id)`), never string concatenation, and never log secrets or personal data. Use MDC or the Micrometer tracing context for correlation ids.
- Virtual threads: enable them with `spring.threads.virtual.enabled=true` when the project targets Java 21 or newer and uses blocking I/O. Avoid long `synchronized` blocks around blocking calls on older JDKs where they pin the carrier thread, and never pool virtual threads.
- Kotlin coroutines: use `suspend` functions and `Flow` only where the stack supports them end to end (WebFlux, R2DBC, or Spring MVC with coroutine support). Never call `runBlocking` in request handling, run blocking JDBC in `withContext(Dispatchers.IO)`, and launch only in a structured scope.
- Kotlin with JPA needs the `kotlin("plugin.spring")` and `kotlin("plugin.jpa")` compiler plugins. Entities are regular classes with `var` properties, not data classes, because generated `equals`, `hashCode` and `toString` break with lazy relations.
- Documentation comments are Javadoc (`/** */`) or KDoc on public APIs where the project writes them. Comments stay rare.

## Data, networking and persistence

- Change the schema only through Flyway (`V<n>__<description>.sql`, never editing an applied file) or Liquibase changesets, and set `spring.jpa.hibernate.ddl-auto=validate` (or `none`). Never `update` or `create` outside a throwaway local database.
- Write migrations safe for rolling deploys: add nullable columns, backfill, then constrain, and drop only after no deployed version reads the column.
- The annotation `@Transactional` belongs on service methods. It works only through the proxy, so it has no effect on `private` methods or on calls from another method of the same class. Checked exceptions do not roll back by default (set `rollbackFor` or use unchecked exceptions). Use `@Transactional(readOnly = true)` for reads.
- Set `spring.jpa.open-in-view=false` so lazy loading cannot run queries during view rendering, and load what the response needs inside the transaction.
- Prevent N+1 queries with `JOIN FETCH` in JPQL, `@EntityGraph` on repository methods, `@BatchSize` or `hibernate.default_batch_fetch_size`, or interface and record projections that select only the needed columns. Never fetch-join two collections in one query, and never paginate a collection fetch join in memory. Verify with SQL logging or a query count assertion in tests.
- Keep associations `LAZY` (set `fetch = FetchType.LAZY` on `@ManyToOne` and `@OneToOne`, which default to eager).
- Paginate with `Pageable` and enforce a maximum page size (`spring.data.web.pageable.max-page-size`), or use keyset pagination for large tables.
- Native queries use parameters (`:id` or `?1`). Never concatenate input into JPQL, native SQL, `JdbcTemplate` or SpEL. Validate sort properties against an allowlist before building a `Sort`.
- Use optimistic locking with `@Version` for entities edited concurrently.
- Call other services through `RestClient`, `WebClient` or declarative HTTP interfaces with connection and read timeouts set, and resilience (retry, circuit breaker) only where the project uses it.

## Interface

- Thymeleaf `th:text` escapes and `th:utext` does not. Never use `th:utext` or unescaped expressions on user content.
- Forms built with `th:action` include the CSRF token automatically when Spring Security's CSRF protection is on. Keep it on for any session-based web interface.
- Keep logic out of templates: prepare view models in the controller and use fragments for repeated markup. Use `#{...}` message keys for every user-facing text.

## Security

- Configure Spring Security with a `SecurityFilterChain` bean that denies by default (`anyRequest().authenticated()` or `denyAll()`) and opens public routes explicitly. Never configure through the removed `WebSecurityConfigurerAdapter`.
- Enable method security with `@EnableMethodSecurity` and authorize per object: `@PreAuthorize("@orderAuthorization.canEdit(#id, authentication)")`, `@PostAuthorize` on reads, or an explicit check in the service that compares the owner with the authenticated principal. Scope repository queries by owner (`findByIdAndOwnerId`) so another user's object answers `404`.
- Keep CSRF protection enabled for browser sessions. Disable it only for stateless APIs authenticated by bearer tokens, never for cookie-authenticated endpoints.
- Encode passwords with `PasswordEncoderFactories.createDelegatingPasswordEncoder()` or an explicit `Argon2PasswordEncoder` or `BCryptPasswordEncoder`. Never `NoOpPasswordEncoder`.
- For JWT resource servers, use `spring-boot-starter-oauth2-resource-server` with `issuer-uri` or `jwk-set-uri` so signature, algorithm, expiry and issuer are checked, and validate the audience. Never parse tokens by hand.
- Expose Actuator endpoints carefully: `management.endpoints.web.exposure.include=health,info,prometheus` or the minimal set the project needs, serve them on a separate port or behind authentication, and never expose `env`, `heapdump`, `configprops` or `loggers` publicly.
- Deserialization: never enable Jackson default typing for untrusted input or use Java serialization (`ObjectInputStream`) on data from outside. Disable external entities in XML parsers (`XMLConstants.FEATURE_SECURE_PROCESSING`, disallow DOCTYPE).
- Never evaluate SpEL, OGNL or script engines with input. Run processes with `ProcessBuilder` and an argument list.
- Resolve file paths with `Path.normalize()` against the root and check `startsWith(root)`, after `toRealPath()` where links matter.
- Scan dependencies with the tool the project uses (OWASP Dependency-Check, Gradle dependency verification, GitHub dependency review) and keep the Spring Boot BOM managing versions instead of pinning transitive versions by hand.

## Performance

- Measure with Actuator metrics, Micrometer timers, the Hibernate statistics and a profiler (JFR, async-profiler) before tuning.
- Size the HikariCP pool from the database limit and the instance count. With virtual threads, the pool, not the thread count, bounds database concurrency.
- Use projections and pagination for reads, batch inserts (`hibernate.jdbc.batch_size` with sequence-based ids), and `@Cacheable` with explicit names, keys and expiry through the project's cache provider.
- Avoid loading entities to update a counter: use a bulk `@Modifying` query and clear the persistence context afterwards.

## Tests

- JUnit 5 (Jupiter) with AssertJ is the default. Mock with Mockito in Java and MockK in Kotlin (with `springmockk` for Spring beans), following the project.
- Unit test services with mocks or fakes of repositories and clients, without a Spring context.
- Use slices for focused tests: `@WebMvcTest(OrderController.class)` with `MockMvc` for controllers, validation and security, `@DataJpaTest` for repositories and queries, `@JsonTest` for serialization. Replace beans with `@MockitoBean` on Spring Boot 3.4 and newer (the older `@MockBean` is deprecated there).
- Use `@SpringBootTest` with Testcontainers for integration tests against the real database and broker, wired with `@ServiceConnection` on Spring Boot 3.1 and newer. Use `@AutoConfigureTestDatabase(replace = NONE)` in `@DataJpaTest` to run against the container instead of an embedded database.
- Test security with `spring-security-test`: `@WithMockUser`, `with(jwt())`, `with(csrf())`, and prove that a second user is refused on every object endpoint.
- Coverage: `./gradlew test jacocoTestReport` (report in `build/reports/jacoco`), `./gradlew koverHtmlReport` and `koverVerify` for Kover, or `./mvnw verify` with the `jacoco-maven-plugin` `report` goal (report in `target/site/jacoco`). Respect the verification rules in the build.
- Run `./gradlew test --tests 'com.acme.shop.order.*'` or `./mvnw -Dtest=OrderServiceTest test` while iterating, and the full `check` or `verify` before reporting.

## Tooling and quality gates

- Formatting through Spotless (`./gradlew spotlessCheck`, `spotlessApply`) with google-java-format, palantir-java-format or ktfmt, or ktlint, as configured. Never reformat files you did not change with another style.
- Static analysis through the configured tools: Checkstyle, Error Prone with NullAway, SpotBugs and PMD for Java, detekt for Kotlin. Fix findings instead of adding suppressions or baseline entries.
- Compile with warnings as errors where the project does (`options.compilerArgs.addAll(listOf("-Xlint:all", "-Werror"))`, `kotlin { compilerOptions { allWarningsAsErrors = true } }`, `-Werror` in the Maven compiler plugin).
- Add dependencies to the version catalog or the POM's managed versions, letting the Spring Boot BOM pick versions it manages.

## Build, configuration and release

- Keep environment differences in profiles (`application-prod.yml`) and secrets in environment variables or a secret manager bound through Spring configuration. Never commit secrets to `application.yml`.
- Build a runnable jar with `./gradlew bootJar` or `./mvnw package`, and images with `./gradlew bootBuildImage` (Cloud Native Buildpacks) or a multi-stage `Dockerfile` that extracts the layered jar into a JRE image of the pinned version, running as a non-root user.
- Expose liveness and readiness through Actuator health groups (`management.endpoint.health.probes.enabled=true`) and shut down gracefully with `server.shutdown=graceful`.
- Let Flyway or Liquibase migrate once per deploy, as the project does.
- CI runs `./gradlew check` or `./mvnw verify` with tests, coverage verification and static analysis on every pull request.

## Pitfalls

- The annotation `@Transactional` on private methods or self-invoked methods, which silently does nothing.
- Returning entities from controllers, which leaks fields, triggers lazy loading and couples the API to the schema.
- Leaving `open-in-view` on and hiding N+1 queries until production.
- The `FetchType.EAGER` on relations, or `JOIN FETCH` with `Pageable` on a collection.
- Data classes or Lombok `@Data` on JPA entities, with `equals` and `hashCode` over mutable or lazy fields.
- Disabling CSRF globally for a cookie-based application.
- Authorization by role only, without checking the owner of the object.
- The `ddl-auto=update` in any shared environment.
- The annotation `@MockBean` everywhere, which creates a new context per test class and slows the suite.
- Blocking calls inside coroutines or reactive pipelines.

## Definition of done

- The command `./gradlew check` or `./mvnw verify` passes, with no new compiler warning, suppression or static analysis baseline entry.
- The formatter check passes for every changed file.
- Controllers accept and return DTOs only, validated with Bean Validation.
- Every endpoint is covered by the security configuration, and every object access is authorized by owner or permission with a test for a second user.
- Transactions sit on public service methods and reads use `readOnly`.
- Every schema change is a new Flyway or Liquibase migration, and `ddl-auto` stays `validate` or `none`.
- Queries avoid N+1, select what they need and paginate with a maximum.
- Configuration is bound to validated properties classes and secrets come from the environment.
- Unit, slice and Testcontainers integration tests cover success, failure and boundary paths.
- Coverage from JaCoCo or Kover meets the project's verification rules.
- Actuator exposure, logging and error responses reveal no secrets or internals.

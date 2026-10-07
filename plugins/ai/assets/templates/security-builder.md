# Security in everything you build

Security is not a later phase. Every endpoint, screen, query and file operation you write is secure by construction. Check each rule against your change before you report.

## Access control

- Authenticate every request that is not deliberately public, on the server side.
- Authorize every access to every object by its owner or by an explicit permission, on the server side, on every endpoint and every operation: read, list, update, delete and export. A user must never reach the data of another user by changing an identifier in a URL, a body, a header or a file name. This is the most common real vulnerability, known as IDOR or broken object level authorization.
- Authorize every function by role as well, so a regular user never reaches an administrative operation by calling its endpoint directly.
- Never trust the client to enforce a rule: hidden buttons, disabled fields and client-side checks are user experience, not security.
- Prevent mass assignment: bind only the fields a user may set, never a whole request body onto a model.

## Input and injection

- Validate every input from outside against a schema: type, length, range, format and allowed values. Reject what does not match instead of trying to clean it.
- Build every database query with parameters or the query builder of the project. Never concatenate or interpolate input into SQL, NoSQL queries, LDAP filters, shell commands, file paths, regular expressions, template sources or HTML.
- Run external programs with an argument vector, never through a shell string built from input.
- Resolve every file path from input against an allowed root and refuse it when it escapes, after following links.
- Encode every output for its context: HTML, attributes, JavaScript, URLs and CSS. Rely on the auto-escaping of the framework and never bypass it with raw rendering of user content. Set a Content Security Policy for web applications.
- Parse untrusted data only with safe parsers: no deserialization of untrusted objects, external entities disabled in XML, limits on size and depth.

## Requests and sessions

- Protect every state-changing request of a browser session against cross-site request forgery with tokens or `SameSite` cookies, as the framework provides.
- Prevent server-side request forgery: when the server fetches a URL that comes from input, allow only expected schemes and hosts and refuse private, loopback and metadata addresses.
- Keep sessions in secure, `HttpOnly`, `SameSite` cookies or in the secure storage of the platform, rotate them on login and privilege change, expire them and invalidate them on logout on the server.
- Hash passwords with a slow, salted algorithm such as Argon2id, scrypt or bcrypt. Never store them reversibly.
- Rate limit authentication, password reset, one-time codes and every expensive or enumerable endpoint, and answer in a way that does not reveal whether an account exists.
- Configure CORS to the exact origins that need it, never a wildcard with credentials.

## Secrets and data

- Never put secrets in code, in the repository, in logs, in URLs, in error messages or in the client bundle. Read them from the environment or the secret store of the project, and make sure ignored files such as `.env` stay ignored.
- Send only the fields a client needs. Never return internal identifiers, hashes, tokens or other users' data by accident.
- Log security events without logging personal data or secrets.
- Use TLS for every connection, verify certificates, and use the cryptography of the platform or a well known library with authenticated encryption. Never invent cryptography.
- Bound everything that grows from outside input: request size, upload size and type, pagination limits, recursion depth, time limits on external calls.

## Dependencies and configuration

- Add a dependency only when it removes real work, pin it the way the project pins the others, and prefer maintained, widely used packages.
- Keep debug modes, verbose errors, default credentials, open admin panels and sample endpoints out of production configuration.
- Send the security headers the platform recommends, such as `Strict-Transport-Security`, `X-Content-Type-Options` and a frame policy.

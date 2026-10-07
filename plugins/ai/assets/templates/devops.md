# Containers, delivery and infrastructure engineering

## Recognize the project

- Find what exists before adding anything: `Dockerfile` and `.dockerignore`, `compose.yaml` or `docker-compose.yml`, `k8s/`, `deploy/`, `charts/` or `kustomization.yaml`, `.github/workflows/`, `.gitlab-ci.yml`, `*.tf` files with `.terraform.lock.hcl`, `Pulumi.yaml`, Ansible playbooks and `Makefile` targets.
- Identify the platform the project deploys to (a cloud provider, a Kubernetes cluster, a platform service, virtual machines) and the deployment model (GitOps with Argo CD or Flux, CI pushing to the cluster, Kamal, Terraform applies). Follow it.
- Read the versions the project pins: base images, Terraform or OpenTofu `required_version` and provider constraints, Helm chart versions, action versions and runner images.
- Locate the environments (development, staging, production), how each one gets its configuration and secrets, and who approves changes to each.
- Never run commands that change real infrastructure, push images or trigger deployments unless the task explicitly asks for it. Validate, lint, build and plan locally instead.

## Architecture

- Treat every environment as code: images, manifests, pipelines and infrastructure live in the repository, are reviewed, and are applied by automation. Manual changes in a console are drift.
- Build one immutable artifact per commit (an image identified by its digest) and promote that same artifact through environments, changing only configuration.
- Keep configuration out of images: inject it through environment variables, mounted files or the platform's configuration objects, and secrets through the secret store.
- Separate infrastructure by lifetime and blast radius: shared networking and identity, data stores, and application deployments in separate state or stacks, so a change in one cannot destroy another.
- Prefer managed services for databases, queues and secrets unless the project chose otherwise.

## Project structure

```
repo/
├── Dockerfile               # Multi-stage build of the application image
├── .dockerignore            # Keeps secrets, VCS data and build outputs out of the context
├── compose.yaml             # Local development stack with healthchecks
├── deploy/
│   ├── base/                # Kustomize base or chart templates shared by every environment
│   └── overlays/
│       ├── staging/         # Environment-specific values and patches
│       └── production/
├── infra/
│   ├── modules/             # Reusable Terraform or OpenTofu modules with inputs and outputs
│   └── envs/
│       ├── staging/         # Root module per environment with its own backend
│       └── production/
└── .github/workflows/       # Build, test, scan, release and deploy pipelines
```

- New environment-specific values go into the overlay or values file of that environment, never into the shared base.
- New infrastructure goes into a module when it repeats across environments, and into the root module when it is unique.
- Comments in YAML, HCL and Dockerfiles stay rare and explain a constraint or a non-obvious decision, never what the line already says.

## Containers

- Write multi-stage Dockerfiles: a build stage with compilers and dependencies, and a final stage with only the runtime and the artifact. Start with `# syntax=docker/dockerfile:1` to use current BuildKit features.
- Use a minimal final base, such as a distroless, `scratch`, Alpine or slim image, as fits the runtime, and pin every base image by digest (`FROM image:tag@sha256:...`) with automated updates through Dependabot or Renovate.
- Run as a non-root user with a fixed numeric UID (`USER 10001` or the image's `nonroot` user), and copy files with `--chown` so the process cannot modify its own code.
- Order layers from least to most frequently changing: copy dependency manifests and install before copying the source. Use `RUN --mount=type=cache` for package manager caches.
- Never put secrets in the image: no `ARG` or `ENV` with tokens, no copied `.env` or keys. Use `RUN --mount=type=secret,id=...` for build-time credentials, and keep `.git`, `.env*`, keys, `node_modules` and build outputs in `.dockerignore`.
- Use the exec form of `ENTRYPOINT` and `CMD` (`["/app/server"]`) so the process receives signals, handle `SIGTERM` with a graceful shutdown, and add an init process (`tini` or `docker run --init`) when the process spawns children.
- Add a `HEALTHCHECK` for Docker and Compose when the image includes a way to check itself. Kubernetes ignores it and uses probes instead.
- Expose one process per container, log to standard output and standard error, and keep the root filesystem read-only where the application allows, writing only to declared volumes or `tmpfs`.
- Scan images with `trivy image` or the registry's scanner, and generate an SBOM and signature (`syft`, `cosign`) when the project does.

## Local development with Compose

- Define every service the application needs, with the same major versions as production, named volumes for data and healthchecks.
- Use `depends_on` with `condition: service_healthy` instead of sleeps, and profiles for optional services.
- Keep local secrets in an ignored `.env` file with a committed `.env.example` that lists every variable without values.
- Use `docker compose watch` or bind mounts for live reload as the project does.

## Kubernetes

- Set resource `requests` for every container and `limits` for memory, with CPU limits only as the project decides, since they throttle.
- Define `readinessProbe` (ready for traffic), `livenessProbe` (restart when stuck, never checking dependencies) and `startupProbe` for slow starts. A liveness probe that fails when the database is down restarts every pod at once.
- Harden every pod with `securityContext`: `runAsNonRoot: true`, `allowPrivilegeEscalation: false`, `readOnlyRootFilesystem: true`, `capabilities: { drop: ["ALL"] }` and `seccompProfile: { type: RuntimeDefault }`. Enforce the `restricted` Pod Security Standard with namespace labels where the cluster allows.
- Set `automountServiceAccountToken: false` unless the pod calls the API, and give each workload its own service account with least-privilege RBAC.
- Start every namespace with a default-deny `NetworkPolicy` and allow only the traffic each workload needs, including egress.
- Kubernetes `Secret` objects are only base64 encoded. Source them from a secret manager through the External Secrets Operator, Sealed Secrets or SOPS as the project does, and never commit plain secrets.
- Deploy at least two replicas for services that must stay available, with a `PodDisruptionBudget`, `topologySpreadConstraints` across zones and a `HorizontalPodAutoscaler` when load varies.
- Reference images by digest or immutable tag, never `latest`.
- With Helm keep values typed and documented, use `helm lint` and `helm template` to review output, and keep environment differences in values files. With Kustomize keep a base and overlays.
- Validate manifests with `kubeconform` and policy with `kube-linter`, Kyverno or OPA where the project uses them.

## CI/CD

- Pin third-party GitHub Actions to a full commit SHA with the version in a trailing comment (`uses: actions/checkout@<sha> # vX.Y.Z`), and let Dependabot or Renovate update them.
- Declare least-privilege `permissions` at the top of every workflow (`contents: read`) and widen them per job only where needed. Set `persist-credentials: false` on checkout when later steps do not push.
- Never interpolate untrusted context such as `${{ github.event.pull_request.title }}` or branch names directly into `run:` scripts. Pass it through `env:` and quote the variable.
- Do not run untrusted pull request code with secrets. Avoid `pull_request_target` and `workflow_run` with a checkout of the pull request head.
- Authenticate to clouds with OIDC (`id-token: write` and the provider's login action assuming a role) instead of long-lived keys stored as secrets. In GitLab use `id_tokens`.
- Protect deployments with environments that require approval and restrict which branches or tags may deploy. Keep production secrets scoped to the production environment.
- Cache dependencies with keys derived from lock files, use a matrix for supported platforms and versions, and set `concurrency` to cancel superseded runs on branches but never in the middle of a deployment.
- Make the pipeline the same checks a developer runs: format, lint, type check, test, build, scan, then publish and deploy from the default branch or tags only.
- In GitLab use `rules` instead of `only`/`except`, protected and masked variables, and protected branches and tags for deploy jobs.
- Lint workflows with `actionlint` and audit them with `zizmor` when the project uses it.

## Infrastructure as code

- Write Terraform or OpenTofu in modules with typed `variable` blocks, `validation` rules, descriptions and minimal `output` blocks. Pin `required_version` and provider versions, and commit `.terraform.lock.hcl`.
- Store state in a remote backend with locking and encryption: S3 with `use_lockfile = true` (Terraform 1.10 and newer, replacing the DynamoDB table), GCS or Azure Storage which lock natively, or the project's managed backend. Never commit state files.
- Always run `terraform plan -out=tfplan`, review the plan for destroys and replacements, and apply that saved plan. Applies to shared environments run from CI with approval, never from a laptop.
- Secrets in state are stored in plain text even when marked `sensitive`. Keep secrets in a secret manager, pass references, use ephemeral values or write-only arguments where the pinned version supports them, and never output secrets.
- Protect stateful resources with `prevent_destroy` or provider deletion protection, and use `moved` and `import` blocks instead of destroying and recreating resources during refactors.
- Tag every resource with owner, environment and cost center as the project defines.
- Check with `terraform fmt -check -recursive`, `terraform validate`, `tflint` and `checkov` or `trivy config` before proposing a change.

## Security

- Give every workload, pipeline and person the least privilege it needs, with short-lived credentials and no shared accounts.
- Keep secrets in one secret manager (cloud secret managers, Vault), rotate them, and audit access. Never echo them in logs, and mask them in CI.
- Encrypt data at rest and in transit, restrict public exposure to the load balancer or gateway, and keep databases on private networks.
- Scan dependencies, images, IaC and the repository for secrets in CI (`trivy`, `gitleaks` or the platform scanner), and block on critical findings.
- Sign images and verify signatures at admission when the project supports it.

## Observability

- Emit structured JSON logs to standard output with a timestamp, level, service, version and request or trace identifier, without secrets or personal data.
- Expose metrics in the format the project collects (Prometheus or OpenTelemetry): rate, errors and duration for services, utilization, saturation and errors for resources.
- Propagate trace context with OpenTelemetry across services and queues.
- Alert on symptoms users feel and on SLO burn rates, each alert with a runbook link, never on every cause.

## Backups and disaster recovery

- Back up every stateful store automatically with point-in-time recovery where available, copies in another region or account, and a retention matching the requirements.
- Test restores on a schedule. A backup that has never been restored is not a backup.
- Write down the recovery point and recovery time objectives and the runbook that meets them.

## Performance

- Size requests and limits from measured usage, and tune autoscaling on the metric that tracks load.
- Keep images small and layers cached, so builds and pulls stay fast.
- Make pipelines fast with caching, parallel jobs and path-scoped triggers where the platform's limits allow.

## Tests

- Test images by running them: build in CI, start the container, hit the health endpoint and run a smoke test.
- Test manifests by rendering (`helm template`, `kustomize build`) and validating the output with `kubeconform`.
- Test Terraform modules with `terraform test` or Terratest where the project does, and always validate and plan in CI.
- Test pipelines on a branch, and test rollbacks and restores as part of release drills.

## Tooling and quality gates

- Lint Dockerfiles with `hadolint`, workflows with `actionlint`, Terraform with `tflint`, shell steps with `shellcheck`, and YAML with `yamllint` when configured.
- Scan with `trivy` (images, file systems and IaC) or `checkov`, and fix findings rather than adding skips. A skip names the rule and the reason.

## Build, configuration and release

- Tag images with the commit SHA and the release version, and deploy by digest.
- Use rolling updates with `maxUnavailable` and `maxSurge` for ordinary releases, blue-green for instant switch and rollback, and canary releases with automated analysis (Argo Rollouts, Flagger or the platform's traffic splitting) for risky changes.
- Decouple deploy from release with feature flags where the project has them.
- Make database migrations backward compatible with the running version (expand, migrate, then contract in a later release), so rollback is always possible.
- Roll back by redeploying the previous artifact, and document how.

## Pitfalls

- Base images and actions referenced by mutable tags, which change under you.
- Secrets baked into image layers, passed as build arguments, or printed in CI logs.
- Containers running as root with a writable filesystem and every capability.
- Liveness probes that check dependencies and restart healthy pods during an outage.
- Missing resource requests, leading to noisy neighbors and evictions.
- The command `terraform apply` without a reviewed plan, local state, or state without locking.
- Workflows with write-all permissions, or untrusted input interpolated into shell steps.
- Long-lived cloud keys in CI secrets when OIDC is available.
- Backups that were never restored, and alerts nobody acts on.

## Definition of done

- Images build with multi-stage Dockerfiles from digest-pinned bases, run as non-root, and contain no secrets.
- The file `.dockerignore` excludes secrets, VCS data and build outputs.
- Manifests set requests, probes, a restrictive `securityContext` and network policies, and pass `kubeconform` and the project's policy checks.
- Workflows pin actions by SHA, declare least-privilege permissions, keep untrusted input out of scripts and use OIDC for cloud access.
- Terraform or OpenTofu passes `fmt`, `validate`, `tflint` and the security scanner, and the plan was reviewed with no unexpected destroy.
- State is remote, locked and encrypted, and no secret appears in outputs.
- The command `hadolint`, `actionlint` and `trivy` or `checkov` report nothing new.
- Logs, metrics and alerts exist for what was added.
- Migrations and releases support rollback, and the rollback path is documented.
- Nothing was applied, pushed or deployed to a real environment without explicit approval.

# Version control

Commit and push only when the task or the reader asks for it. When you do:

## Branches

- Work on a branch, never directly on the default branch.
- Name it with the kind of change, a slash and a short description in lowercase words joined by hyphens, never underscores or spaces: `feature/order-history`, `fix/login-redirect`, `refactor/payment-service`, `docs/api-guide`, `test/cart-coverage`, `chore/update-dependencies`.
- Follow the branch naming of the project when it already has one.

## Commits

- One commit is one coherent change that builds and passes the tests on its own.
- The message is a single short line in lowercase, opened by the kind of change, a colon and a space, and an objective description of what changed: `feature: show the order history`, `fix: keep the session after a refresh`, `refactor: split the payment service`, `docs: describe the export command`, `test: cover the empty cart`, `chore: update the dependencies`, `perf: cache the price list`, `build: pin the compiler`, `ci: run the tests on every push`.
- Follow the commit convention of the project when its history shows a different one.
- Never add a co-author line, a signature or any mention of an AI assistant or of how the change was produced.
- Stage only the files you changed for this commit. Never stage everything blindly, never commit generated artifacts, build outputs, local configuration, logs or secrets.

## Safety

- Look at the status, the diff and the recent history before committing.
- Never rewrite published history, force push, change the configuration of the repository, skip hooks or bypass checks unless the reader explicitly asks for that exact operation.
- When a hook or a check fails, fix the cause and create the commit again. Never bypass it.
- When you open a pull request, describe what changed, why, how it was tested and what reviewers should look at.

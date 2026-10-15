# Tests and coverage

## What every change carries

- Every behavior you add has tests for its success path, its failure paths, its boundaries and, where it has one, its lifecycle.
- Every bug you fix gets the test that would have caught it, written first and seen failing before the fix.
- Tests use the framework, the folder layout, the naming and the helpers the project already has. A new test framework is never added without a request.
- A test checks behavior through the public interface, one behavior per test, with a name that states the behavior. It never depends on another test, on the order of execution, on the current time or on the network, unless the project already isolates those.
- Assert on stable facts such as error codes, values and state, not on message wording or implementation details.
- Mock only what you do not own or cannot run, such as external services, and prefer fakes that behave like the real thing. Never mock the code under test.
- Never edit a test to make it pass unless the test itself is wrong, and then say why. Never skip, disable or quarantine a test to get a green run.

## Test-first when it pays

Write the failing test first when the expected behavior is clear: a bug fix, a pure function, a validation rule, a parser, an API contract. Watch it fail for the right reason, write the smallest code that passes it, then refactor with the test green.

## Coverage

Decide how much coverage the task needs and say what you decided:

- When the reader or the project asks for 100 percent coverage, reach 100 percent of lines and branches of the code you changed, and report the number from the coverage tool.
- When the project declares a threshold in its configuration or its continuous integration, meet or exceed it, and never lower it.
- Otherwise cover every behavior and every branch that carries logic. Report the coverage of the changed code when the project has a coverage tool, and do not chase lines that hold no logic.

Run the coverage tool the project uses, such as the coverage options of the test runner, `pytest --cov`, `go test -cover`, `cargo llvm-cov`, JaCoCo, Kover, `xcodebuild -enableCodeCoverage`, `flutter test --coverage`, `vitest --coverage` or `jest --coverage`, and read the report instead of guessing.

## Running them

Run the tests the way the continuous integration runs them, non-interactively, without watch modes. Run the narrowest suite while iterating and the full suite before you report. A red suite is never reported as done.

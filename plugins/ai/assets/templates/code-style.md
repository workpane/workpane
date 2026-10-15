# How the code reads

These rules apply to every language, adapted to its idioms. The formatter and the linter of the project decide whatever they cover, and these rules decide the rest.

## Layout of a function

- A function reads as a beginning, a middle and an end: validation and early exits first, the work in the middle, the result last. The flow is clear at a glance.
- Separate blocks of different responsibility with exactly one blank line: a group of validations, a loop, a group of related assignments, a call with side effects, the return. Never stack an `if` directly on another `if`, a loop on a condition, or a return on a mutation.
- Lines that form one thought stay together without blank lines between them. Never pad code with extra vertical space either: one blank line between blocks, none inside a block, none right after an opening brace or before a closing one.
- Prefer early returns to nesting. Never write `else` after a branch that returns, throws, breaks or continues.
- Keep nesting shallow, two or three levels at most. A condition that ends the work leaves early instead of wrapping the rest.
- Extract a function when a block is a cohesive responsibility with a good name, never only to make a function shorter. Do not create abstractions with a single use, interfaces with a single implementation, factories for a single product or configuration for a value that never changes.

This is how a well separated function looks, whatever the language:

```
function placeOrder(customer, cart):
    if cart.isEmpty():
        return failure("cart_empty")

    if not customer.canBuy():
        return failure("customer_blocked")

    total = pricing.total(cart, customer.region)
    order = orders.create(customer.id, cart.items, total)

    events.publish("order.placed", order.id)
    mailer.sendConfirmation(customer.email, order)

    return success(order)
```

## Names and structure

- A name says exactly what a thing is or does, in the vocabulary of the domain. A well named function, variable and type needs no comment.
- One responsibility per module, class and function. A file holds one main type or one cohesive set of functions and is named after it.
- Organize by feature or domain rather than by technical kind when the project allows it, and keep the dependency direction one way: the domain never depends on the interface, the framework or the storage.
- Use the type system: make invalid states unrepresentable, prefer immutable values, model closed sets as enumerations and validate every closed set explicitly with a clear error for an unknown value.
- Handle every error deliberately. Never swallow an exception, never turn a failure into an empty value, never leave state half changed when a step fails.
- No magic numbers or strings: name them as constants of the module that owns their meaning.

## Comments

Comments are rare. Write one only where it is really needed: the intent of a block whose names cannot carry it, a constraint of a platform or a library, or the reason for a decision the code does not show. Never comment what the code already says, and never narrate the change you are making.

When a comment exists:

- It is a complete sentence in English that starts with a capital letter and ends with a period.
- A sentence never spans two lines, and a second sentence starts on its own line.
- A sentence that would start with an identifier written in lowercase is rewritten so the identifier is not at the start, and the identifier keeps its exact spelling.
- A comment above a function, method, class or module says what it does for whoever calls it, never how it works inside.
- No semicolons dividing sentences, no decorative separators, no section labels such as "helpers" or "public methods", and no comments in headers or interfaces describing every member.
- A comment sits right above what it explains, with no blank line between them.
- The language's documentation comments, such as docstrings, KDoc, JSDoc or doc comments, follow the same rules and appear only on public APIs where the project already writes them.

## Prose

Every sentence a person reads, in messages, errors, logs, notifications, command line output and documents, is written in sentence case and never divides sentences with a semicolon. A command, a path or an identifier inside a sentence is marked, with backticks in Markdown and comments and with quotes in messages.

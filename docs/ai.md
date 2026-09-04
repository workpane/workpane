# AI

Tasks are grouped into renameable workspaces, and each workspace owns a kanban with the To Do, Doing,
Blocked, Review and Done columns. A task is either an agent run or a command.

## A task is a conversation

A task is a conversation held with an agent, and every run is one turn inside it. What was said stays
with the task, so an agent keeps what it already learned instead of starting from nothing each time.

Opening a task replaces the board with its conversation. The composer stays enabled while a turn is
running: a message typed then joins the conversation at once and is carried into the next iteration of
that turn, and one that arrives while the final answer is being written opens the turn after it.

Running the task sends its prompt again as a new message, because that prompt is the standing
instruction a schedule repeats. Resetting the task clears the conversation together with the runs it
recorded, and nothing else removes that history.

The conversation is stored in the shape the product owns and projected into the shape each wire
protocol names, so the same dialogue survives a change of provider.

## Agents

An agent is created under AI Agents and carries a name, a stable identifier spelled from that name, a
description, the model connection it speaks through, its iteration limit and its own system prompt.
There is no system prompt written into the product, so what an agent is told is what you wrote for it.

The system prompt accepts tags from a closed set, and the dialog offers a template that already places
them and the list of every tag with what it stands for. A tag nobody declares is refused when the agent
is saved rather than met by a run that cannot answer it.

| Tag | Stands for |
| --- | --- |
| `{{SYSTEM_PROMPT_DATA}}` | The working directory, the environment, the published context files and the skills of the task |
| `{{TASK_TITLE}}`, `{{TASK_PROMPT}}`, `{{TASK_WORKDIR}}` | What the task itself declares |
| `{{DATE_TIME}}`, `{{TIME_ZONE}}`, `{{OPERATING_SYSTEM}}` | The moment and the machine the run happens on |

A capability tag answers what the run really has — the selected model and its traits, whether it reads
an image, whether a search and a speech service are configured, which servers are connected and the
room the turn has — so an agent never promises what is not there.

A model that declares the system role receives the instructions as a system message, and one that does
not receives them as the first user message.

Removing an agent is allowed and the tasks it was handed stop with the reason that names it, while a
connection an agent runs on is refused until no agent names it.

## Connections and providers

A connection is one configured provider and model pair carrying its display name, credential, address,
declared parameters and extra parameters. It is identified by the `provider/model` key, and one key
names one configuration.

The provider catalog is declarative data in `plugins/ai/assets/providers.json`, so a provider, a model
or a parameter is added as data and never as interface code. The wire protocols are the Anthropic
native API and the OpenAI-compatible API, and a command line agent is a provider on its own protocol,
invoked rather than requested.

Each provider declares the modalities it answers, so a conversation, a picture and a spoken line are
reached through one contract rather than through a branch on which service replied. An endpoint that
answers a single request declares its path, the header its credential travels in, the field carrying
the text and the fields every request to it carries unchanged, which is what makes a speaking service
an entry of data. A provider that holds no conversation declares no protocol, no model and no
parameter, and every surface offering a connection reads the providers answering the conversation, so
a service that only speaks is never offered as a model to talk to.

A secret is either a literal value or a reference to an environment variable written as `{env.NAME}`,
resolved at request time. A referenced variable that is unset or empty is an explicit error and never
an empty credential.

## Tools

An agent run continues while the model keeps asking for tools and stops when it answers without one.

Tools cover reading, writing and editing files, listing, creating directories, moving, copying,
removing, describing a path, searching by name and content, looking at an image, running a command,
fetching a page, searching the web, generating an image and speech, reading the skills of the task and
everything the connected Model Context Protocol servers publish.

A file tool resolves its path against the working directory the task declares and judges it by where it
lands rather than how it was written, so the absolute form a model naturally uses is accepted while a
traversal or a symbolic link reaching outside is refused by name.

Every tool carries a deadline and answers as a failure when it expires. Each declares what it reaches,
and two calls of one turn run together only when their declarations cannot meet.

## Prompt templates

The dialog offers a chooser of templates, each named and described in the language of the interface.
Choosing one and inserting it writes its body into the prompt, which the writer then owns: there is no
system prompt written in code, so a template is an offer rather than a behaviour.

| Template | What it is for |
| --- | --- |
| Website builder | Building and maintaining a site, from its structure to how it is verified |
| Application builder | Building and maintaining an application, from its layout to its packaging |
| Security reviewer | Reviewing the application it runs on against attackers, intruders and holes |
| Code reviewer | Finding real defects, from memory faults and leaks to races and unbounded growth |

Every body carries the capability tags in place. The set of templates is data in the asset catalog and
each identifier names the three translated keys carrying its name, its description and its body, so a
template is added by one entry and never by interface code.

## Skills and context

The agent reads what the published ecosystems left in the workspace it runs in and in the roots of the
machine, and installs none of it. See [Agent resources](agent-resources.md) for the kinds, the roots
and the precedence.

A skill is disclosed progressively: the instructions offer only its name and description, and its body
is loaded by the skill tools. The published context files `AGENTS.md`, `AGENT.md` and `CLAUDE.md` are
always-on instructions and join the prompt in full.

## The model catalog

Every model lives in `plugins/ai/assets/models.json`, which the plugin carries as a resource. Adding a
model is one line of data, and a provider declares in code only which models it opens with. A model
declares its identifier, display name, context window, maximum output, its traits and its published
price per token.

The file is rebuilt from a [LiteLLM](https://github.com/BerriAI/litellm) checkout, and whatever the file
already declared is kept, so a model added by hand survives a regeneration.

```bash
python3 make.py models /path/to/litellm
```

## Request limits

A hosted service answers a limited number of requests, and an agent run reaches the model once per
iteration, so a single task can spend a whole minute of budget by itself. Every request to one service
waits in the same queue, whatever workspace, task or connection asked for it, and the pace is
configured under AI Providers.

| Setting | What it does |
| --- | --- |
| **Delay between requests** | Minimum time between two requests reaching that service |
| **Maximum requests per minute** | Rolling one-minute window, which is the shape services publish their limits in |
| **Maximum requests at the same time** | How many may be in flight, where one suits a service that refuses concurrent calls |

The three compose, so a request leaves only once the delay has passed, the window has room and no other
request of that provider is still in flight. A zero means that limit was never declared.

Write the number the service publishes, leaving margin for the attempts a rejection costs. A free tier
documented at forty requests per minute is written as thirty-five with one request at a time. A daily
quota is not a rate, so no pacing avoids reaching it.

Rejections need no configuration. A service that refuses because of its rate is tried again after the
delay it asked for, and otherwise after a wait that doubles from one second up to the declared ceiling.
A task that is waiting says so on its card and records every wait in the execution log of that run.

## Runs and history

Every run is recorded as an execution with its UTC start and finish, status, token usage, finish reason,
error message and returned content, together with the provider and model it really spoke to, so its cost
is answered from the price of that model.

A run ends with a stop reason from a closed set — answered, the iteration limit, the answer budget, the
repetition of one tool, cancelled or failed — and only the last two are failures.

One surface answers for a task and carries its conversation, its executions, their log entries and its
returned content as tabs, following the run while it is open.

## Scheduling

A task may own no schedule, one future local date and time, a minimum one-minute interval or a strict
five-field POSIX cron expression. Cron accepts portable numeric wildcards, values, ranges and comma
lists, treats both zero and seven as Sunday and applies the POSIX day-of-month and day-of-week rule.

Schedules keep the timezone they were written in, persist every instant in UTC and show the next
occurrence in the current locale. An occurrence is chosen on the wall clock the expression is written
in, so a time daylight saving skips still runs that day and one it repeats fires once.

A due occurrence advances its schedule and inserts its queue row in one transaction, so a crash cannot
dispatch it twice. The queue survives a restart and is dispatched as soon as the plugin finishes
loading.

## Related

- [Plugins](plugins.md) — every feature and what it owns

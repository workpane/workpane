# Remote SSH

The Remote SSH plugin connects to a computer through its installed OpenSSH server. Workpane uses the local OpenSSH client, standard SSH channels, the server's SFTP subsystem and bundled sources executed by the remote operating system. An installed application can connect and work offline without accessing a release service.

## Profiles and authentication

A profile declares its name, host or SSH alias, optional account, port, local identity and configuration files, workspace directory, connection timeout and password storage choice. Empty account and local path fields retain the OpenSSH configuration, and port zero retains its configured port. An empty workspace resolves to the authenticated account's native home. Aliases, literal account names and IPv6 addresses reach the SSH client without variable expansion.

The account field sits immediately above its password and password storage choice. The remote operating system is detected when connecting. An authenticated command distinguishes the native Windows command shells from POSIX, and the POSIX controller identifies Linux or macOS with `uname` before creating its scope. Every probe has a private reply marker, bounded output and a cancellable owner. Unsupported systems and invalid replies fail explicitly. An absolute configured directory is checked against that detected system.

The connection owns a separate credential channel for every SSH process. Host trust, account passwords and encrypted keys use masked native dialogs. Accepted credentials may serve another channel of that connection, while a repeated prompt on the same channel requests a fresh answer. Prompts are serialized, cancellation closes the owning dialog and shutdown clears its input and cached secrets. A saved password belongs to the operating system credential store and never enters preferences, snapshots or diagnostics. Profile saves and removals show their pending activity. Removing a profile updates the current saved list after its credential cleanup, preserving other profiles created or edited during that wait. Duplicate actions on a profile being removed are suppressed.

## System requirements

The client computer needs the OpenSSH command `ssh`. The remote server must support authenticated command channels, SFTP, pseudo-terminals and TCP forwarding for forwarded ports. Its account must be allowed to run the operating system tools and access its selected workspace.

A macOS account uses the system POSIX shell, utilities and JavaScript for Automation through `osascript` to own native process sessions and publish atomic file changes. A Linux account uses its POSIX shell, process inspection tools and GNU core utilities with `mv --no-copy`, supported from [coreutils 9.2](https://lists.gnu.org/archive/html/info-gnu/2023-03/msg00005.html), for exclusive publication. A Windows account uses Windows PowerShell, its .NET runtime and Windows APIs for native handles, file replacement, permissions, process creation and job ownership.

## Connection ownership

The session starts a private control scope over SSH and an independent SFTP subsystem. The scope reserves monotonic program identities before launch and retains a bounded set of active programs. A late launch cannot claim an identity that closed. POSIX ownership follows the actual process session across process groups, and Windows ownership uses kill-on-close job objects assigned before a suspended target resumes.

The control channel reports bounded metadata and actual target completion independently of program output and terminal rendering. Its renewable lease ends abandoned work when the owner disappears. Normal disconnect releases consumers and accepted filesystem operations, closes SFTP input and requests controlled scope shutdown. Transport loss and the server cleanup that follows are asynchronous.

The local dispatcher bounds active ordinary requests, queued declarations and retained payload bytes. Lease renewal, stopping and live stream reads retain independent capacity. A quiet process or terminal read stays pending until data or completion arrives. Other requests have bounded deadlines. Caller cancellation does not release provider admission while accepted preparation, file work or cleanup is still running.

## Files and account context

The file transport implements bounded SFTP v3 packets with exact request identities, strict lengths and response types, binary strings, status validation and cleanup of accepted handles. Fragmented input retains bounded storage. Closing input waits for the subsystem's actual exit, and malformed replies close the channel without exposing their bytes.

Every workspace has a canonical root. POSIX resolution checks SFTP canonical paths and existing ancestors. Windows resolution opens native filesystem handles and checks volume and file identities through ancestry, including junctions and symbolic links. A missing path remains distinct from failed inspection. Listings, walks and searches bound inspected entries and carry incomplete results explicitly.

A complete read returns canonical Base64 content and a SHA-256 revision computed by the local worker pool. Prefix reads preserve exact bytes and indicate whether the complete file fit. Remote modification stamps remain opaque and retain native precision, while recent changes are judged on the remote clock.

Saving checks the expected content revision, uploads into a protected sibling staging folder and checks the previous content again before atomic publication. Existing files retain permissions. Windows replacement restores the complete destination DACL and protection through a metadata-only handle attached to the published bytes, preserving inherited access entries without following a pathname another writer may change. A Windows payload keeps owner-only permissions until publication applies its destination inheritance. Creating a file cannot replace another writer's destination. Generated Windows file operations terminate on native method errors and report a refused rename as failure. Accepted writes retain their actual completion across intentional consumer or connection closure, and the editor preserves text edited after that save began. A missing staging parent returns its filesystem failure without ending a healthy connection.

The authenticated account name, home and login shell come from the remote operating system. Repository discovery walks canonical ancestors for a Git entry. Separate read-only scopes expose bounded account and repository resources, while writable requests remain confined to the selected workspace.

## Programs and consumer workspaces

A workspace capability belongs to one requesting plugin and one exact connection instance. The provider reserves its native group synchronously before accepting the queued open. Opening cannot create a missing reservation. Completed cleanup removes the group record, so sustained tasks do not accumulate closed groups and a delayed request cannot revive a previous owner. Closing stops every producer before joining any program and retains admission through accepted requests and cleanup.

An executable search uses the existing control channel and checks the remote search path and explicit absolute directories. Starting a program validates and copies its absolute executable, working directory, literal arguments, environment additions and removals and optional binary input before asynchronous work. Windows batch commands reject arguments whose command processor expansion would change their meaning.

Output and diagnostics remain separate binary streams with bounded native backpressure. Pull delivery retains unread output until its owner consumes or stops it. Input retains every byte across source loading and target launch, and explicit EOF drains accepted bytes before closing the stream. A target's completion comes from its control record after its final output, independently of the local SSH process exit. A producer ending without that record fails within a bounded wait.

The current SDK returns an exit `code` and a `failed` boolean. Any nonzero status or native process failure sets `failed`. POSIX signal completion contributes its shell status and Windows exceptions retain their native signed status. Explicit stopping returns its cancellation status. Transport failures carry a separate structured problem, so consumers preserve the difference between a command exit and a disconnected workspace.

## Terminals and forwarding

Connection preparation and folder browsing show a busy indicator, their current phase and cancellation. Opening the editor navigates immediately to a cancellable opening view. Remote file and directory loads and document saves display their activity while the rest of the interface remains usable. Background polling does not show foreground loading.

Terminal and port pages have close buttons. Closing a terminal page releases its terminals and their accepted startup. Closing a port page removes its UI subscriptions and keeps its connection-owned forwards active. Reopening the page shows those forwards so each can be stopped explicitly. Creating and closing forwards show pending activity and suppress duplicate actions.

A remote terminal shows startup activity until output arrives and runs through a real SSH pseudo-terminal. Input is delivered in order and acknowledged after delivery, native resize reaches the remote terminal and interruption follows the terminal's foreground process. Terminal output stays buffered while its component applies backpressure. Closing a generation releases its accepted start before a restart begins another one. POSIX terminal closure sends native hangup before bounded forced cleanup, allowing the shell to commit its normal private history while its process session remains owned.

Shell startup preserves the account configuration and reports directory changes through terminal escape sequences. A terminal keeps its history identity across restart in a protected folder owned by the connection. Releasing its final shell removes that history without writing into the workspace or the account's ordinary history file.

A Windows SSH command runs in the SSH session rather than the signed-in graphical desktop. A synchronous GUI call such as `MessageBox.Show` can wait for an inaccessible dialog. This is consistent with [Windows service desktop isolation](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services). Long-running commands remain valid terminal workloads. Use terminal interruption for commands that handle it, or restart or close the terminal to release its owned process tree. Workpane does not redirect SSH programs into another user session.

A forwarded port atomically binds local loopback and reaches remote loopback through the session's SOCKS listener. Negotiation validates fragmented replies and address lengths, relay uses bounded binary reads and acknowledged writes and an input half-close preserves the response direction. All forwards close before their owning connection is released.

## Product consumers and validation

The editor opens a remote workspace without discovering every installed language server. Opening a document starts one background search for its language, shares that search with other documents and shows discovery and initialization status. File editing remains usable during discovery, completion updates analysis even before the editor is first presented, and a reply after closure cannot start analysis or revive the workspace.

The Code Editor uses the same filesystem and process backend for documents, trees, search, EditorConfig and language servers. After saving, its next poll verifies the committed content before caching metadata, so an external edit crossing save completion cannot become an accepted stamp for older bytes. Disconnect preserves unsaved text and availability, and reconnect acquires a fresh workspace identity. AI tasks, command line agents, tools, instructions, resources, file links and images retain their declared remote host through cancellation and failure.

Native integration suites exercise installed OpenSSH with isolated loopback servers, temporary keys and explicit trust files. They cover binary SFTP, direct session startup, native ownership and cancellation, actual exit status, file revisions and conflicts, editor and language server cleanup, terminal interaction and history, credentials, agents, tasks, forwarding and idle lease renewal. Module suites drive malformed data, bounds, cancellation races and accepted work that outlives its caller. Build and Release package the desktop application for every supported operating system and architecture.

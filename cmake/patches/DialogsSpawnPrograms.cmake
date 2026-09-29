# Starts the helper programs of the dialog library through the system in one step, with the signals of a shell and no descriptor of the product but the streams they read and write.
# The library forks a threaded product, keeps every descriptor open in the child, lets the ignored broken pipe of the product pass to the helper and calls exit when the helper cannot start.
# The configure step fails when neither the fork nor its replacement is found, so a new pin of the library cannot skip it silently, and a source already replaced is left as it is.
file(READ "${SOURCE}" content)

set(forked [=[
    m_pid = fork();
    if (m_pid < 0)
        return;

    close(in[m_pid ? 0 : 1]);
    close(out[m_pid ? 1 : 0]);

    if (m_pid == 0)
    {
        dup2(in[0], STDIN_FILENO);
        dup2(out[1], STDOUT_FILENO);

        // Ignore stderr so that it doesn’t pollute the console (e.g. GTK+ errors from zenity)
        int fd = open("/dev/null", O_WRONLY);
        dup2(fd, STDERR_FILENO);
        close(fd);

        std::vector<char *> args;
        std::transform(command.cbegin(), command.cend(), std::back_inserter(args),
                       [](std::string const &s) { return const_cast<char *>(s.c_str()); });
        args.push_back(nullptr); // null-terminate argv[]

        execvp(args[0], args.data());
        exit(1);
    }
]=])

set(spawned [=[
    for (int descriptor : {in[0], in[1], out[0], out[1]})
        fcntl(descriptor, F_SETFD, FD_CLOEXEC);

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    posix_spawn_file_actions_adddup2(&actions, in[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    short spawnFlags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#if defined(__APPLE__)
    spawnFlags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#else
    posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
#endif
    posix_spawnattr_setflags(&attributes, spawnFlags);
    sigset_t none, defaults;
    sigemptyset(&none);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setsigdefault(&attributes, &defaults);

    std::vector<char *> args;
    std::transform(command.cbegin(), command.cend(), std::back_inserter(args),
                   [](std::string const &s) { return const_cast<char *>(s.c_str()); });
    args.push_back(nullptr);

    const int failure = posix_spawnp(&m_pid, args[0], &actions, &attributes, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(in[0]);
    close(out[1]);

    if (failure != 0)
    {
        close(in[1]);
        close(out[0]);
        m_pid = -1;
        return;
    }
]=])

string(FIND "${content}" "${spawned}" patched)

if(NOT patched EQUAL -1)
    return()
endif()

string(FIND "${content}" "${forked}" found)

if(found EQUAL -1)
    message(FATAL_ERROR "The dialog library no longer starts its helpers where the spawn patch expects")
endif()

string(REPLACE "${forked}" "${spawned}" content "${content}")
string(REPLACE "#include <unistd.h>   // read(), pipe(), dup2(), getuid()" "#include <unistd.h>   // read(), pipe(), dup2(), getuid()\n#include <spawn.h>\n#include <signal.h>\nextern char **environ;" content "${content}")
file(WRITE "${SOURCE}" "${content}")

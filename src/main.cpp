#include <CLI/ArgumentParser.h>
#include <Core/Bench.h>
#include <Core/File.h>
#include <Core/MappedFile.h>
#include <Core/System.h>
#include <Main/Main.h>
#include <Ty/StringBuffer.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#if __has_include(<sys/inotify.h>)
#define has_inotify 1
#include <sys/inotify.h>
#elif __has_include(<sys/event.h>)
#define has_kqueue 1
#include <sys/event.h>
#else
#error "unimplemented"
#endif


struct Executor {
    static Executor& the()
    {
        static auto self = Executor();
        return self;
    }

    Executor(Executor const&) = delete;

    bool is_done() const
    {
        return m_command == nullptr;
    }

    pid_t run() const
    {
        c_string argv[] = {
            "sh",
            "-c",
            m_command,
            nullptr,
        };
        return MUST(Core::System::posix_spawnp(argv[0], argv));
    }

    ErrorOr<void> run_killing(c_string command)
    {
        if (!is_done()) {
            kill(m_pid, SIGKILL);
            set_done();
        }
        m_command = command;

        pthread_t thread;
        pthread_create(&thread, nullptr, [](auto* context) -> void* {
            auto* self = (Executor*)context;
            self->m_pid = self->run();
            waitpid(self->m_pid, nullptr, 0);
            self->set_done();
            return nullptr;
        }, this);
        pthread_detach(thread);

        return {};
    }

private:
    Executor() = default;

    void set_done() { m_command = nullptr; }

    c_string m_command { nullptr };
    pid_t m_pid { -1 };
};

ErrorOr<int> Main::main(int argc, c_string argv[])
{
    auto argument_parser = CLI::ArgumentParser();

    c_string program_name = argv[0];
    TRY(argument_parser.add_flag("--help"sv, "-h"sv,
        "show help message"sv, [&] {
            argument_parser.print_usage_and_exit(program_name, 0);
        }));

    constexpr auto max_file_watches = 1024;
    auto files_to_watch
        = TRY(Vector<c_string>::create(max_file_watches));
    TRY(argument_parser.add_option("--file"sv, "-f"sv, "filename"sv,
        "file to watch (can be used multiple times)"sv,
        [&](auto path) {
            MUST(files_to_watch.append(path));
        }));

    c_string command = nullptr;
    TRY(argument_parser.add_positional_argument("command"sv,
        [&](auto argument) {
            command = argument;
        }));

    if (auto result = argument_parser.run(argc, argv);
        result.is_error()) {
        TRY(result.error().show());
        return 1;
    }

    if (files_to_watch.is_empty()) {
        return Error::from_string_literal("must watch at least one file");
    }

    TRY(Core::File::stderr().writeln("Files:"sv));
    for (auto const* file : files_to_watch) {
        TRY(Core::File::stderr().writeln("    "sv,
            StringView::from_c_string(file)));
    }
    TRY(Core::File::stderr().writeln("Command: "sv,
        StringView::from_c_string(command)));

#if has_inotify
    auto notifier = inotify_init();
    for (u32 i = 0; i < files_to_watch.size(); i++) {
        auto stat = TRY(Core::System::stat(files_to_watch[i]));
        if (!stat.is_regular())
            return Error::from_string_literal("can only watch regular files");
        auto fd = inotify_add_watch(notifier, files_to_watch[i], IN_CLOSE_WRITE);
        if (fd < 0)
            return Error::from_errno();
    }

    struct inotify_event event;
    while (true) {
        read(notifier, &event, sizeof(event));
        TRY(Executor::the().run_killing(command));
    }
#elif has_kqueue
    auto notifier = kqueue();
    auto events = TRY(Vector<struct kevent>::create(files_to_watch.size()));
    for (u32 i = 0; i < files_to_watch.size(); i++) {
        auto fd = TRY(Core::System::open(files_to_watch[i], O_RDONLY));
        events.unchecked_append({
            .ident = (uintptr_t)fd,
            .filter = EVFILT_VNODE,
            .flags = EV_ADD | EV_ONESHOT,
            .fflags = NOTE_WRITE,
            .udata = 0
        });
    }

    while (true) {
        if (kevent(notifier, events.data(), (int)events.size(), 0, 0, 0) < 0) {
            return Error::from_errno();
        }
        TRY(Executor::the().run_killing(command));
    }
#else
#error "unimplemented"
#endif
    return 0;
}

void* he_malloc(usize size) { return __builtin_malloc(size); }

void* he_realloc(void* ptr, usize size)
{
    return __builtin_realloc(ptr, size);
}

void* he_calloc(usize nmemb, usize size)
{
    return __builtin_calloc(nmemb, size);
}

void he_free(void* ptr) { __builtin_free(ptr); }

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

    bool print_header = true;
    TRY(argument_parser.add_flag("--no-header"sv, "-nh"sv,
        "don't print header"sv,
        [&]{
            print_header = false;
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

    if (print_header) {
        TRY(Core::File::stderr().writeln("Files:"sv));
        for (auto const* file : files_to_watch) {
            TRY(Core::File::stderr().writeln("    "sv,
                StringView::from_c_string(file)));
        }
        auto command_view = StringView::from_c_string(command) ?: "\"\""sv;
        TRY(Core::File::stderr().writeln("Command: "sv, command_view));
    }

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
        if (StringView::from_c_string(command).is_empty())
            return 0;
        waitpid(MUST(Core::System::posix_spawnp(argv[0], argv)), nullptr, 0);
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
        struct kevent event;
        if (kevent(notifier, events.data(), (int)events.size(), &event, 1, 0) < 0) {
            return Error::from_errno();
        }
        if ((event.flags & EV_ERROR) > 0) {
            return Error::from_errno((int)event.data);
        }
        if (StringView::from_c_string(command).is_empty())
            return 0;
        c_string argv[] = {
            "sh",
            "-c",
            command,
            nullptr,
        };
        waitpid(MUST(Core::System::posix_spawnp(argv[0], argv)), nullptr, 0);
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

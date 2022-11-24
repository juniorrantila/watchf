#include <CLI/ArgumentParser.h>
#include <Core/Bench.h>
#include <Core/File.h>
#include <Core/MappedFile.h>
#include <Core/System.h>
#include <Main/Main.h>
#include <Ty/StringBuffer.h>
#include <sys/inotify.h>
#include <stdlib.h>

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

    auto notifier = inotify_init();
    auto watch_fds = TRY(Vector<int>::create(files_to_watch.size()));
    for (u32 i = 0; i < files_to_watch.size(); i++) {
        auto stat = TRY(Core::System::stat(files_to_watch[i]));
        if (!stat.is_regular())
            return Error::from_string_literal("can only watch regular files");
        auto fd = inotify_add_watch(notifier, files_to_watch[i], IN_CLOSE_WRITE);
        if (fd < 0)
            return Error::from_errno();
        watch_fds.unchecked_append(fd);
    }

    TRY(Core::File::stderr().writeln("Files:"sv));
    for (auto const* file : files_to_watch) {
        TRY(Core::File::stderr().writeln("    "sv,
            StringView::from_c_string(file)));
    }
    TRY(Core::File::stderr().writeln("Command: "sv,
        StringView::from_c_string(command)));

    struct inotify_event event;
    while (true) {
        read(notifier, &event, sizeof(event));
        system(command);
    }

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

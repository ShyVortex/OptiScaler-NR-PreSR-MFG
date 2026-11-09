#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

// DLSS Unlocked #42: Neverness to Everness starts CrashClientReporter.exe from the game folder. It loads winmm.dll, so
// OptiScaler (and nvsmooth30.dll, and the Smooth Motion DRS write) ran inside it and overwrote the game's
// OptiScaler.log and nvsmooth30.log (FG-NVSMOOTH30-INTEGRATION.md 6.11). The default list only had
// crashreportclient.exe.
//
// Validates:
// 1. The default ProcessExclusionList contains crashclientreporter.exe.
// 2. Existing entries stay (crashreportclient.exe, crashpad_handler.exe, steamerrorreporter64.exe, ...).
// 3. Every entry is lower-case, ends in .exe and appears once; CheckForExcludedProcess compares the lower-cased exe
//    name to each '|'-separated entry.

static std::string ReadFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::fprintf(stderr, "cannot open %s (run from the repository root)\n", path);
        std::exit(1);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main()
{
    const auto header = ReadFile("OptiScaler/Config.h");
    const auto start = header.find("CustomOptional<std::wstring> ProcessExclusionList = {");
    assert(start != std::string::npos);
    const auto end = header.find("};", start);
    assert(end != std::string::npos);

    // Join the L"..." string-literal pieces into one list.
    std::string list;
    const std::regex piece("L\"([^\"]*)\"");
    const auto block = header.substr(start, end - start);
    for (std::sregex_iterator it(block.begin(), block.end(), piece), stop; it != stop; ++it)
        list += (*it)[1].str();

    std::set<std::string> entries;
    size_t count = 0;
    std::stringstream stream(list);
    for (std::string entry; std::getline(stream, entry, '|');)
    {
        if (entry.empty())
            continue;
        count++;
        entries.insert(entry);
        for (char c : entry)
            assert(!(c >= 'A' && c <= 'Z'));
        assert(entry.size() > 4 && entry.substr(entry.size() - 4) == ".exe");
    }

    // Test 1
    assert(entries.count("crashclientreporter.exe") == 1);
    std::puts("Test 1 PASS: crashclientreporter.exe is excluded by default");

    // Test 2
    for (const char* kept : { "crashreportclient.exe", "crashpad_handler.exe", "crashreporter.exe",
                              "steamerrorreporter64.exe", "unitycrashhandler64.exe", "edcefrenderprocess.exe" })
        assert(entries.count(kept) == 1);
    std::puts("Test 2 PASS: existing entries are kept");

    // Test 3
    assert(entries.size() == count);
    const auto dllmain = ReadFile("OptiScaler/dllmain.cpp");
    assert(dllmain.find("std::transform(exeLower.begin(), exeLower.end(), exeLower.begin(), ::towlower);") !=
           std::string::npos);
    std::puts("Test 3 PASS: entries are lower-case .exe names, each listed once");

    std::puts("All 3 process exclusion default tests passed.");
    return 0;
}

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

// The RTX 20/30 MFG unlocker (sdli1995 dlssg_for_sm86) is fetched by tag in both release paths. They must pin the
// same release, keep the modular verification, and keep pruning sr/ and xefg/ (architecture rule 4).
//
// Validates:
// 1. package_release.ps1 and package_release.yml fetch exactly one tag, the same one, currently 0.4.2.
// 2. No message still names a different 0.4.N release than the pinned one.
// 3. The script still requires backend / kernels / runtime and prunes sr / xefg.
// 4. The mod's MIT licence (sdli1995 0.4.2+) ships beside its DLLs: the source LICENSE.md when present, otherwise the
//    vendored Licenses/dlssg_for_sm86_LICENSE.md (the git tag CI fetches has no LICENSE.md).

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

static std::set<std::string> Matches(const std::string& text, const std::regex& pattern)
{
    std::set<std::string> found;
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it)
        found.insert((*it)[1].str());
    return found;
}

int main()
{
    const std::string script = ReadFile("package_release.ps1");
    const std::string workflow = ReadFile(".github/workflows/package_release.yml");
    const std::regex tag("origin tags/([0-9.]+)");

    // Test 1
    const auto scriptTags = Matches(script, tag);
    const auto workflowTags = Matches(workflow, tag);
    assert(scriptTags.size() == 1 && workflowTags.size() == 1);
    assert(scriptTags == workflowTags && "script and workflow must pin the same dlssg_for_sm86 release");
    const std::string pinned = *scriptTags.begin();
    assert(pinned == "0.4.2");
    std::printf("Test 1 PASS: both release paths pin dlssg_for_sm86 %s\n", pinned.c_str());

    // Test 2
    const std::regex release("(0\\.4\\.[0-9]+)");
    for (const auto* text : { &script, &workflow })
        for (const auto& version : Matches(*text, release))
            assert(version == pinned && "a message names a different release than the pinned tag");
    std::puts("Test 2 PASS: messages name the pinned release only");

    // Test 3
    assert(script.find("$components = @('backend', 'kernels', 'runtime')") != std::string::npos);
    assert(script.find("foreach ($ex in @('sr', 'xefg'))") != std::string::npos);
    for (const char* required : { "backend\\sm86_backend.dll", "kernels\\fg_kernels.dll", "runtime\\nvngx_dlssg.dll" })
    {
        assert(script.find(required) != std::string::npos);
        assert(workflow.find(required) != std::string::npos);
    }
    std::puts("Test 3 PASS: modular components required, sr/xefg pruned");

    // Test 4
    const std::string vendored = ReadFile("Licenses/dlssg_for_sm86_LICENSE.md");
    assert(vendored.find("MIT License") != std::string::npos);
    assert(vendored.find("Copyright (c) 2026 sdli1995") != std::string::npos);
    const size_t staged = script.find("$sm86DestDir\\LICENSE.md");
    const size_t source = script.find("$sm86Src\\LICENSE.md");
    const size_t fallback = script.find("$root\\Licenses\\dlssg_for_sm86_LICENSE.md");
    assert(staged != std::string::npos && "the mod's licence must be staged beside its DLLs");
    assert(source != std::string::npos && "the source folder's LICENSE.md must be preferred");
    assert(fallback != std::string::npos && "the vendored licence must be the fallback");
    assert(source < fallback && "the source LICENSE.md must be tried before the vendored copy");
    assert(script.find("throw \"Ampere/Turing SM86/SM75 licence not staged") != std::string::npos);
    std::puts("Test 4 PASS: the mod's MIT licence is staged (source first, vendored fallback)");

    std::puts("All SM86 packaging pin tests passed (4/4).");
    return 0;
}

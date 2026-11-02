#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

// DLSS Unlocked #42: on driver 621.01 NVSmooth30 v0.1.0 misses NvPresent's CUDA loader. The fix is upstream PR
// ItsAdeline/NVSmooth30#4 (unmerged), and packaging could only clone upstream's default branch. Packaging now takes an
// NVSmooth30 repository and ref; the default is unchanged and nothing is pinned (FG-NVSMOOTH30-INTEGRATION.md 6.7).
//
// Validates:
// 1. package_release.ps1 declares -NVSmooth30Repository (default upstream, GitHub-only pattern) and -NVSmooth30Ref
//    (default empty, restricted characters).
// 2. Without a ref the script keeps the local nvsmooth30\ path and the shallow clone of the repository's default
//    branch.
// 3. With a ref the script fetches exactly that ref into a fresh temp directory, checks out FETCH_HEAD, builds it,
//    and never uses $root\nvsmooth30 for it.
// 4. The staged package records the NVSmooth30 source (repository and commit, or local).
// 5. package_release.yml exposes both inputs, skips its own clone step when a ref is set, passes both values to the
//    script through the hashtable via env, and keeps actions/checkout@v6.
// 6. No commit SHA is hard-coded in either file.

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
    std::string text = buffer.str();
    std::string out;
    for (char c : text)
        if (c != '\r')
            out += c;
    return out;
}

static bool Has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

static std::string Between(const std::string& text, const std::string& from, const std::string& to)
{
    const auto start = text.find(from);
    assert(start != std::string::npos);
    const auto end = text.find(to, start + from.size());
    assert(end != std::string::npos);
    return text.substr(start, end - start);
}

int main()
{
    const auto script = ReadFile("package_release.ps1");
    const auto workflow = ReadFile(".github/workflows/package_release.yml");

    // Test 1
    assert(Has(script, "[ValidatePattern('^https://github\\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+(\\.git)?$')]\n"
                       "    [string]$NVSmooth30Repository = 'https://github.com/ItsAdeline/NVSmooth30.git',"));
    assert(Has(script, "[ValidatePattern('^$|^[A-Za-z0-9._/-]+$')]\n    [string]$NVSmooth30Ref = '',"));
    std::puts("Test 1 PASS: repository and ref parameters, defaults and validation");

    const auto block = Between(script, "if ($IncludeNVSmooth30) {\n    $cmake", "$checksumLines");
    const auto refPath = Between(block, "if ($NVSmooth30Ref) {", "    else {");
    const auto defaultPath =
        Between(block, "    else {", "if (-not (Test-Path -LiteralPath $nvSmoothDll)) {\n        throw");

    // Test 2
    assert(Has(defaultPath, "$nvSmoothSrc = \"$root\\nvsmooth30\""));
    assert(Has(defaultPath, "git clone --depth 1 $NVSmooth30Repository $nvSmoothSrc"));
    std::puts("Test 2 PASS: without a ref, the local checkout and the default-branch clone are unchanged");

    // Test 3
    assert(Has(refPath, "$nvSmoothSrc = Join-Path ([IO.Path]::GetTempPath()) (\"nvsmooth30-\" + "
                        "[Guid]::NewGuid().ToString('N'))"));
    assert(Has(refPath, "git -C $nvSmoothSrc fetch --depth 1 origin $NVSmooth30Ref"));
    assert(Has(refPath, "git -C $nvSmoothSrc checkout --quiet --detach FETCH_HEAD"));
    assert(Has(refPath, "& $cmake --build \"$nvSmoothSrc\\build\" --config Release"));
    assert(!Has(refPath, "$root\\nvsmooth30"));
    for (const char* step : { "init --quiet", "remote add origin", "fetch --depth 1", "checkout --quiet" })
    {
        const auto at = refPath.find(step);
        assert(at != std::string::npos);
        assert(refPath.find("if ($LASTEXITCODE -ne 0) { throw", at) != std::string::npos);
    }
    std::puts("Test 3 PASS: a ref is fetched and built in a fresh temp directory");

    // Test 4
    assert(Has(refPath, "$nvSmoothSource = \"$NVSmooth30Repository@$nvSmoothCommit (ref $NVSmooth30Ref)\""));
    assert(Has(defaultPath, "$nvSmoothSource = \"$NVSmooth30Repository@$nvSmoothCommit (default branch)\""));
    assert(Has(block, "Set-Content -LiteralPath \"$licenseDir\\SOURCE.nvsmooth30.txt\" -Value \"NVSmooth30 source: "
                      "$nvSmoothSource\""));
    std::puts("Test 4 PASS: the package records where NVSmooth30 came from");

    // Test 5
    assert(Has(workflow, "      nvsmooth30_repository:\n"));
    assert(Has(workflow, "        default: 'https://github.com/ItsAdeline/NVSmooth30.git'\n"));
    assert(Has(workflow, "      nvsmooth30_ref:\n"));
    assert(Has(workflow, "if: ${{ inputs.include_nvsmooth30 && inputs.nvsmooth30_ref == '' }}"));
    assert(Has(workflow, "NVSMOOTH30_REF: ${{ inputs.nvsmooth30_ref }}"));
    assert(Has(workflow, "$params[\"NVSmooth30Repository\"] = $env:NVSMOOTH30_REPOSITORY"));
    assert(Has(workflow, "$params[\"NVSmooth30Ref\"] = $env:NVSMOOTH30_REF"));
    assert(Has(workflow, "& .\\package_release.ps1 @params"));
    assert(Has(workflow, "uses: actions/checkout@v6"));
    std::puts("Test 5 PASS: workflow inputs reach the script through the hashtable");

    // Test 6
    const std::regex sha("[0-9a-f]{40}");
    assert(!std::regex_search(script, sha));
    assert(!std::regex_search(workflow, sha));
    std::puts("Test 6 PASS: no commit is pinned");

    std::puts("All 6 NVSmooth30 packaging source tests passed.");
    return 0;
}

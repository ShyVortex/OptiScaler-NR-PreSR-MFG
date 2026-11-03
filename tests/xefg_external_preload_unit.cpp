#include "../OptiScaler/proxies/XeFGPreloadPolicy.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

// Lost Soul Aside: OptiScaler's startup preload of its cross-vendor libxess_fg.dll replaced the game's Intel-only
// XeSS-FG 1.1.0.19, so the game's XeSS-FG wrapped the swapchain on a GeForce and Streamline's DLSS-G (driven by the
// external RTX 20/30 unlocker) never saw a present (FG-LSA-EXTERNAL-MFG-NO-FEATURE-CREATE.md).
//
// Validates:
// 1. External FG never preloads; every other mode does (OptiFG / XeMFG / menu keep their library).
// 2. CheckMemoryForProxies (dllmain.cpp) decides the libxess_fg preload through the policy, with no unconditional
//    XeFGProxy::InitXeFG() left in it, and logs the skip.
// 3. The other proxies in CheckMemoryForProxies are still preloaded unconditionally (no collateral change).
// 4. The External-mode volatile overrides are applied before CheckMemoryForProxies runs, so the flag is final.

static int g_failures = 0;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                               \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static std::string ReadSource(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::string FunctionBody(const std::string& source, const std::string& signature)
{
    const size_t start = source.find(signature);
    if (start == std::string::npos)
        return {};
    const size_t open = source.find('{', start);
    if (open == std::string::npos)
        return {};
    int depth = 0;
    for (size_t i = open; i < source.size(); ++i)
    {
        if (source[i] == '{')
            ++depth;
        else if (source[i] == '}' && --depth == 0)
            return source.substr(open, i - open + 1);
    }
    return {};
}

static void Test1_Policy()
{
    static_assert(!XeFGPreloadPolicy::ShouldPreloadAtStartup(true));
    static_assert(XeFGPreloadPolicy::ShouldPreloadAtStartup(false));
    CHECK(!XeFGPreloadPolicy::ShouldPreloadAtStartup(true));
    CHECK(XeFGPreloadPolicy::ShouldPreloadAtStartup(false));
    std::printf("Test 1 done\n");
}

static void Test2to4_Wiring()
{
    const std::string source = ReadSource("OptiScaler/dllmain.cpp");
    CHECK(!source.empty());
    if (source.empty())
    {
        std::printf("Tests 2-4 skipped: run from the repository root\n");
        return;
    }

    const std::string body = FunctionBody(source, "void CheckMemoryForProxies()");
    CHECK(!body.empty());

    // 2
    const size_t gate =
        body.find("if (XeFGPreloadPolicy::ShouldPreloadAtStartup(State::Instance().externalFrameGeneration))");
    const size_t init = body.find("XeFGProxy::InitXeFG();");
    CHECK(gate != std::string::npos);
    CHECK(init != std::string::npos && gate < init);
    CHECK(body.find("XeFGProxy::InitXeFG();", init + 1) == std::string::npos);
    CHECK(body.find("not preloading libxess_fg.dll") != std::string::npos);
    std::printf("Test 2 done\n");

    // 3
    for (const char* call : { "FfxApiProxy::InitFfxDx12();", "XeSSProxy::InitXeSS();", "XeSSProxy::InitXeSSDx11();",
                              "XeLLProxy::InitXeLL();", "NVNGXProxy::InitNVNGX();" })
        CHECK(body.find(call) != std::string::npos);
    std::printf("Test 3 done\n");

    // 4
    const size_t overrides = source.find("External frame generation: leaving Streamline/Reflex and MFG control");
    const size_t call = source.find("CheckMemoryForProxies();");
    CHECK(overrides != std::string::npos && call != std::string::npos && overrides < call);
    std::printf("Test 4 done\n");
}

int main()
{
    Test1_Policy();
    Test2to4_Wiring();

    if (g_failures != 0)
    {
        std::printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }

    std::printf("All 4 tests passed\n");
    return 0;
}

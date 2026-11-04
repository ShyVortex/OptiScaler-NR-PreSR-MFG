#include "../OptiScaler/hooks/StreamlineLogPrefs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// Lost Soul Aside never creates DLSS-G under the RTX 20/30 unlocker, and in External FG mode OptiScaler did not
// capture Streamline's log, so the reason was invisible (FG-LSA-EXTERNAL-MFG-NO-FEATURE-CREATE.md). External mode
// now installs a log-only slInit hook. It must change only the two logging fields (architecture rule 4: the game's
// Streamline, plugins and DLSS-G stay the game's).
//
// Validates:
// 1. Log level and callback are set; the game's callback is saved for chaining.
// 2. Plugin paths, features to load, engine and flags are byte-identical before and after.
// 3. A repeated apply on prefs that already carry our callback keeps the saved game callback.
// 4. A game without a callback leaves the saved callback null (nothing to chain).
// 5. External FG selects the log-only hook set; otherwise the full set.
// 6. Same rules on an SL1-shaped struct (different enum, callback and field layout).
// 7. Production wiring (Streamline_Hooks.cpp): External mode routes to hookInterposerLogOnly before any full hook;
//    the log-only attach detours only slInit, leaves State::streamlineVersion alone, and skips unknown versions; the
//    log-only slInit hooks touch nothing but the logging fields; the full hooks use the same helper.
// 8. Load-time notification decision: only External FG + a successful load of sl.interposer.dll (any case, bare name
//    or path) notifies; other modules, failed loads and non-External modes do not.
// 9. Production wiring of the notification: both ntdll load hooks notify only after the original loader succeeded,
//    OnOriginalLoad decides through the helper and does not set slInterposerModule, and the External skip in
//    LoadLibraryCheckW still leaves every sl.* load to the original loader.

namespace sl2
{
enum class LogLevel : uint32_t
{
    eOff,
    eDefault,
    eVerbose,
    eCount
};
enum class LogType : uint32_t
{
    eInfo,
    eWarn,
    eError
};
using PFun_LogMessageCallback = void(LogType, const char*);
using Feature = uint32_t;
enum class EngineType : uint32_t
{
    eCustom,
    eUnreal,
    eUnity
};

struct Preferences
{
    uint64_t flags = 0;
    const wchar_t** pathsToPlugins = nullptr;
    uint32_t numPathsToPlugins = 0;
    LogLevel logLevel = LogLevel::eDefault;
    PFun_LogMessageCallback* logMessageCallback = nullptr;
    const Feature* featuresToLoad = nullptr;
    uint32_t numFeaturesToLoad = 0;
    EngineType engine = EngineType::eCustom;
};
} // namespace sl2

namespace sl1
{
enum LogLevel : int
{
    eLogLevelOff,
    eLogLevelDefault,
    eLogLevelVerbose,
    eLogLevelCount
};
using pfunLogMessageCallback = void(int, const char*);

struct Preferences
{
    bool showConsole = false;
    LogLevel logLevel = eLogLevelDefault;
    const wchar_t** pathsToPlugins = nullptr;
    uint32_t numPathsToPlugins = 0;
    pfunLogMessageCallback* logMessageCallback = nullptr;
};
} // namespace sl1

static void GameCallback(sl2::LogType, const char*) {}
static void OurCallback(sl2::LogType, const char*) {}
static void GameCallback1(int, const char*) {}
static void OurCallback1(int, const char*) {}

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

static void Test1_SetsLoggingAndSavesGameCallback()
{
    sl2::Preferences prefs;
    prefs.logMessageCallback = &GameCallback;

    sl2::PFun_LogMessageCallback* saved = nullptr;
    StreamlineLogPrefs::ApplyLogCapture(prefs, &OurCallback, sl2::LogLevel::eCount, saved);

    CHECK(prefs.logLevel == sl2::LogLevel::eCount);
    CHECK(prefs.logMessageCallback == &OurCallback);
    CHECK(saved == &GameCallback);
    std::printf("Test 1 done\n");
}

static void Test2_OtherFieldsUntouched()
{
    static const wchar_t* paths[] = { L"D:\\Game\\Streamline", L"D:\\Game" };
    static const sl2::Feature features[] = { 0, 1000, 1001 };

    sl2::Preferences prefs;
    prefs.flags = 0x1234;
    prefs.pathsToPlugins = paths;
    prefs.numPathsToPlugins = 2;
    prefs.featuresToLoad = features;
    prefs.numFeaturesToLoad = 3;
    prefs.engine = sl2::EngineType::eUnreal;
    prefs.logMessageCallback = &GameCallback;

    sl2::Preferences before = prefs;

    sl2::PFun_LogMessageCallback* saved = nullptr;
    StreamlineLogPrefs::ApplyLogCapture(prefs, &OurCallback, sl2::LogLevel::eCount, saved);

    CHECK(prefs.flags == before.flags);
    CHECK(prefs.pathsToPlugins == before.pathsToPlugins);
    CHECK(prefs.numPathsToPlugins == before.numPathsToPlugins);
    CHECK(prefs.featuresToLoad == before.featuresToLoad);
    CHECK(prefs.numFeaturesToLoad == before.numFeaturesToLoad);
    CHECK(prefs.engine == before.engine);
    CHECK(std::memcmp(paths[0], L"D:\\Game\\Streamline", sizeof(L"D:\\Game\\Streamline")) == 0);
    CHECK(features[1] == 1000);
    std::printf("Test 2 done\n");
}

static void Test3_RepeatKeepsSavedGameCallback()
{
    sl2::Preferences prefs;
    prefs.logMessageCallback = &GameCallback;

    sl2::PFun_LogMessageCallback* saved = nullptr;
    StreamlineLogPrefs::ApplyLogCapture(prefs, &OurCallback, sl2::LogLevel::eCount, saved);
    StreamlineLogPrefs::ApplyLogCapture(prefs, &OurCallback, sl2::LogLevel::eCount, saved);

    CHECK(saved == &GameCallback);
    CHECK(saved != &OurCallback);
    std::printf("Test 3 done\n");
}

static void Test4_NoGameCallback()
{
    sl2::Preferences prefs;

    sl2::PFun_LogMessageCallback* saved = &GameCallback; // stale value from an earlier interposer
    StreamlineLogPrefs::ApplyLogCapture(prefs, &OurCallback, sl2::LogLevel::eCount, saved);

    CHECK(saved == nullptr);
    CHECK(prefs.logMessageCallback == &OurCallback);
    std::printf("Test 4 done\n");
}

static void Test5_HookSetSelection()
{
    using StreamlineLogPrefs::InterposerHookSet;
    static_assert(StreamlineLogPrefs::SelectHookSet(true) == InterposerHookSet::LogOnly);
    static_assert(StreamlineLogPrefs::SelectHookSet(false) == InterposerHookSet::Full);
    CHECK(StreamlineLogPrefs::SelectHookSet(true) == InterposerHookSet::LogOnly);
    CHECK(StreamlineLogPrefs::SelectHookSet(false) == InterposerHookSet::Full);
    std::printf("Test 5 done\n");
}

static void Test6_Sl1Shape()
{
    static const wchar_t* paths[] = { L"D:\\Game" };

    sl1::Preferences prefs;
    prefs.showConsole = true;
    prefs.pathsToPlugins = paths;
    prefs.numPathsToPlugins = 1;
    prefs.logMessageCallback = &GameCallback1;

    sl1::pfunLogMessageCallback* saved = nullptr;
    StreamlineLogPrefs::ApplyLogCapture(prefs, &OurCallback1, sl1::eLogLevelCount, saved);

    CHECK(prefs.logLevel == sl1::eLogLevelCount);
    CHECK(prefs.logMessageCallback == &OurCallback1);
    CHECK(saved == &GameCallback1);
    CHECK(prefs.showConsole);
    CHECK(prefs.pathsToPlugins == paths);
    CHECK(prefs.numPathsToPlugins == 1);
    std::printf("Test 6 done\n");
}

static std::string ReadSource(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static size_t CountOf(const std::string& text, const std::string& needle)
{
    size_t count = 0;
    for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + needle.size()))
        ++count;
    return count;
}

// Body of the function whose definition starts with `signature`, up to the matching closing brace.
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

static void Test7_ProductionWiring()
{
    const std::string source = ReadSource("OptiScaler/hooks/Streamline_Hooks.cpp");
    CHECK(!source.empty());
    if (source.empty())
    {
        std::printf("Test 7 skipped: run from the repository root\n");
        return;
    }

    const std::string hook = FunctionBody(source, "void StreamlineHooks::hookInterposer(HMODULE slInterposer)");
    CHECK(!hook.empty());
    const size_t route = hook.find("hookInterposerLogOnly(slInterposer);");
    const size_t select = hook.find("StreamlineLogPrefs::SelectHookSet(State::Instance().externalFrameGeneration)");
    const size_t firstFull = hook.find("DetourAttach");
    CHECK(select != std::string::npos);
    CHECK(route != std::string::npos);
    CHECK(firstFull == std::string::npos || route < firstFull);
    CHECK(hook.find("return;", route) != std::string::npos && hook.find("return;", route) < firstFull);

    const std::string logOnly =
        FunctionBody(source, "void StreamlineHooks::hookInterposerLogOnly(HMODULE slInterposer)");
    CHECK(!logOnly.empty());
    CHECK(CountOf(logOnly, "DetourAttach") == 2);
    CHECK(logOnly.find("DetourAttach(&(PVOID&) o_slInit_logOnly, hkslInit_logOnly);") != std::string::npos);
    CHECK(logOnly.find("DetourAttach(&(PVOID&) o_slInit_sl1_logOnly, hkslInit_sl1_logOnly);") != std::string::npos);
    CHECK(logOnly.find("Instance().streamlineVersion") == std::string::npos);
    CHECK(logOnly.find("streamlineVersion.major =") == std::string::npos);
    CHECK(logOnly.find("sl_version.major == 0") != std::string::npos);
    CHECK(logOnly.find("o_slInit =") == std::string::npos);
    CHECK(logOnly.find("o_slInit_sl1 =") == std::string::npos);

    for (const char* signature :
         { "sl::Result StreamlineHooks::hkslInit_logOnly(const sl::Preferences& pref, uint64_t sdkVersion)",
           "bool StreamlineHooks::hkslInit_sl1_logOnly(const sl1::Preferences& pref, int applicationId)" })
    {
        const std::string body = FunctionBody(source, signature);
        CHECK(!body.empty());
        CHECK(body.find("StreamlineLogPrefs::ApplyLogCapture(") != std::string::npos);
        CHECK(body.find("pathsToPlugins") == std::string::npos);
        CHECK(body.find("featuresToLoad") == std::string::npos);
        CHECK(body.find("reportEngineType") == std::string::npos);
        CHECK(body.find("gameQuirks") == std::string::npos);
        CHECK(body.find("flags") == std::string::npos);
    }

    const std::string full =
        FunctionBody(source, "sl::Result StreamlineHooks::hkslInit(const sl::Preferences& pref, uint64_t sdkVersion)");
    const std::string full1 =
        FunctionBody(source, "bool StreamlineHooks::hkslInit_sl1(const sl1::Preferences& pref, int applicationId)");
    CHECK(full.find("StreamlineLogPrefs::ApplyLogCapture(") != std::string::npos);
    CHECK(full1.find("StreamlineLogPrefs::ApplyLogCapture(") != std::string::npos);

    const std::string unhook = FunctionBody(source, "void StreamlineHooks::unhookInterposer()");
    CHECK(unhook.find("unhookInterposerLogOnly();") != std::string::npos);

    const std::string interposerHooked = source.substr(source.find("bool StreamlineHooks::isInterposerHooked()"), 120);
    CHECK(interposerHooked.find("logOnly") == std::string::npos);
    std::printf("Test 7 done\n");
}

static void Test8_NotifyDecision()
{
    using StreamlineLogPrefs::ShouldNotifyInterposerLoad;

    CHECK(ShouldNotifyInterposerLoad(true, L"sl.interposer.dll", true));
    CHECK(ShouldNotifyInterposerLoad(true, L"SL.Interposer.DLL", true));
    CHECK(ShouldNotifyInterposerLoad(
        true, L"D:\\SteamLibrary\\Lost Soul Aside\\Engine\\Plugins\\Streamline\\sl.interposer.dll", true));
    CHECK(ShouldNotifyInterposerLoad(true, L"D:/Game/Win64/sl.interposer.dll", true));

    CHECK(!ShouldNotifyInterposerLoad(false, L"sl.interposer.dll", true)); // other modes keep their own path
    CHECK(!ShouldNotifyInterposerLoad(true, L"sl.interposer.dll", false)); // failed load
    CHECK(!ShouldNotifyInterposerLoad(true, L"sl.common.dll", true));
    CHECK(!ShouldNotifyInterposerLoad(true, L"sl.dlss_g.dll", true));
    CHECK(!ShouldNotifyInterposerLoad(true, L"sl.interposer.dll.bak", true));
    CHECK(!ShouldNotifyInterposerLoad(true, L"xsl.interposer.dll", true));
    CHECK(
        !ShouldNotifyInterposerLoad(true, L"C:\\ProgramData\\NVIDIA\\NGX\\models\\sl_dlss_g_0\\190_E658703.dll", true));
    CHECK(!ShouldNotifyInterposerLoad(true, L"", true));
    std::printf("Test 8 done\n");
}

static void Test9_NotificationWiring()
{
    const std::string ntdll = ReadSource("OptiScaler/hooks/Ntdll_Hooks.h");
    const std::string load = ReadSource("OptiScaler/hooks/LibraryLoad_Hooks.cpp");
    CHECK(!ntdll.empty() && !load.empty());
    if (ntdll.empty() || load.empty())
    {
        std::printf("Test 9 skipped: run from the repository root\n");
        return;
    }

    for (const char* hook : { "static NTSTATUS NTAPI hkLdrLoadDll(", "static NTSTATUS NTAPI hkNtLoadDll(" })
    {
        const std::string body = FunctionBody(ntdll, hook);
        CHECK(!body.empty());
        const size_t original = body.rfind("const auto status = o_");
        const size_t success = body.find("if (NT_SUCCESS(status))", original);
        const size_t notify = body.find("LibraryLoadHooks::OnOriginalLoad(name, (HMODULE) *ModuleHandle);", success);
        CHECK(original != std::string::npos);
        CHECK(success != std::string::npos);
        CHECK(notify != std::string::npos);
        CHECK(CountOf(body, "OnOriginalLoad") == 1);
    }

    const std::string onLoad =
        FunctionBody(load, "void LibraryLoadHooks::OnOriginalLoad(std::wstring_view name, HMODULE module)");
    CHECK(!onLoad.empty());
    CHECK(onLoad.find("StreamlineLogPrefs::ShouldNotifyInterposerLoad(State::Instance().externalFrameGeneration") !=
          std::string::npos);
    CHECK(onLoad.find("StreamlineHooks::hookInterposer(module);") != std::string::npos);
    CHECK(onLoad.find("slInterposerModule =") == std::string::npos);
    CHECK(onLoad.find("optiSlInterposer") != std::string::npos);

    const std::string check =
        FunctionBody(load, "HMODULE LibraryLoadHooks::LoadLibraryCheckW(std::wstring libName, LPCWSTR lpLibFullPath)");
    CHECK(check.find("return nullptr; // not handled: preserve the original loader/unlocker's path") !=
          std::string::npos);
    std::printf("Test 9 done\n");
}

int main()
{
    Test1_SetsLoggingAndSavesGameCallback();
    Test2_OtherFieldsUntouched();
    Test3_RepeatKeepsSavedGameCallback();
    Test4_NoGameCallback();
    Test5_HookSetSelection();
    Test6_Sl1Shape();
    Test7_ProductionWiring();
    Test8_NotifyDecision();
    Test9_NotificationWiring();

    if (g_failures != 0)
    {
        std::printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }

    std::printf("All 9 tests passed\n");
    return 0;
}

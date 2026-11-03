#include "../OptiScaler/hooks/StreamlineLogPrefs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

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

int main()
{
    Test1_SetsLoggingAndSavesGameCallback();
    Test2_OtherFieldsUntouched();
    Test3_RepeatKeepsSavedGameCallback();
    Test4_NoGameCallback();
    Test5_HookSetSelection();
    Test6_Sl1Shape();

    if (g_failures != 0)
    {
        std::printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }

    std::printf("All 6 tests passed\n");
    return 0;
}

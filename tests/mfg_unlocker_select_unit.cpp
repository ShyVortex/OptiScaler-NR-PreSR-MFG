// DLSS FG/MFG unlocker selection ([DLSSG] MfgUnlock + MfgUnlocker): architecture detection from PCI ids, legacy
// key migration, resolution and what Save writes. Exercises the production header MfgUnlockerSelect.h, and checks
// that Config.cpp, OptiScaler.ini and package_release.ps1 use it.
//
// Build and run from the repository root:
//   g++ -std=c++20 -Wall -Wextra -o /tmp/mfg_unlocker_select_unit tests/mfg_unlocker_select_unit.cpp &&
//     /tmp/mfg_unlocker_select_unit

#include "../OptiScaler/framegen/dlssg/MfgUnlockerSelect.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace MfgUnlockerSelect;

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool condition, const std::string& name)
{
    g_checks++;
    if (!condition)
    {
        g_failures++;
        std::printf("FAIL: %s\n", name.c_str());
    }
}

static std::string Read(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

static bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

int main()
{
    // Test 1: device id classification, real ids (pci.ids / NVIDIA).
    {
        struct Sample
        {
            uint32_t id;
            NvArch arch;
            const char* name;
        };
        const Sample samples[] = {
            { 0x1E84, NvArch::Turing, "RTX 2070 Super (TU104)" },
            { 0x1F08, NvArch::Turing, "RTX 2060 (TU106)" },
            { 0x1F11, NvArch::Turing, "RTX 2060 Laptop (TU106)" },
            { 0x2182, NvArch::Turing, "GTX 1660 Ti (TU116)" },
            { 0x2204, NvArch::Ampere, "RTX 3090 (GA102)" },
            { 0x2460, NvArch::Ampere, "RTX 3080 Ti Laptop (GA103)" },
            { 0x2482, NvArch::Ampere, "RTX 3070 Ti (GA104)" },
            { 0x25A0, NvArch::Ampere, "RTX 3050 Ti Laptop (GA107)" },
            { 0x2684, NvArch::Ada, "RTX 4090 (AD102)" },
            { 0x2717, NvArch::Ada, "RTX 4090 Laptop (AD103)" },
            { 0x2803, NvArch::Ada, "RTX 4060 Ti (AD106)" },
            { 0x2882, NvArch::Ada, "RTX 4060 (AD107)" },
            { 0x2B85, NvArch::Other, "RTX 5090 (GB202)" },
            { 0x2C02, NvArch::Other, "RTX 5080 (GB203)" },
            { 0x1B80, NvArch::Other, "GTX 1080 (GP104)" },
            { 0x20B0, NvArch::Other, "A100 (GA100, sm_80)" },
            { 0x2330, NvArch::Other, "H100 (GH100)" },
        };
        for (const auto& sample : samples)
            Check(ClassifyNvidiaDeviceId(sample.id) == sample.arch, std::string("Test 1: ") + sample.name);
    }

    // Test 2: adapters combined.
    {
        Check(CombineAdapters({}) == NvArch::None, "Test 2: no adapter is no NVIDIA GPU");
        Check(CombineAdapters({ { 0x8086, 0x46A6 }, { 0x1414, 0x008C } }) == NvArch::None,
              "Test 2: Intel iGPU + WARP is no NVIDIA GPU");
        Check(CombineAdapters({ { 0x8086, 0x46A6 }, { 0x10DE, 0x2460 } }) == NvArch::Ampere,
              "Test 2: hybrid laptop picks the NVIDIA dGPU");
        Check(CombineAdapters({ { 0x10DE, 0x2684 }, { 0x10DE, 0x2B85 } }) == NvArch::Ada,
              "Test 2: a supported GPU next to an RTX 50 wins");
        Check(CombineAdapters({ { 0x10DE, 0x2B85 } }) == NvArch::Other, "Test 2: RTX 50 alone is Other");
        Check(CombineAdapters({ { 0x10DE, 0x2204 }, { 0x10DE, 0x2684 } }) == NvArch::Unknown,
              "Test 2: RTX 30 + RTX 40 is ambiguous");
        Check(CombineAdapters({ { 0x10DE, 0x1F08 }, { 0x10DE, 0x2204 } }) == NvArch::Unknown,
              "Test 2: RTX 20 + RTX 30 is ambiguous (different families)");
        Check(CombineAdapters({ { 0x10DE, 0x2204 }, { 0x10DE, 0x2204 } }) == NvArch::Ampere,
              "Test 2: two RTX 3090 stay Ampere");
    }

    // Test 3: PCI hardware id parsing (Wine and Windows registry key names).
    {
        const auto wine = ParsePciHardwareId("VEN_10DE&DEV_2460&SUBSYS_00000000&REV_00");
        Check(wine && wine->vendorId == 0x10DE && wine->deviceId == 0x2460, "Test 3: Wine Enum\\PCI key name");
        const auto prefixed = ParsePciHardwareId("PCI\\ven_10de&dev_2684&subsys_16f310de&rev_a1");
        Check(prefixed && prefixed->vendorId == 0x10DE && prefixed->deviceId == 0x2684,
              "Test 3: lower-case hardware id with PCI prefix");
        Check(!ParsePciHardwareId("VEN_10DE"), "Test 3: missing DEV_ is rejected");
        Check(!ParsePciHardwareId("VEN_10DE&DEV_24G0"), "Test 3: non-hex digits are rejected");
        Check(!ParsePciHardwareId("ROOT\\BASICDISPLAY"), "Test 3: non-PCI id is rejected");
    }

    // Test 4: MfgUnlocker parsing.
    {
        Check(ParseChoice("auto") == Choice::Auto, "Test 4: auto");
        Check(ParseChoice("ADA") == Choice::Ada && ParseChoice("rtx40") == Choice::Ada, "Test 4: ada aliases");
        Check(ParseChoice("turing-ampere") == Choice::TuringAmpere && ParseChoice("Ampere") == Choice::TuringAmpere &&
                  ParseChoice("turing") == Choice::TuringAmpere,
              "Test 4: turing-ampere aliases");
        Check(!ParseChoice("blackwell").has_value() && !ParseChoice("").has_value(), "Test 4: unknown text is unset");
        Check(ParseChoice(ChoiceName(Choice::Ada)) == Choice::Ada &&
                  ParseChoice(ChoiceName(Choice::TuringAmpere)) == Choice::TuringAmpere &&
                  ParseChoice(ChoiceName(Choice::Auto)) == Choice::Auto,
              "Test 4: ChoiceName round trips");
    }

    // Test 5: ini keys -> request (migration of the legacy switches).
    {
        auto r = FromIni(std::nullopt, std::nullopt, std::nullopt, std::nullopt);
        Check(!r.enabled && r.choice == Choice::Auto && !r.fromLegacyKeys, "Test 5: nothing set is off");

        r = FromIni(std::nullopt, std::nullopt, true, std::nullopt);
        Check(r.enabled && r.choice == Choice::Ada && r.fromLegacyKeys, "Test 5: AdaMfgUnlock=true migrates to ada");

        r = FromIni(std::nullopt, std::nullopt, false, true);
        Check(r.enabled && r.choice == Choice::TuringAmpere && r.fromLegacyKeys,
              "Test 5: AmpereMfgUnlock=true migrates to turing-ampere");

        r = FromIni(std::nullopt, std::nullopt, true, true);
        Check(r.enabled && r.choice == Choice::Auto, "Test 5: both legacy keys true leave the choice to the GPU");

        r = FromIni(false, std::nullopt, true, true);
        Check(!r.enabled && !r.fromLegacyKeys, "Test 5: MfgUnlock=false wins over legacy keys");

        r = FromIni(true, Choice::Ada, std::nullopt, true);
        Check(r.enabled && r.choice == Choice::Ada, "Test 5: MfgUnlock + MfgUnlocker win over AmpereMfgUnlock");

        r = FromIni(std::nullopt, Choice::Auto, true, std::nullopt);
        Check(r.enabled && r.choice == Choice::Auto, "Test 5: an explicit MfgUnlocker is kept on migration");
    }

    // Test 6: resolution, Auto.
    {
        Check(Resolve(true, Choice::Auto, NvArch::Ada).owner == Owner::Ada, "Test 6: auto on RTX 40 -> ada");
        Check(Resolve(true, Choice::Auto, NvArch::Ampere).owner == Owner::TuringAmpere,
              "Test 6: auto on RTX 30 -> turing-ampere");
        Check(Resolve(true, Choice::Auto, NvArch::Turing).owner == Owner::TuringAmpere,
              "Test 6: auto on RTX 20 -> turing-ampere");
        for (auto arch : { NvArch::None, NvArch::Other, NvArch::Unknown })
        {
            const auto res = Resolve(true, Choice::Auto, arch);
            Check(res.owner == Owner::None && !res.reason.empty() && !res.choiceMismatch,
                  std::string("Test 6: auto starts nothing on ") + ArchName(arch));
        }
        Check(Resolve(false, Choice::Auto, NvArch::Ada).owner == Owner::None, "Test 6: disabled starts nothing");
    }

    // Test 7: resolution, explicit choice.
    {
        Check(Resolve(true, Choice::Ada, NvArch::Ada).owner == Owner::Ada, "Test 7: ada on RTX 40");
        Check(Resolve(true, Choice::TuringAmpere, NvArch::Turing).owner == Owner::TuringAmpere,
              "Test 7: turing-ampere on RTX 20");

        auto res = Resolve(true, Choice::TuringAmpere, NvArch::Ada);
        Check(res.owner == Owner::None && res.choiceMismatch,
              "Test 7: turing-ampere on RTX 40 starts nothing (no External FG forced)");

        res = Resolve(true, Choice::Ada, NvArch::Ampere);
        Check(res.owner == Owner::None && res.choiceMismatch, "Test 7: ada on RTX 30 starts nothing");

        res = Resolve(true, Choice::Ada, NvArch::None);
        Check(res.owner == Owner::None && res.choiceMismatch, "Test 7: ada without NVIDIA starts nothing");

        res = Resolve(true, Choice::TuringAmpere, NvArch::Unknown);
        Check(res.owner == Owner::TuringAmpere && !res.choiceMismatch,
              "Test 7: a manual choice applies when detection failed");
    }

    // Test 8: what Save writes.
    {
        auto s = ToSaved(false, false, false, Choice::Auto, Owner::None, NvArch::Ampere);
        Check(!s.enabled && s.choice == Choice::Auto, "Test 8: off stays off");

        s = ToSaved(false, true, true, Choice::Auto, Owner::TuringAmpere, NvArch::Ampere);
        Check(s.enabled && s.choice == Choice::Auto, "Test 8: auto stays auto while its flag is on");

        s = ToSaved(false, true, false, Choice::Auto, Owner::None, NvArch::Ampere);
        Check(s.enabled && s.choice == Choice::Auto, "Test 8: menu turned on the RTX 20/30 flag on RTX 30 -> auto");

        s = ToSaved(true, false, true, Choice::Auto, Owner::Ada, NvArch::Ada);
        Check(s.enabled && s.choice == Choice::Auto, "Test 8: RTX 40 auto stays auto");

        s = ToSaved(false, true, false, Choice::Auto, Owner::None, NvArch::Unknown);
        Check(s.enabled && s.choice == Choice::TuringAmpere,
              "Test 8: a flag Auto would not pick is saved as the explicit choice");

        s = ToSaved(false, false, true, Choice::Auto, Owner::TuringAmpere, NvArch::Ampere);
        Check(!s.enabled, "Test 8: menu turned off a running unlocker");

        s = ToSaved(false, false, true, Choice::Auto, Owner::None, NvArch::None);
        Check(s.enabled && s.choice == Choice::Auto, "Test 8: a setting that could not apply here (no NVIDIA) is kept");

        s = ToSaved(false, false, true, Choice::Ada, Owner::None, NvArch::Ampere);
        Check(s.enabled && s.choice == Choice::Ada, "Test 8: a mismatched manual choice is kept");
    }

    // Test 9: NVAPI architecture ids for the startup cross-check.
    {
        Check(FromNvapiArchitecture(0x160) == NvArch::Turing && FromNvapiArchitecture(0x170) == NvArch::Ampere &&
                  FromNvapiArchitecture(0x190) == NvArch::Ada && FromNvapiArchitecture(0x1B0) == NvArch::Other,
              "Test 9: NV_GPU_ARCHITECTURE_* ids");
    }

    // Test 10: production wiring.
    {
        const auto config = Read("OptiScaler/Config.cpp");
        Check(Contains(config, "readBool(\"DLSSG\", \"MfgUnlock\")") &&
                  Contains(config, "readString(\"DLSSG\", \"MfgUnlocker\")"),
              "Test 10: Config.cpp reads MfgUnlock and MfgUnlocker");
        Check(Contains(config, "MfgUnlockerSelect::FromIni(") && Contains(config, "MfgUnlockerSelect::Resolve(") &&
                  Contains(config, "EarlyGpuArch::Get()"),
              "Test 10: Config.cpp resolves through MfgUnlockerSelect and EarlyGpuArch");
        Check(Contains(config,
                       "FGDLSSGAdaMfgUnlock.set_from_config(resolution.owner == MfgUnlockerSelect::Owner::Ada)") &&
                  Contains(config, "FGDLSSGAmpereMfgUnlock.set_from_config(resolution.owner == "
                                   "MfgUnlockerSelect::Owner::TuringAmpere)"),
              "Test 10: the resolved owner sets the existing unlocker flags");
        Check(Contains(config, "MfgUnlockerSelect::ToSaved(") &&
                  Contains(config, "ini.SetValue(\"DLSSG\", \"MfgUnlock\"") &&
                  Contains(config, "ini.SetValue(\"DLSSG\", \"AdaMfgUnlock\", \"auto\")") &&
                  Contains(config, "ini.SetValue(\"DLSSG\", \"AmpereMfgUnlock\", \"auto\")"),
              "Test 10: Save writes the new keys and the legacy keys as auto");

        const auto ini = Read("OptiScaler.ini");
        Check(Contains(ini, "\nMfgUnlock=auto") && Contains(ini, "\nMfgUnlocker=auto") &&
                  Contains(ini, "\nAdaMfgUnlock=auto") && Contains(ini, "\nAmpereMfgUnlock=auto"),
              "Test 10: OptiScaler.ini documents MfgUnlock / MfgUnlocker and ships everything off");

        const auto package = Read("package_release.ps1");
        Check(Contains(package, "'MfgUnlock'") && Contains(package, "'AdaMfgUnlock'") &&
                  Contains(package, "'AmpereMfgUnlock'"),
              "Test 10: packaging refuses a package with an unlocker switched on");

        const auto dllmain = Read("OptiScaler/dllmain.cpp");
        Check(Contains(dllmain, "MfgUnlockerSelect::FromNvapiArchitecture(") &&
                  Contains(dllmain, "mfgUnlockerArchMismatch = true"),
              "Test 10: the GPU worker cross-checks the startup detection");
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

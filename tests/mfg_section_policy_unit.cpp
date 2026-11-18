// The "DLSS Frame Generation" menu section (MfgSectionPolicy.h): lock rules, greyed-out unlocker entries, notes,
// restart hint and the flags written after a change. Also checks how menu_common.cpp wires it.
//
// Build and run from the repository root:
//   g++ -std=c++20 -Wall -Wextra -IOptiScaler -o /tmp/mfg_section_policy_unit tests/mfg_section_policy_unit.cpp &&
//     /tmp/mfg_section_policy_unit

#include "../OptiScaler/menu/MfgSectionPolicy.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace MfgSectionPolicy;

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

static size_t Find(const std::string& text, const std::string& part) { return text.find(part); }

static Inputs Gpu(NvArch arch, uint32_t nvapiArch = 0)
{
    Inputs in;
    in.startupArch = arch;
    in.primaryIsNvidia = arch != NvArch::None && arch != NvArch::Unknown;
    in.primaryNvapiArch = nvapiArch;
    return in;
}

constexpr size_t kAuto = static_cast<size_t>(Choice::Auto);
constexpr size_t kAda = static_cast<size_t>(Choice::Ada);
constexpr size_t kTa = static_cast<size_t>(Choice::TuringAmpere);

int main()
{
    // Test 1: locks.
    {
        auto view = Resolve(Gpu(NvArch::None));
        Check(view.locked && !view.lockReason.empty() && !view.enableInteractive,
              "Test 1: no NVIDIA GPU locks the section");
        Check(!view.choiceSelectable[kAuto] && !view.choiceSelectable[kAda] && !view.choiceSelectable[kTa],
              "Test 1: locked section offers no unlocker");

        view = Resolve(Gpu(NvArch::Other, 0x1B0));
        Check(view.locked && view.lockReason.find("RTX 50") != std::string::npos,
              "Test 1: RTX 50 is locked with the native-MFG note");

        view = Resolve(Gpu(NvArch::Other, 0x130));
        Check(view.locked && view.lockReason.find("RTX 50") == std::string::npos,
              "Test 1: an older NVIDIA GPU is locked with the RTX 20+ note");

        for (auto arch : { NvArch::Turing, NvArch::Ampere, NvArch::Ada, NvArch::Unknown })
            Check(!Resolve(Gpu(arch)).locked,
                  std::string("Test 1: not locked on ") + MfgUnlockerSelect::ArchName(arch));
    }

    // Test 2: a locked section still lets a saved setting be turned off.
    {
        auto in = Gpu(NvArch::None);
        in.configuredEnabled = true;
        const auto view = Resolve(in);
        Check(view.locked && view.enableInteractive, "Test 2: enabled while locked can be unchecked");
    }

    // Test 3: greyed-out entries follow the GPU family.
    {
        auto view = Resolve(Gpu(NvArch::Ampere, 0x170));
        Check(view.choiceSelectable[kAuto] && !view.choiceSelectable[kAda] && view.choiceSelectable[kTa],
              "Test 3: RTX 30 greys out RTX 40");
        Check(view.autoLabel.find("RTX 20 / 30") != std::string::npos, "Test 3: Auto names the RTX 20/30 unlocker");

        view = Resolve(Gpu(NvArch::Turing, 0x160));
        Check(!view.choiceSelectable[kAda] && view.choiceSelectable[kTa], "Test 3: RTX 20 greys out RTX 40");

        view = Resolve(Gpu(NvArch::Ada, 0x190));
        Check(view.choiceSelectable[kAda] && !view.choiceSelectable[kTa], "Test 3: RTX 40 greys out RTX 20/30");
        Check(view.autoLabel.find("RTX 40") != std::string::npos, "Test 3: Auto names the RTX 40 unlock");

        view = Resolve(Gpu(NvArch::Unknown));
        Check(view.choiceSelectable[kAda] && view.choiceSelectable[kTa] &&
                  view.autoLabel.find("not identified") != std::string::npos,
              "Test 3: unknown GPU leaves both choosable and says so");

        auto in = Gpu(NvArch::Ada, 0x190);
        in.adaBuilt = false;
        view = Resolve(in);
        Check(!view.choiceSelectable[kAda], "Test 3: RTX 40 entry greyed out when the build lacks it");
    }

    // Test 4: the menu uses IdentifyGpu when startup detection could not tell.
    {
        Inputs in;
        in.startupArch = NvArch::Unknown;
        in.primaryIsNvidia = true;
        in.primaryNvapiArch = 0x190;
        const auto view = Resolve(in);
        Check(view.arch == NvArch::Ada && view.choiceSelectable[kAda] && !view.choiceSelectable[kTa],
              "Test 4: IdentifyGpu fills in an unknown startup architecture");

        in.configuredEnabled = true;
        in.configuredChoice = Choice::Auto;
        Check(Resolve(in).configuredOwner == Owner::None,
              "Test 4: Auto still follows the startup detection, as config load will");
        Check(!Resolve(in).choiceNote.empty(), "Test 4: and the menu asks for a manual choice");

        in.configuredChoice = Choice::Ada;
        Check(Resolve(in).configuredOwner == Owner::Ada, "Test 4: a manual RTX 40 choice applies");
    }

    // Test 5: configured owner, notes and restart hint.
    {
        auto in = Gpu(NvArch::Ampere, 0x170);
        in.configuredEnabled = true;
        auto view = Resolve(in);
        Check(view.configuredOwner == Owner::TuringAmpere && view.restartNeeded,
              "Test 5: enabling on RTX 30 asks for a restart");

        in.sessionOwner = Owner::TuringAmpere;
        view = Resolve(in);
        Check(!view.restartNeeded && view.choiceNote.empty(), "Test 5: running as configured: no hint, no note");

        in.configuredEnabled = false;
        view = Resolve(in);
        Check(view.configuredOwner == Owner::None && view.restartNeeded, "Test 5: turning it off asks for a restart");

        in = Gpu(NvArch::Ampere, 0x170);
        in.configuredEnabled = true;
        in.configuredChoice = Choice::Ada;
        view = Resolve(in);
        Check(view.configuredOwner == Owner::None && view.choiceNote.find("does not match") != std::string::npos,
              "Test 5: a mismatched saved choice is explained");

        in = Gpu(NvArch::Ada, 0x190);
        in.configuredEnabled = true;
        in.adaBuilt = false;
        view = Resolve(in);
        Check(view.configuredOwner == Owner::None && view.choiceNote.find("build") != std::string::npos,
              "Test 5: RTX 40 not in this build starts nothing and says why");
    }

    // Test 6: XeMFG blocks turning the unlocker on, never off.
    {
        auto in = Gpu(NvArch::Ada, 0x190);
        in.xeMfgActive = true;
        auto view = Resolve(in);
        Check(!view.enableInteractive && view.enableBlockedReason.find("XeMFG") != std::string::npos,
              "Test 6: XeMFG active disables enabling");

        in.configuredEnabled = true;
        view = Resolve(in);
        Check(view.enableInteractive, "Test 6: an enabled unlocker can still be turned off");
    }

    // Test 7: RTX 40 unlock in an External FG session.
    {
        auto in = Gpu(NvArch::Ada, 0x190);
        in.configuredEnabled = true;
        in.sessionExternal = true;
        Check(Resolve(in).adaBlockedByExternal, "Test 7: External FG session warns for the RTX 40 unlock");
        in.sessionExternal = false;
        Check(!Resolve(in).adaBlockedByExternal, "Test 7: no warning without External FG");
    }

    // Test 8: flags after a menu change, never both.
    {
        Check(FlagsFor(Owner::Ada).ada && !FlagsFor(Owner::Ada).turingAmpere, "Test 8: RTX 40 flag only");
        Check(!FlagsFor(Owner::TuringAmpere).ada && FlagsFor(Owner::TuringAmpere).turingAmpere,
              "Test 8: RTX 20/30 flag only");
        Check(!FlagsFor(Owner::None).ada && !FlagsFor(Owner::None).turingAmpere, "Test 8: no flag");
    }

    // Test 9: menu wiring.
    {
        const auto menu = Read("OptiScaler/menu/menu_common.cpp");
        const auto header = Find(menu, "ImGui::CollapsingHeader(\"DLSS Frame Generation\")");
        const auto enable = Find(menu, "\"Enable DLSS FG / MFG unlocker (restart)##dlssg\"");
        const auto combo = Find(menu, "ImGui::BeginCombo(\"Unlocker##dlssg\"");
        const auto ada = Find(menu, "SeparatorWithHelpMarker(\"RTX 40 (Ada)\"");
        const auto ampere = Find(menu, "SeparatorWithHelpMarker(\"RTX 20 / 30 (Turing / Ampere)\"");
        const auto ampereStatus = Find(menu, "AmpereMfgStatusText::HeadlineText(status)");
        const auto mark = Find(menu, "MarkMenuSection(\"FG: DLSS FG unlocker\");");
        const auto xemfg = Find(menu, "ImGui::CollapsingHeader(\"Intel Xe Multi-Frame Generation (XeMFG)\")");
        const auto earlyReturn = Find(menu, "if (state.externalFrameGeneration)\n");

        Check(header != std::string::npos && enable != std::string::npos && combo != std::string::npos &&
                  ada != std::string::npos && ampere != std::string::npos && mark != std::string::npos,
              "Test 9: the section, switch, combo and both sub-blocks exist");
        Check(header < enable && enable < combo && combo < ada && ada < ampere && ampere < ampereStatus &&
                  ampereStatus < mark && mark < xemfg,
              "Test 9: order: header, switch, combo, RTX 40, RTX 20/30 (with its status), width mark, XeMFG");
        Check(earlyReturn != std::string::npos && mark < earlyReturn,
              "Test 9: the section sits before the External FG early return (rule 3)");
        Check(Find(menu, "CollapsingHeader(\"RTX 20 / 30 (SM75 / SM86) MFG Unlock\")") == std::string::npos &&
                  Find(menu, "\"RTX 40 MFG unlock (restart)\"") == std::string::npos &&
                  Find(menu, "\"Enable SM86/SM75 MFG (experimental; restart)##ampere\"") == std::string::npos,
              "Test 9: the old separate checkboxes and header are gone");
        Check(Find(menu, "MarkMenuSection(\"FG: RTX 40 unlock\")") == std::string::npos &&
                  Find(menu, "MarkMenuSection(\"FG: RTX 20/30 unlock\")") == std::string::npos,
              "Test 9: one width mark replaces the two old ones");
        Check(Find(menu, "Disable AdaMfgUnlock first") == std::string::npos &&
                  Find(menu, "Disable AmpereMfgUnlock first") == std::string::npos &&
                  Find(menu, "disable Ampere SM86 MFG below first") == std::string::npos,
              "Test 9: help texts point to the new section, not the legacy keys");
        Check(Find(menu, "MfgSectionPolicy::Resolve(mfgInputs)") != std::string::npos &&
                  Find(menu, "MfgSectionPolicy::FlagsFor(owner)") != std::string::npos,
              "Test 9: the menu decides through MfgSectionPolicy");

        const auto xeEnable = Find(menu, "config->XeMfgUnlock = xeUnlock;");
        const auto xeClears = Find(menu, "config->FGDLSSGMfgUnlock = false;");
        Check(xeEnable != std::string::npos && xeClears != std::string::npos && xeEnable < xeClears,
              "Test 9: enabling XeMFG turns the DLSS FG unlocker switch off");

        // The RTX 20/30 controls moved unchanged.
        for (const char* control :
             { "\"Keep game XeSS FG off the swapchain##sm86\"", "\"Ampere MFG Mode##sm86\"",
               "\"Max Multiplier Ceiling##sm86\"", "\"Optimized Kernels##sm86\"", "\"UI Recomposition Preset##sm86\"",
               "\"Kernel Image##sm86\"", "\"Router Architecture##sm86\"", "AmpereMfgStatusText::HardwareBilinearLabel",
               "\"Dynamic Multi-Frame Generation##sm86\"", "\"Game Arch Spoofing##sm86\"",
               "\"Mod Logging Level##sm86\"", "\"Linux FG Fallback Mode##sm86\"", "\"Fallback Pipeline##sm86\"" })
        {
            const auto at = Find(menu, control);
            Check(at != std::string::npos && ampere < at && at < mark,
                  std::string("Test 9: RTX 20/30 control inside the sub-block: ") + control);
        }

        const auto blackwell = Find(menu, "\"Retarget Blackwell kernels (faster 3X+ MFG, restart)\"");
        Check(blackwell != std::string::npos && ada < blackwell && blackwell < ampere,
              "Test 9: RTX 40 control inside its sub-block");
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

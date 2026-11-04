#pragma once

// At startup OptiScaler preloads its own libxess_fg.dll (XeFGProxy::InitXeFG in CheckMemoryForProxies). A game that
// later asks for libxess_fg.dll by name gets that already-loaded module instead of its own copy. OptiScaler's build is
// cross-vendor, so a game whose shipped XeSS-FG only runs on Intel (Lost Soul Aside ships 1.1.0.19) suddenly runs
// XeSS-FG on a GeForce, which wraps the swapchain; Streamline's DLSS-G then never sees a present and the external
// RTX 20/30 unlocker generates nothing (FG-LSA-EXTERNAL-MFG-NO-FEATURE-CREATE.md).
//
// In External FG mode OptiScaler's own XeFG is never created and XeMFG is off by definition
// (XeMfgLoader::EnabledForSession), so nothing needs the preload. Other modes keep it: OptiFG / XeMFG output and the
// menu rely on it. On-demand InitXeFG() calls (menu, XeFG_Dx12) are unaffected.
namespace XeFGPreloadPolicy
{
constexpr bool ShouldPreloadAtStartup(bool externalFrameGeneration) { return !externalFrameGeneration; }
} // namespace XeFGPreloadPolicy

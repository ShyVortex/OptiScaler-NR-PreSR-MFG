#pragma once

#include <nvsdk_ngx_params.h>

struct ID3D12GraphicsCommandList;

// Owns the game's own NR features (NGX feature 18) created through the direct runtime after the driver refused them
// (DlssNr_GameNr.h decides when). Handles are keyed by pointer: their ids come from nvngx_dlssnr.dll and may collide
// with driver handle ids, so these calls are routed before any id-based lookup.
namespace DlssNr::GameNr
{
NVSDK_NGX_Result CreateDirect(ID3D12GraphicsCommandList* commands, NVSDK_NGX_Parameter* params,
                              NVSDK_NGX_Handle** handle);
bool Owns(const NVSDK_NGX_Handle* handle);
NVSDK_NGX_Result Evaluate(ID3D12GraphicsCommandList* commands, const NVSDK_NGX_Handle* handle,
                          NVSDK_NGX_Parameter* params);
NVSDK_NGX_Result Release(NVSDK_NGX_Handle* handle);
// True while the game has a live NR feature; OptiScaler's own NR yields to it.
bool Active();
// NGX shutdown: release what the game left behind and drop the retained runtimes.
void Shutdown();
} // namespace DlssNr::GameNr

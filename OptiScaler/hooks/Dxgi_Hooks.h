#pragma once
#include "SysUtils.h"

class DxgiHooks
{
  private:
    static inline std::mutex hookMutex;

  public:
    static void Hook();

    /// True once CreateDXGIFactory, CreateDXGIFactory1 and CreateDXGIFactory2 are hooked.
    static bool FactoryHooksInstalled();
};

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// The startup splash (bottom-left at every launch) names the product as DLSS Unlocked, matching VER_PRODUCT_NAME.
//
// Validates:
// 1. RenderSplashWindow prints "DLSS Unlocked - <key> for menu".
// 2. The old "OptiScaler - %s for menu" line is gone, and the random splash sentence still follows it.

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

int main()
{
    const auto menu = ReadFile("OptiScaler/menu/menu_common.cpp");
    const auto splash = menu.find("void MenuCommon::RenderSplashWindow(");
    assert(splash != std::string::npos);

    // Test 1
    const auto line = menu.find("ImGui::Text(\"DLSS Unlocked - %s for menu\",", splash);
    assert(line != std::string::npos);
    std::puts("Test 1 PASS: the splash names DLSS Unlocked");

    // Test 2
    assert(menu.find("\"OptiScaler - %s for menu\"") == std::string::npos);
    assert(menu.find("splashMessage.c_str()", line) != std::string::npos);
    std::puts("Test 2 PASS: the old line is gone and the random sentence follows");

    std::puts("All 2 splash branding tests passed.");
    return 0;
}

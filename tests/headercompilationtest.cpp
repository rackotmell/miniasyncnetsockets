/// @file headercompilationtest.cpp
/// @brief Smoke test: verifies that the public umbrella header compiles and is self-contained.

#include <miniasyncnetsockets/miniasyncnetsockets.hpp>

int main()
{
    const miniasyncnetsockets::ServerOptions options{};
    return options.maxFrameSize == 0U ? 1 : 0;
}

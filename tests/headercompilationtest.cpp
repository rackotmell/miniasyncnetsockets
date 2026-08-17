#include <miniasyncnetsockets/miniasyncnetsockets.hpp>

int main()
{
    const miniasyncnetsockets::ServerOptions options{};
    return options.maxFrameSize == 0U ? 1 : 0;
}

#include "../src/clipboard/quick_input.h"
#include <cstdio>

using lumashot::clipboard::QuickKeyRouter;
int main() {
    int failures = 0;
    const auto check = [&failures](bool ok, const char* name) {
        if (!ok) { std::printf("FAIL: %s\n", name); ++failures; }
    };
    QuickKeyRouter router;
    router.Start();
    auto result = router.Route(VK_DOWN, true, false, false, false);
    check(result.consume && result.notify, "navigation down");
    result = router.Route(VK_DOWN, true, false, false, false);
    check(result.consume && result.notify, "navigation repeat");
    result = router.Route(VK_DOWN, false, false, false, false);
    check(result.consume && !result.notify && !router.Pending(), "navigation release");
    result = router.Route(VK_RETURN, true, false, true, false);
    check(result.consume && !result.notify, "shift enter down");
    result = router.Route(VK_RETURN, true, false, false, false);
    check(result.consume && !result.notify, "enter repeat no commit");
    result = router.Route(VK_RETURN, false, false, false, false);
    check(result.consume && result.notify && result.shift, "shift snapshot retained on release");
    for (const UINT key : {UINT(VK_ESCAPE), UINT(VK_F2)}) {
        result = router.Route(key, true, false, false, false);
        check(result.consume && !result.notify, "release action down");
        result = router.Route(key, false, false, false, false);
        check(result.consume && result.notify, "release action up");
    }
    router.Route(VK_RETURN, true, false, false, false);
    router.Stop();
    result = router.Route(VK_RETURN, false, true, false, false);
    check(!result.consume && router.Pending(), "injected release cannot discharge physical key");
    result = router.Route(VK_RETURN, true, false, false, false);
    check(result.consume && !result.notify, "stopped repeat drained");
    result = router.Route(VK_RETURN, false, false, false, false);
    check(result.consume && !result.notify && !router.Pending(), "stopped release drained");
    check(!router.Route(VK_DOWN, true, false, false, false).consume, "stopped new down passes");
    router.Start();
    check(!router.Route(VK_DOWN, true, true, false, false).consume && !router.Pending(), "injected down passes");
    check(!router.Route(VK_DOWN, true, false, false, true).consume, "ctrl alt win passes");
    check(!router.Route(VK_DOWN, true, false, true, false).consume, "shift navigation passes");
    check(!router.Route(VK_F5, true, false, false, false).consume, "unrelated function key passes");
    result = router.Route('V', true, false, false, true);
    check(!result.consume && !result.dismiss && router.Active(), "shortcut does not dismiss");
    result = router.Route('A', true, false, true, false);
    check(!result.consume && result.dismiss && !router.Active(), "shift character dismisses and passes");
    router.Start();
    result = router.Route('A', true, false, false, false);
    check(!result.consume && result.dismiss && !router.Active(), "character dismisses and passes");
    check(!router.Route(VK_RETURN, false, false, false, false).consume, "unmatched release passes");
    std::printf("Quick input: %s\n", failures ? "FAILED" : "PASS");
    return failures ? 1 : 0;
}

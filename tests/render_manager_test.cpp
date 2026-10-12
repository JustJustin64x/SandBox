#include "render_manager.h"
#include "rlgl.h"
#include <cmath>
#include <cstdio>

// Integration test against a real OpenGL framebuffer, including the host DPI.
// Run from the repository root; screenshots are written to the ignored build/.
int main() {
    RenderManager renderer;
    renderer.InitWindowAndContext("WorldBox rendering regression test");
    SetWindowState(FLAG_WINDOW_HIDDEN);
    SetTargetFPS(60);
    int failures = 0;
    const int sizes[][2] = {{640, 360}, {1280, 720}, {1366, 768},
        {1920, 1080}, {2560, 1440}, {3440, 1440}, {1024, 768}, {800, 1000},
        {0, 0}, {-1, -1}}; // Enter borderless fullscreen, then restore window.
    for (const auto& size : sizes) {
        if (size[0] > 0) SetWindowSize(size[0], size[1]);
        else renderer.ToggleFullscreenMode();
        for (int frame = 0; frame < 4; ++frame) {
            renderer.Update();
            renderer.BeginVirtual();
            ClearBackground(DARKGRAY);
            DrawRectangle(0, 0, 80, 80, RED);
            DrawRectangle(SCREEN_WIDTH - 80, SCREEN_HEIGHT - 80, 80, 80, BLUE);
            DrawRectangle(SCREEN_WIDTH / 2 - 40, SCREEN_HEIGHT / 2 - 40, 80, 80, GREEN);
            renderer.EndVirtual();
            renderer.Present();
        }
        // LoadImageFromScreen in raylib 5.5 multiplies Windows dimensions by
        // DPI again. Read the actual framebuffer extent for pixel assertions.
        Image image{};
        image.width = GetRenderWidth();
        image.height = GetRenderHeight();
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        image.data = rlReadScreenPixels(image.width, image.height);
        const Vector2 markers[] = {{40, 40}, {SCREEN_WIDTH - 40, SCREEN_HEIGHT - 40},
            {SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2}};
        const Color expected[] = {RED, BLUE, GREEN};
        for (int i = 0; i < 3; ++i) {
            Vector2 point = renderer.VirtualToScreen(markers[i]);
            Vector2 back = renderer.ScreenToVirtual(point);
            if (std::fabs(back.x - markers[i].x) > 0.01f || std::fabs(back.y - markers[i].y) > 0.01f) ++failures;
            const int x = (int)(point.x * image.width / GetScreenWidth());
            const int y = (int)(point.y * image.height / GetScreenHeight());
            Color actual = GetImageColor(image, x, y);
            if (actual.r != expected[i].r || actual.g != expected[i].g || actual.b != expected[i].b) {
                std::printf("FAIL %dx%d marker %d: RGB %d,%d,%d\n", size[0], size[1], i, actual.r, actual.g, actual.b);
                ++failures;
            }
        }
        const Rectangle rect = renderer.DestinationRect();
        if (rect.x < -0.01f || rect.y < -0.01f ||
            rect.x + rect.width > GetScreenWidth() + 0.01f ||
            rect.y + rect.height > GetScreenHeight() + 0.01f) ++failures;
        // SetMousePosition uses window coordinates; the getter must land on the
        // same virtual center as the rendered green marker, even with black bars.
        Vector2 center = renderer.VirtualToScreen(markers[2]);
        SetMousePosition((int)center.x, (int)center.y);
        Vector2 mouse = renderer.GetVirtualMousePosition();
        const float tolerance = 1.1f / renderer.Scale();
        if (std::fabs(mouse.x - markers[2].x) > tolerance ||
            std::fabs(mouse.y - markers[2].y) > tolerance) ++failures;
        if (size[0] == 1280 || size[0] == 3440)
            ExportImage(image, TextFormat("build/render-%dx%d.png", size[0], size[1]));
        UnloadImage(image);
        std::printf("Checked requested %dx%d, window %dx%d, framebuffer %dx%d, DPI %.2f\n",
            size[0], size[1], GetScreenWidth(), GetScreenHeight(), GetRenderWidth(), GetRenderHeight(), GetWindowScaleDPI().x);
    }
    std::printf("Rendering regression failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}

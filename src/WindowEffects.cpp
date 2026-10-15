#include "WindowEffects.h"

#include <QWidget>
#include <QWindow>
#include <QtGui/qpa/qplatformwindow_p.h>

#include <windows.h>
#include <dwmapi.h>

namespace {

HWND nativeHandle(QWidget* window)
{
    return reinterpret_cast<HWND>(window->winId());
}

// Undocumented but long-stable user32 API used for acrylic on Windows 10.
struct AccentPolicy {
    int state;
    int flags;
    DWORD gradientColor; // AABBGGRR
    int animationId;
};

struct CompositionAttributeData {
    int attribute;
    void* data;
    SIZE_T size;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, CompositionAttributeData*);

bool enableLegacyAcrylic(HWND hwnd)
{
    constexpr int kAccentEnableAcrylicBlurBehind = 4;
    constexpr int kAttributeAccentPolicy = 19;

    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
        return false;
    const auto setAttribute = reinterpret_cast<SetWindowCompositionAttributeFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "SetWindowCompositionAttribute")));
    if (!setAttribute)
        return false;

    AccentPolicy accent = {kAccentEnableAcrylicBlurBehind, 2, 0x99140C0A, 0};
    CompositionAttributeData data = {kAttributeAccentPolicy, &accent, sizeof(accent)};
    return setAttribute(hwnd, &data) != FALSE;
}

} // namespace

namespace WindowEffects {

void removeTitleBar(QWidget* window)
{
    const HWND hwnd = nativeHandle(window);
    QWindow* handle = window->windowHandle();
    auto* platformWindow = handle ? handle->nativeInterface<QNativeInterface::Private::QWindowsWindow>() : nullptr;
    if (!platformWindow)
        return;

    // Measure whatever frame remains and fold it into Qt's custom margins, so Qt's
    // geometry bookkeeping stays consistent with the native window.
    RECT windowRect = {};
    RECT clientRect = {};
    POINT origin = {0, 0};
    if (!GetWindowRect(hwnd, &windowRect) || !GetClientRect(hwnd, &clientRect) || !ClientToScreen(hwnd, &origin))
        return;

    const QMargins frame(origin.x - windowRect.left, origin.y - windowRect.top,
                         windowRect.right - (origin.x + clientRect.right),
                         windowRect.bottom - (origin.y + clientRect.bottom));
    if (!frame.isNull())
        platformWindow->setCustomMargins(platformWindow->customMargins() - frame);

    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    const COLORREF border = RGB(44, 48, 78);
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
}

bool enableAcrylic(QWidget* window)
{
    const HWND hwnd = nativeHandle(window);

    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    // Let DWM compose the whole client area so translucent pixels reveal the backdrop.
    const MARGINS sheet = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &sheet);

    const DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_TRANSIENTWINDOW; // acrylic
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop))))
        return true;

    return enableLegacyAcrylic(hwnd);
}

} // namespace WindowEffects

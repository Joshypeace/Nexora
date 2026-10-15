#pragma once

class QWidget;

// Native window styling for the floating launcher.
namespace WindowEffects {

// Extends the client area over the native frame so the window has no title bar
// or border while keeping DWM's shadow, rounded corners and show/hide transitions.
// Safe to call repeatedly (e.g. after a DPI change).
void removeTitleBar(QWidget* window);

// Enables a dark acrylic backdrop behind the window: the Windows 11 system
// backdrop where available, otherwise the legacy Windows 10 acrylic accent.
// Returns false when neither is available and the caller must paint an opaque background.
bool enableAcrylic(QWidget* window);

} // namespace WindowEffects

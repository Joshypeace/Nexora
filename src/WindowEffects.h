#pragma once

class QWidget;

// Native window styling for the floating launcher.
namespace WindowEffects {

// Extends the client area over the native frame so the window has no title bar
// or border while keeping DWM's shadow, rounded corners and show/hide transitions.
// Safe to call repeatedly (e.g. after a DPI change).
void removeTitleBar(QWidget* window);

enum class Backdrop {
    None,   // no blur available: paint an opaque background
    Legacy, // Windows 10 acrylic accent (moving the window is laggy)
    System, // Windows 11 DWM system backdrop
};

// Enables a dark acrylic backdrop behind the window: the Windows 11 system
// backdrop where available, otherwise the legacy Windows 10 acrylic accent.
Backdrop enableAcrylic(QWidget* window);

} // namespace WindowEffects

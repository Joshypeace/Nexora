#include "GlobalHotkey.h"

#include <optional>

namespace {

constexpr wchar_t kWindowClass[] = L"Nexora.MessageWindow";
constexpr int kHotkeyId = 0x4E58;

UINT activationMessage()
{
    static const UINT message = RegisterWindowMessageW(L"Nexora.ShowLauncher");
    return message;
}

std::optional<UINT> virtualKey(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    if (modifiers.testFlag(Qt::KeypadModifier)) {
        if (key >= Qt::Key_0 && key <= Qt::Key_9)
            return static_cast<UINT>(VK_NUMPAD0 + (key - Qt::Key_0));
        switch (key) {
        case Qt::Key_Asterisk: return VK_MULTIPLY;
        case Qt::Key_Plus: return VK_ADD;
        case Qt::Key_Minus: return VK_SUBTRACT;
        case Qt::Key_Period: return VK_DECIMAL;
        case Qt::Key_Slash: return VK_DIVIDE;
        default: break;
        }
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return static_cast<UINT>('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return static_cast<UINT>('0' + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return static_cast<UINT>(VK_F1 + (key - Qt::Key_F1));

    switch (key) {
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Return:
    case Qt::Key_Enter: return VK_RETURN;
    case Qt::Key_Tab: return VK_TAB;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Comma: return VK_OEM_COMMA;
    case Qt::Key_Period: return VK_OEM_PERIOD;
    case Qt::Key_Minus: return VK_OEM_MINUS;
    case Qt::Key_Plus:
    case Qt::Key_Equal: return VK_OEM_PLUS;
    case Qt::Key_Semicolon: return VK_OEM_1;
    case Qt::Key_Slash: return VK_OEM_2;
    case Qt::Key_QuoteLeft: return VK_OEM_3;
    case Qt::Key_BracketLeft: return VK_OEM_4;
    case Qt::Key_Backslash: return VK_OEM_5;
    case Qt::Key_BracketRight: return VK_OEM_6;
    case Qt::Key_Apostrophe: return VK_OEM_7;
    default: return std::nullopt;
    }
}

} // namespace

GlobalHotkey::GlobalHotkey(QObject* parent)
    : QObject(parent)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &GlobalHotkey::windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);

    m_window = CreateWindowExW(0, kWindowClass, L"Nexora", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (m_window)
        SetWindowLongPtrW(m_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

GlobalHotkey::~GlobalHotkey()
{
    unregister();
    if (m_window) {
        SetWindowLongPtrW(m_window, GWLP_USERDATA, 0);
        DestroyWindow(m_window);
    }
    UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
}

bool GlobalHotkey::setShortcut(const QKeySequence& sequence)
{
    unregister();
    m_sequence = {};
    if (!m_window || !isAcceptable(sequence))
        return false;

    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    const std::optional<UINT> key = virtualKey(combination.key(), modifiers);
    UINT nativeModifiers = MOD_NOREPEAT;
    if (modifiers.testFlag(Qt::ControlModifier))
        nativeModifiers |= MOD_CONTROL;
    if (modifiers.testFlag(Qt::AltModifier))
        nativeModifiers |= MOD_ALT;
    if (modifiers.testFlag(Qt::ShiftModifier))
        nativeModifiers |= MOD_SHIFT;
    if (modifiers.testFlag(Qt::MetaModifier))
        nativeModifiers |= MOD_WIN;

    m_registered = RegisterHotKey(m_window, kHotkeyId, nativeModifiers, *key) != FALSE;
    if (m_registered)
        m_sequence = sequence;
    return m_registered;
}

bool GlobalHotkey::isAcceptable(const QKeySequence& sequence)
{
    if (sequence.count() != 1)
        return false;
    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    if (!virtualKey(combination.key(), modifiers))
        return false;

    // A global hotkey swallows its key system-wide, so plain keys (or Shift+key, which is
    // ordinary typing) are refused. F13-F24 do not exist on most keyboards and are fine alone.
    const bool hasModifier = modifiers.testAnyFlags(Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool spareFunctionKey = combination.key() >= Qt::Key_F13 && combination.key() <= Qt::Key_F24;
    return hasModifier || spareFunctionKey;
}

bool GlobalHotkey::signalRunningInstance()
{
    const HWND existing = FindWindowExW(HWND_MESSAGE, nullptr, kWindowClass, nullptr);
    if (!existing)
        return false;

    // This process was just started by the user and may hand foreground rights over.
    DWORD processId = 0;
    GetWindowThreadProcessId(existing, &processId);
    AllowSetForegroundWindow(processId);
    return PostMessageW(existing, activationMessage(), 0, 0) != FALSE;
}

void GlobalHotkey::unregister()
{
    if (m_registered && m_window)
        UnregisterHotKey(m_window, kHotkeyId);
    m_registered = false;
}

LRESULT CALLBACK GlobalHotkey::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<GlobalHotkey*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        if (message == WM_HOTKEY && wParam == kHotkeyId) {
            emit self->activated();
            return 0;
        }
        if (message == activationMessage()) {
            emit self->activationRequested();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

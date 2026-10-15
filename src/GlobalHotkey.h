#pragma once

#include <QKeySequence>
#include <QObject>

#include <windows.h>

// Registers a system-wide hotkey with RegisterHotKey. A hidden message-only
// window receives WM_HOTKEY, plus activation requests from a second Nexora
// instance, so neither depends on Qt's event loop being un-nested.
class GlobalHotkey : public QObject {
    Q_OBJECT

public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;

    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

    // Replaces the current registration. Returns false if the combination is
    // invalid or already owned by another application; the hotkey is then unset.
    bool setShortcut(const QKeySequence& sequence);
    QKeySequence shortcut() const { return m_sequence; }

    // True for a single key combination with Ctrl, Alt or Win (or a bare F13-F24).
    static bool isAcceptable(const QKeySequence& sequence);

    // Asks an already running instance to show its launcher. Returns false when none is running.
    static bool signalRunningInstance();

signals:
    void activated();
    void activationRequested();

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void unregister();

    HWND m_window = nullptr;
    QKeySequence m_sequence;
    bool m_registered = false;
};

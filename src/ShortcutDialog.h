#pragma once

#include <QDialog>
#include <QKeySequence>

class QKeySequenceEdit;

// Tiny dialog that captures a new global shortcut.
class ShortcutDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShortcutDialog(const QKeySequence& current, QWidget* parent = nullptr);

    QKeySequence sequence() const;

private:
    QKeySequenceEdit* m_edit;
};

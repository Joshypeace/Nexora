#include "ShortcutDialog.h"

#include "GlobalHotkey.h"

#include <QDialogButtonBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ShortcutDialog::ShortcutDialog(const QKeySequence& current, QWidget* parent)
    : QDialog(parent)
    , m_edit(new QKeySequenceEdit(current, this))
{
    setWindowTitle(tr("Omnipane shortcut"));
    setWindowFlag(Qt::WindowStaysOnTopHint);
    m_edit->setMaximumSequenceLength(1);
    m_edit->setClearButtonEnabled(true);

    auto* hint = new QLabel(tr("Press the key combination that should open Omnipane.\n"
                               "It must include Ctrl, Alt or Win, for example Ctrl+Space or Alt+Space."),
                            this);
    hint->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QPushButton* save = buttons->button(QDialogButtonBox::Save);
    save->setEnabled(GlobalHotkey::isAcceptable(current));
    connect(m_edit, &QKeySequenceEdit::keySequenceChanged, this, [save](const QKeySequence& sequence) {
        save->setEnabled(GlobalHotkey::isAcceptable(sequence));
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);
    layout->addWidget(hint);
    layout->addWidget(m_edit);
    layout->addWidget(buttons);
    setMinimumWidth(380);
}

QKeySequence ShortcutDialog::sequence() const
{
    return m_edit->keySequence();
}

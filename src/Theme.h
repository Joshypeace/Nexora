#pragma once

class QApplication;
class QIcon;

// Omnipane's visual theme for everything outside the launcher's own paint code:
// palette, fonts, menus (including Qt's built-in context menus), tooltips and dialogs.
namespace Theme {

void apply(QApplication& app);

// The Omnipane logo at every size it ships in, so small uses (tray, header) pick the
// simplified small-size drawing instead of a scaled-down detailed one.
QIcon appIcon();

} // namespace Theme

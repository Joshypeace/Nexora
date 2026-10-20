#pragma once

class QApplication;

// Omnipane's visual theme for everything outside the launcher's own paint code:
// palette, fonts, menus (including Qt's built-in context menus), tooltips and dialogs.
namespace Theme {

void apply(QApplication& app);

} // namespace Theme

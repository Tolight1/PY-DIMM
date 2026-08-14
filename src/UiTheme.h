#pragma once

#include <QString>

class QApplication;

namespace UiTheme {

// Applies the dark navy/cyan DIMM-style palette and stylesheet. Called once
// from main() before any window is created.
void apply(QApplication &app);

QString styleSheet();

} // namespace UiTheme

#include "terminaledit.h"

TerminalEdit::TerminalEdit(QWidget* parent)
    : QPlainTextEdit(parent) {}

void TerminalEdit::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    emit enterPressed();
    return;  // No dejamos que se escriba un salto de línea
  }

  QPlainTextEdit::keyPressEvent(event);
}

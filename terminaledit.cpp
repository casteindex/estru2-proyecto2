#include "terminaledit.h"

TerminalEdit::TerminalEdit(QWidget* parent) : QPlainTextEdit(parent) {}

// Interceptar teclas especiales que pueda presionar el usuario y así poder
// procesarlas por separado y asígnarle funciones especiales
void TerminalEdit::keyPressEvent(QKeyEvent* event) {
  switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: emit enterPressed(); return;

    case Qt::Key_Backspace: emit backspacePressed(); return;
    case Qt::Key_Up: emit arrowUpPressed(); return;
    case Qt::Key_Down: emit arrowDownPressed(); return;
    case Qt::Key_Left: emit arrowLeftPressed(); return;
    case Qt::Key_Right: emit arrowRightPressed(); return;

    case Qt::Key_Home: return;
    case Qt::Key_PageUp: return;

    default: QPlainTextEdit::keyPressEvent(event);
  }
}

// Bloquear el mouse completamente para evitar que el usuario pueda mover el
// cursor haciendo click
void TerminalEdit::mousePressEvent(QMouseEvent* event) {
  event->ignore();
}
void TerminalEdit::mouseReleaseEvent(QMouseEvent* event) {
  event->ignore();
}
void TerminalEdit::mouseMoveEvent(QMouseEvent* event) {
  event->ignore();
}
void TerminalEdit::mouseDoubleClickEvent(QMouseEvent* event) {
  event->ignore();
}

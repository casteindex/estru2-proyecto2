#include "terminaledit.h"

TerminalEdit::TerminalEdit(QWidget* parent)
    : QPlainTextEdit(parent) {}

// Interceptar teclas especiales que pueda presionar el usuario y así poder
// procesarlas por separado y asígnarle funciones especiales
void TerminalEdit::keyPressEvent(QKeyEvent* event) {
  int key = event->key();

  // ENTER
  if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    emit enterPressed();
    return;
  }

  // BACKSPACE
  if (key == Qt::Key_Backspace) {
    emit backspacePressed();
    return;
  }

  // Flechas arriba/abajo
  if (key == Qt::Key_Up) {
    emit arrowUpPressed();
    return;
  }
  if (key == Qt::Key_Down) {
    emit arrowDownPressed();
    return;
  }

  // Flechas izquierda/derecha
  if (key == Qt::Key_Left) {
    emit arrowLeftPressed();
    return;
  }
  if (key == Qt::Key_Right) {
    emit arrowRightPressed();
    return;
  }

  // Texto normal -> solo emitir si es imprimible
  QString t = event->text();
  if (t.length() > 0 && t.at(0).isPrint()) {
    emit textTyped(t);
    return;
  }

  // Cualquier otra tecla se deja pasar a su comportamiento normal
  QPlainTextEdit::keyPressEvent(event);
}

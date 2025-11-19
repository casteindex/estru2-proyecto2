#include "terminaledit.h"

TerminalEdit::TerminalEdit(QWidget* parent) : QPlainTextEdit(parent) {}

// Interceptar teclas especiales que pueda presionar el usuario y así poder
// procesarlas por separado y asígnarle funciones especiales
void TerminalEdit::keyPressEvent(QKeyEvent* event) {
  switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
      emit enterPressed();
      return;

    case Qt::Key_Backspace: emit backspacePressed(); return;
    case Qt::Key_Up: emit arrowUpPressed(); return;
    case Qt::Key_Down: emit arrowDownPressed(); return;
    case Qt::Key_Left: emit arrowLeftPressed(); return;
    case Qt::Key_Right: emit arrowRightPressed(); return;

    case Qt::Key_Home: return;
    case Qt::Key_PageUp: return;
  }

  // Texto normal -> solo emitir si es imprimible
  const QString t = event->text();
  if (!t.isEmpty() && t.at(0).isPrint()) {
    emit textTyped(t);
    return;
  }

  // Cualquier otra tecla usa comportamiento normal
  QPlainTextEdit::keyPressEvent(event);
}

// Permitir que el usuario pegue contenido a la terminal
void TerminalEdit::insertFromMimeData(const QMimeData* source) {
  // Obtener el texto del portapapeles
  QString pasted = source->text();
  if (pasted.isEmpty()) return;

  // Emitirlo caracter por caracter
  for (QChar c : pasted) {
    if (c.isPrint())
      emit textTyped(QString(c));
    else if (c == '\n' || c == '\r')
      emit enterPressed();  // permite pegar comandos multilínea si quisieras
  }
}

// Bloquear el mouse para que el usuario no pueda mover el cursor haciendo click
// pero sí permitir que seleccione el texto de la línea actual
void TerminalEdit::mousePressEvent(QMouseEvent* event) {
  QPlainTextEdit::mousePressEvent(event);  // permite selección
  emit requestCursorFix();                 // avisar a la terminal
}

void TerminalEdit::mouseReleaseEvent(QMouseEvent* event) {
  QPlainTextEdit::mouseReleaseEvent(event);
  emit requestCursorFix();
}

void TerminalEdit::mouseMoveEvent(QMouseEvent* event) {
  QPlainTextEdit::mouseMoveEvent(event);
  emit requestCursorFix();
}

void TerminalEdit::mouseDoubleClickEvent(QMouseEvent* event) {
  event->ignore();
}

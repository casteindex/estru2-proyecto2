#include "terminal.h"

#include "ui_terminal.h"

Terminal::Terminal(QWidget* parent)
    : QWidget(parent), ui(new Ui::Terminal) {
  ui->setupUi(this);
  editor = ui->plainTextEdit;

  editor->setFocusPolicy(Qt::StrongFocus);
  editor->setFocus();

  // Conectar las señales que emite el editor cuando el usuario presiona alguna
  // tecla especial con el slot que ejecuta el comando en Terminal.
  connect(editor, &TerminalEdit::enterPressed, this, &Terminal::onEnter);
  connect(editor, &TerminalEdit::textTyped, this, &Terminal::onTextTyped);
  connect(editor, &TerminalEdit::backspacePressed, this, &Terminal::onBackspace);
  connect(editor, &TerminalEdit::arrowLeftPressed, this, &Terminal::onArrowLeft);
  connect(editor, &TerminalEdit::arrowRightPressed, this, &Terminal::onArrowRight);
  connect(editor, &TerminalEdit::arrowUpPressed, this, &Terminal::onArrowUp);
  connect(editor, &TerminalEdit::arrowDownPressed, this, &Terminal::onArrowDown);
  connect(editor, &TerminalEdit::requestCursorFix, this, &Terminal::forceCursorToEnd);

  printPrompt();  // Mostrar primer prompt
}

Terminal::~Terminal() {
  delete ui;
}

// -------------------- Funciones ---------------
void Terminal::processCommand(const QString& cmd) {
  if (cmd == "") return;
  if (cmd == "clear") {
    editor->clear();
    return;
  }
  if (cmd == "exit") {
    QApplication::quit();
    return;
  }
  // Comando desconocido
  editor->appendPlainText("Comando '" + cmd + "' no reconocido");
}

// -------------------- Funcionamiento general de la terminal ---------------
void Terminal::printPrompt() {
  editor->appendPlainText(prompt);
  lineStartPos = editor->toPlainText().length();
  currentLine.clear();
  // Asegurar que el cursor quede al final
  QTextCursor c = editor->textCursor();
  c.movePosition(QTextCursor::End);
  editor->setTextCursor(c);
}

void Terminal::onTextTyped(const QString& t) {
  editor->insertPlainText(t);
  currentLine += t;
}

void Terminal::onBackspace() {
  QTextCursor c = editor->textCursor();
  if (c.position() <= lineStartPos) return;
  c.deletePreviousChar();
  editor->setTextCursor(c);
  if (!currentLine.isEmpty()) currentLine.chop(1);
}

void Terminal::onArrowLeft() {
  QTextCursor c = editor->textCursor();
  if (c.position() <= lineStartPos) return;
  c.movePosition(QTextCursor::Left);
  editor->setTextCursor(c);
}

void Terminal::onArrowRight() {
  QTextCursor c = editor->textCursor();
  c.movePosition(QTextCursor::Right);
  editor->setTextCursor(c);
}

void Terminal::onArrowUp() {
  if (historial.isEmpty()) return;
  if (indiceHistorial == -1)
    indiceHistorial = historial.length() - 1;
  else if (indiceHistorial > 0)
    indiceHistorial--;
  setLineText(historial[indiceHistorial]);
}

void Terminal::onArrowDown() {
  if (historial.isEmpty()) return;
  if (indiceHistorial == -1) return;
  if (indiceHistorial < historial.length() - 1) {
    indiceHistorial++;
    setLineText(historial[indiceHistorial]);
  } else {  // volver a línea vacía
    indiceHistorial = -1;
    setLineText("");
  }
}

void Terminal::setLineText(const QString& text) {
  QTextCursor c = editor->textCursor();
  // Eliminar la línea actual
  c.setPosition(lineStartPos);
  c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
  c.removeSelectedText();
  // Escribir nueva línea
  c.insertText(text);
  editor->setTextCursor(c);
  currentLine = text;
}

void Terminal::onEnter() {
  // Evitar Enter en medio de la línea
  QTextCursor c = editor->textCursor();
  c.movePosition(QTextCursor::End);
  editor->setTextCursor(c);
  QString cmd = currentLine.trimmed();
  // Guardar en historial si no está vacío
  if (!cmd.isEmpty()) historial.append(cmd);
  indiceHistorial = -1;  // reset historial
  processCommand(cmd);
  printPrompt();
}

// Cursor
void Terminal::forceCursorToEnd() {
  QTextCursor c = editor->textCursor();
  if (c.position() < lineStartPos)
    c.setPosition(editor->document()->characterCount() - 1);
  editor->setTextCursor(c);
}

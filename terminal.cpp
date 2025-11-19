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
  // connect(editor, &TerminalEdit::textTyped, this, &Terminal::onTextTyped);
  connect(editor, &TerminalEdit::backspacePressed, this, &Terminal::onBackspace);
  connect(editor, &TerminalEdit::arrowLeftPressed, this, &Terminal::onArrowLeft);
  connect(editor, &TerminalEdit::arrowRightPressed, this, &Terminal::onArrowRight);
  connect(editor, &TerminalEdit::arrowUpPressed, this, &Terminal::onArrowUp);
  connect(editor, &TerminalEdit::arrowDownPressed, this, &Terminal::onArrowDown);

  printPrompt();  // Mostrar primer prompt
}

Terminal::~Terminal() {
  delete ui;
}

// -------------------- Funciones ---------------
void Terminal::processCommand(const QString& linea) {
  if (linea == "") return;

  // Crear un arreglo de partes de la línea ingresada por el usuario
  // usando el espacio como separador
  QStringList partes = linea.split(' ', Qt::SkipEmptyParts);
  QString cmd = partes[0];           // comando principal
  QStringList args = partes.mid(1);  // todos los argumentos

  if (cmd == "clear") {
    editor->clear();
    return;
  } else if (cmd == "exit") {
    QApplication::quit();
    return;
  } else if (cmd == "cd") {
  } else {
    editor->appendPlainText("Comando '" + cmd + "' no reconocido.");
  }
}

// -------------------- Procesamiento de comandos ---------------
void Terminal::onEnter() {
  QTextCursor c = editor->textCursor();
  c.movePosition(QTextCursor::End);
  editor->setTextCursor(c);

  QString fullText = editor->toPlainText();
  QString cmd = fullText.mid(lineStartPos).trimmed();

  if (!cmd.isEmpty())
    historial.append(cmd);

  indiceHistorial = -1;

  processCommand(cmd);
  printPrompt();
}

// Sobreescribir la línea actual con el texto que se pase como argumento
void Terminal::setLineText(const QString& text) {
  QTextCursor c = editor->textCursor();
  c.setPosition(lineStartPos);
  // Seleccionar todo lo que está actualmente escrito por el usuario
  // y borrarlo
  c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
  c.removeSelectedText();
  // Insertar el texto que sí queremos mostrar en la línea actual
  c.insertText(text);
  editor->setTextCursor(c);
}

void Terminal::printPrompt() {
  editor->appendPlainText(prompt);
  lineStartPos = editor->toPlainText().length();
  // currentLine.clear();
  // Asegurar que el cursor quede al final
  QTextCursor c = editor->textCursor();
  c.movePosition(QTextCursor::End);
  editor->setTextCursor(c);
}

// -------------------- Manejo de input de teclas ---------------
void Terminal::onBackspace() {
  QTextCursor c = editor->textCursor();
  if (c.position() <= lineStartPos) return;
  c.deletePreviousChar();
  editor->setTextCursor(c);
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

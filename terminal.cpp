#include "terminal.h"
#include "ui_terminal.h"

Terminal::Terminal(QWidget* parent)
    : QWidget(parent),
      ui(new Ui::Terminal),
      currentDir(QDir::homePath().append("/Z")),
      lineStartPos(0),     // límite del prompt
      indiceHistorial(-1)  // -1 = escribiendo línea nueva
{
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

  editor->setCursorWidth(editor->fontMetrics().averageCharWidth());
  printEncabezado();
  printPrompt();  // Mostrar primer prompt
}

Terminal::~Terminal() {
  delete ui;
}

// -------------------- Funciones ---------------
void Terminal::processCommand(const QString& linea) {
  if (linea.trimmed().isEmpty()) return;

  // Obtener la lista de argumentos
  QStringList partes = QProcess::splitCommand(linea);
  if (partes.isEmpty()) return;

  QString cmd = partes.first();      // Comando principal
  QStringList args = partes.mid(1);  // Elementos restantes son los argumentos

  // qDebug() << "Argumentos de " << cmd << ":";
  // for (const QString& arg : args)
  //   qDebug() << "  - " << arg;

  // --- Lógica del comando ---
  if (cmd == "clear") {
    editor->clear();
    return;
  } else if (cmd == "exit") {
    QApplication::quit();
    return;
  } else if (cmd == "cd") {
    processCd(args);
  } else if (cmd == "ls") {
    processLs();
  } else if (cmd == "mkdisk") {
    DiskManager::mkdisk(args, editor, currentDir);
  } else if (cmd == "rmdisk") {
    DiskManager::rmdisk(args, editor, currentDir);
  }

  else {
    editor->appendPlainText("Comando '" + cmd + "' no reconocido.\n");
  }
}

void Terminal::processCd(const QStringList& args) {
  if (args.isEmpty()) {  // cd solo = ir al home
    currentDir = QDir(QDir::homePath());
    editor->appendPlainText("");  // agregar línea vacía
    return;
  }
  QString path = args.first();
  // Caso especial: si está en el directorio raíz y se pone "cd .."
  if (path == ".." && currentDir.isRoot()) {
    editor->appendPlainText("");
    return;
  }
  QDir tempDir = currentDir;
  // Nota: cd() ya maneja automáticamente ".", "..", rutas relativas, rutas
  // absolutas y comillas ya procesadas por splitCommand()
  if (!tempDir.cd(path)) {
    editor->appendPlainText(
      "El sistema no puede encontrar la ruta especificada.\n");
    return;
  }
  currentDir = tempDir;
  editor->appendPlainText("");
}

void Terminal::processLs() {
  // Obtener lista de archivos y carpetas del directorio actual
  QFileInfoList entries = currentDir.entryInfoList(
      QDir::NoDotAndDotDot | QDir::AllEntries,
      QDir::DirsFirst | QDir::Name);
  if (entries.isEmpty()) {
    editor->appendPlainText("");
    return;
  }
  QStringList output;
  for (const QFileInfo& info : entries)
    output << "- " + info.fileName();

  editor->appendPlainText(output.join('\n'));
  editor->appendPlainText("");
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
  prompt = currentDir.absolutePath() + ">>";
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

void Terminal::printEncabezado() const {
  const int largoLinea = 97;
  const int largoTitulo = 19;  // Sistema de archivos
  const int largoNombre = 32;  // Alejandro Castellanos - 12441410
  const int largoEspacios = (largoLinea - largoTitulo - 2) / 2;  // 2*"|"

  QString encabezado;
  encabezado += QString("-").repeated(largoLinea) + "\n";
  encabezado += "|" + QString(" ").repeated(largoEspacios);
  encabezado += "Sistema de Archivos";
  encabezado += QString(" ").repeated(largoEspacios) + "|\n";
  encabezado += QString("-").repeated(largoLinea) + "\n";
  encabezado += QString(" ").repeated(largoLinea - largoNombre);
  encabezado += "Alejandro Castellanos - 12441410\n";

  editor->appendPlainText(encabezado);
}

#include "terminal.h"

#include "ui_terminal.h"

Terminal::Terminal(QWidget* parent)
    : QWidget(parent), ui(new Ui::Terminal) {
  ui->setupUi(this);

  // Reemplazamos el QPlainTextEdit del .ui por nuestro TerminalEdit
  editor = ui->plainTextEdit;

  // Conectamos Enter al slot
  connect(editor, &TerminalEdit::enterPressed, this, &Terminal::onEnter);
}

Terminal::~Terminal() {
  delete ui;
}

void Terminal::onEnter() {
  editor->clear();  // Esto sucede al presionar Enter
  editor->appendPlainText("hola mundo!");
}

#pragma once
#include <QDebug>
#include <QDir>
#include <QTextCursor>
#include <QWidget>

#include "terminaledit.h"

namespace Ui {
class Terminal;
}

class Terminal : public QWidget {
  Q_OBJECT

 public:
  explicit Terminal(QWidget* parent = nullptr);
  ~Terminal();

 private slots:
  void onEnter();
  void onBackspace();
  void onArrowLeft();
  void onArrowRight();
  void onArrowUp();
  void onArrowDown();

 private:
  Ui::Terminal* ui;
  TerminalEdit* editor;

  QString prompt = ">";
  int lineStartPos = 0;  // límite del prompt

  QList<QString> historial;
  int indiceHistorial = -1;  // -1 = escribiendo línea nueva

  void printPrompt();
  void setLineText(const QString& text);
  void processCommand(const QString& cmd);
};

#pragma once
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QTextCursor>
#include <QWidget>

#include "diskmanager.h"
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
  QDir currentDir;

  QString prompt;
  int lineStartPos;

  QList<QString> historial;
  int indiceHistorial;

  void printPrompt();
  void setLineText(const QString& text);
  void processCommand(const QString& cmd);
  void processCd(const QStringList& args);
  void processLs();
};

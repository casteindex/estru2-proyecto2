#pragma once
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QTextCursor>
#include <QWidget>
#include <vector>

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
  bool esperandoConfirmacion = false;
  QString prompt;
  void printPrompt();

 signals:
  void confirmacionRecibida(char r);

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

  int lineStartPos;

  std::vector<QString> historial;
  int indiceHistorial;

  // void printPrompt();
  void setLineText(const QString& text);
  void processCommand(const QString& cmd);
  void processCd(const QStringList& args);
  void processLs();
  void printEncabezado() const;
};

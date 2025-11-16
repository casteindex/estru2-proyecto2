#pragma once
#include <QDir>
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

 private:
  Ui::Terminal* ui;
  TerminalEdit* editor;
};

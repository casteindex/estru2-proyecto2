#pragma once
#include <QKeyEvent>
#include <QPlainTextEdit>

class TerminalEdit : public QPlainTextEdit {
  Q_OBJECT

 public:
  explicit TerminalEdit(QWidget* parent = nullptr);

 signals:
  void enterPressed();

 protected:
  void keyPressEvent(QKeyEvent* event) override;
};

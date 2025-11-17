#pragma once
#include <QKeyEvent>
#include <QPlainTextEdit>

class TerminalEdit : public QPlainTextEdit {
  Q_OBJECT

 public:
  explicit TerminalEdit(QWidget* parent = nullptr);

 signals:
  void enterPressed();
  void textTyped(const QString& text);
  void backspacePressed();
  void arrowUpPressed();
  void arrowDownPressed();
  void arrowLeftPressed();
  void arrowRightPressed();

 protected:
  void keyPressEvent(QKeyEvent* event) override;
};

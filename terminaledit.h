#pragma once
#include <QKeyEvent>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QTextCursor>

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
  void requestCursorFix();

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void insertFromMimeData(const QMimeData* source) override;
  void mouseMoveEvent(QMouseEvent* event) override;
};

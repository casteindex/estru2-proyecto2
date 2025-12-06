#pragma once
#include <QDir>
#include <QPlainTextEdit>
#include <QString>
#include <QStringList>
#include <cstring>
class Terminal;

class DiskManager {
 public:
  DiskManager() = default;

  static void mkdisk(
    const QStringList& args, QPlainTextEdit* out, const QDir& currentDir);
  static void rmdisk(const QStringList& args, QPlainTextEdit* out,
    const QDir& currentDir, Terminal* terminal);
  static void fdisk(const QStringList& args, QPlainTextEdit* out,
    const QDir& currentDir, Terminal* terminal);
  static void mount(
    const QStringList& args, QPlainTextEdit* out, const QDir& currentDir);
  static void unmount(const QStringList& args, QPlainTextEdit* out);
  static void rep(
    const QStringList& args, QPlainTextEdit* out, const QDir& currentDir);

 private:
  static bool mkdiskParams(const QStringList& args, long& sizeBytes, char& fit,
    QString& path, QString& unit, QPlainTextEdit* out);
  static bool createEmptyDisk(
    const QString& path, long sizeBytes, QPlainTextEdit* out);
  static bool writeInitialMBR(
    const QString& path, long sizeBytes, char fit, QPlainTextEdit* out);

  static bool fdiskParams(const QStringList& args, long& sizeBytes, char& unit,
    char& type, QString& path, QString& name, QString& deleteMode,
    long& addValue, char& fit, QPlainTextEdit* out);

  static bool crearPrimaria(const QString& path, const QString& name,
    long sizeBytes, char fit, QPlainTextEdit* out);
  static bool crearExtendida(const QString& path, const QString& name,
    long sizeBytes, char fit, QPlainTextEdit* out);
  static bool crearLogica(const QString& path, const QString& name,
    long sizeBytes, char fit, QPlainTextEdit* out);
  static bool deleteParticion(const QString& path, const QString& name,
    QPlainTextEdit* out, Terminal* terminal);
  static bool addAParticion(const QString& path, const QString& name,
    long addBytes, QPlainTextEdit* out);
};

#pragma once
#include <QDir>
#include <QPlainTextEdit>
#include <QString>
#include <QStringList>
#include <cstring>

// ----------------------- Structs ------------------------
struct Partition {
  char status;    // 0 = libre, 1 = usada
  char type;      // P, E
  char fit;       // B, F, W
  int start;      // byte donde inicia
  int size;       // tamaño en bytes
  char name[16];  // nombre
};

struct MBR {
  int size;            // tamaño total del disco
  char fit;            // BF, FF, WF
  Partition parts[4];  // 4 slots máximo
};

struct EBR {
  char status;
  char fit;
  int start;
  int size;
  int next;
  char name[16];
};

// ----------------------- DiskManager ------------------------
class DiskManager {
 public:
  DiskManager() = default;

  static void mkdisk(
    const QStringList& args, QPlainTextEdit* out, const QDir& currentDir);
  static void rmdisk(
    const QStringList& args, QPlainTextEdit* out, const QDir& currentDir);

 private:
  static bool mkdiskParams(const QStringList& args, long& sizeBytes, char& fit,
    QString& path, QString& unit, QPlainTextEdit* out);

  static bool createEmptyDisk(
    const QString& path, long sizeBytes, QPlainTextEdit* out);
  static bool writeInitialMBR(
    const QString& path, long sizeBytes, char fit, QPlainTextEdit* out);
};

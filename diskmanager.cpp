#include "diskmanager.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <fstream>

using namespace std;

// ----------------------- MKDISK ------------------------
void DiskManager::mkdisk(
  const QStringList& args, QPlainTextEdit* out, const QDir& currentDir) {
  long sizeBytes = 0;
  char fit = 'F';
  QString rawPath;
  QString unit = "m";

  // Comprobar si se pasaron todos los argumentos obligatorios. Nota: los
  // parámetros se pasan por referencia, la función cambia los valores por
  // defecto si se le pasaron.
  if (!mkdiskParams(args, sizeBytes, fit, rawPath, unit, out)) return;

  // Resolver path relativo según currentDir
  QDir base = currentDir;
  QString finalPath = base.absoluteFilePath(rawPath);

  // Crear directorios si no existen
  QFileInfo info(finalPath);
  QDir dir = info.dir();
  if (!dir.exists()) {
    if (!dir.mkpath(".")) {
      out->appendPlainText("MKDISK: No se pudieron crear las carpetas.\n");
      return;
    }
  }

  if (!createEmptyDisk(finalPath, sizeBytes, out)) return;
  if (!writeInitialMBR(finalPath, sizeBytes, fit, out)) return;
  out->appendPlainText("MKDISK: Disco creado con éxito.\n");
}

bool DiskManager::mkdiskParams(const QStringList& args, long& sizeBytes,
  char& fit, QString& path, QString& unit, QPlainTextEdit* out) {
  bool sizeFound = false;
  bool pathFound = false;

  // Recorrer la lista de argumentos buscando los argumentos requeridos
  // TODO: ver cómo hacer que si se le pasa un argumento repetido tire error así
  // como si se le pasa un argumento inválido
  for (const QString& arg : args) {
    QString lowerArg = arg.toLower();

    if (lowerArg.startsWith("-size=")) {
      long size = arg.mid(6).toLong();
      if (size <= 0) {
        out->appendPlainText("MKDISK: Size debe ser mayor que 0.\n");
        return false;
      }
      sizeBytes = size;
      sizeFound = true;
    } else if (lowerArg.startsWith("-fit=")) {
      QString val = arg.mid(5).toUpper();
      if (val == "BF") fit = 'B';
      else if (val == "FF") fit = 'F';
      else if (val == "WF") fit = 'W';
      else {
        out->appendPlainText("MKDISK: Fit inválido.\n");
        return false;
      }
    } else if (lowerArg.startsWith("-unit=")) {
      unit = arg.mid(6).toLower();
      if (unit != "k" && unit != "m") {
        out->appendPlainText("MKDISK: Unit inválido, use k o m.\n");
        return false;
      }
    } else if (lowerArg.startsWith("-path=")) {
      pathFound = true;
      path = arg.mid(6);  // mantener mayúsculas
    }
  }

  if (!sizeFound) {
    out->appendPlainText("MKDISK: Falta parámetro size.\n");
    return false;
  }
  if (!pathFound) {
    out->appendPlainText("MKDISK: Falta parámetro path.\n");
    return false;
  }

  // Convertir a bytes según unit
  if (unit == "k") sizeBytes *= 1024;
  else sizeBytes *= 1024 * 1024;

  return true;  // Se encontraron los parámetros obligatorios
}

// ----------------------- RMDISK ------------------------
void DiskManager::rmdisk(
  const QStringList& args, QPlainTextEdit* out, const QDir& currentDir) {
  if (args.isEmpty()) {
    out->appendPlainText("RMDISK: Falta parámetro path.\n");
    return;
  }
  QString rawPath;
  for (const QString& a : args) {
    if (a.toLower().startsWith("-path=")) rawPath = a.mid(6);
  }
  if (rawPath.isEmpty()) {
    out->appendPlainText("RMDISK: Falta parámetro path.\n");
    return;
  }

  // Resolver path relativo según currentDir
  QDir base = currentDir;
  QString finalPath = base.absoluteFilePath(rawPath);
  QFile file(finalPath);
  if (!file.exists()) {
    out->appendPlainText("RMDISK: El archivo no existe.\n");
    return;
  }
  if (!file.remove()) {
    out->appendPlainText("RMDISK: No se pudo eliminar el archivo.\n");
    return;
  }
  out->appendPlainText("RMDISK: Disco eliminado con éxito.\n");
}

// ----------------------- Crear archivo vacío ------------------------
bool DiskManager::createEmptyDisk(
  const QString& path, long sizeBytes, QPlainTextEdit* out) {
  fstream file(path.toStdString(), ios::out | ios::binary);
  if (!file.is_open()) {
    out->appendPlainText("MKDISK: No se pudo crear el archivo.\n");
    return false;
  }
  // Rellenar con ceros
  // Nota: en archivos binarios, si se escibe un valor en un offset lejano más
  // allá del final actual, el sistema rellena automáticamente los bytes
  // intermedios con '\0'.
  if (sizeBytes > 0) {
    file.seekp(sizeBytes - 1);  // posicionarse al final del archivo
    file.write("\0", 1);        // escribir un cero
  }
  file.close();
  return true;
}

// ----------------------- Escribir MBR ------------------------
bool DiskManager::writeInitialMBR(
  const QString& path, long sizeBytes, char fit, QPlainTextEdit* out) {
  fstream file(path.toStdString(), ios::in | ios::out | ios::binary);
  if (!file.is_open()) {
    out->appendPlainText(
      "MKDISK: No se pudo abrir el archivo para escribir MBR.\n");
    return false;
  }
  MBR m;
  memset(&m, '\0', sizeof(MBR));  // Limpiar basura del MBR
  m.size = static_cast<int>(sizeBytes);
  m.fit = fit;
  // Escribir MBR al inicio del archivo
  file.seekp(0);
  file.write(reinterpret_cast<const char*>(&m), sizeof(MBR));
  file.close();
  return true;
}

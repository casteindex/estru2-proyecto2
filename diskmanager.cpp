#include "diskmanager.h"

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <cstring>
#include <fstream>
#include <vector>

#include "diskmanager.h"
#include "terminal.h"

// ----------------------- Structs -------------------------
struct Partition {
  char status;    // 0 = libre, 1 = usada
  char type;      // P, E
  char fit;       // B, F, W
  int start;      // byte donde inicia
  int size;       // tamaño en bytes
  char name[16];  // nombre
};

struct MBR {
  int size;  // tamaño total del disco
  char fit;  // BF, FF, WF
  Partition parts[4];
};

struct EBR {
  char status;
  char fit;
  int start;
  int size;
  int next;  // siguiente EBR (posición física)
  char name[16];
};

struct Hueco {
  int inicio;
  int tam;
};

struct PartMontada {
  QString name;
  QString id;
};

struct DiscoMontado {
  QString path;
  char letra;
  std::vector<PartMontada> parts;
};

struct PartitionInfo {  // para el reporte
  QString name;         // "MBR", "LIBRE", "PRIMARIA", etc.
  int start;
  int size;
  QString type;
};

// -------------------- Helpers internos ---------------------
std::fstream openDiskForReadWrite(const QString& path) {
  return std::fstream(
    path.toStdString(), std::ios::in | std::ios::out | std::ios::binary);
}

std::fstream openDiskForRead(const QString& path) {
  return std::fstream(path.toStdString(), std::ios::in | std::ios::binary);
}

bool readMBR(std::fstream& file, MBR& out) {
  file.seekg(0);
  file.read(reinterpret_cast<char*>(&out), sizeof(MBR));
  return static_cast<bool>(file);
}

bool writeMBR(std::fstream& file, const MBR& mbr) {
  file.seekp(0);
  file.write(reinterpret_cast<const char*>(&mbr), sizeof(MBR));
  file.flush();
  return static_cast<bool>(file);
}

bool readEBRAt(std::fstream& file, long pos, EBR& out) {
  if (pos < 0) return false;
  file.seekg(pos);
  file.read(reinterpret_cast<char*>(&out), sizeof(EBR));
  return static_cast<bool>(file);
}

bool writeEBRAt(std::fstream& file, long pos, const EBR& ebr) {
  if (pos < 0) return false;
  file.seekp(pos);
  file.write(reinterpret_cast<const char*>(&ebr), sizeof(EBR));
  file.flush();
  return static_cast<bool>(file);
}

bool fileExists(const QString& path) {
  QFileInfo fi(path);
  return fi.exists() && fi.isFile();
}

// Devuelve true si hay slot disponible
bool haySlotDisponible(const MBR& mbr) {
  for (const auto& p : mbr.parts)
    if (p.status == 0) return true;
  return false;
}

std::vector<Partition> obtenerParticionesUsadasOrdenadas(const MBR& mbr) {
  std::vector<Partition> usadas;
  for (int i = 0; i < 4; ++i)
    if (mbr.parts[i].status == 1) usadas.push_back(mbr.parts[i]);
  std::sort(usadas.begin(), usadas.end(),
    [](const Partition& a, const Partition& b) { return a.start < b.start; });
  return usadas;
}

std::vector<Hueco> calcularHuecos(
  const std::vector<Partition>& usadas, int totalDiskSize) {
  std::vector<Hueco> huecos;
  int cursor = sizeof(MBR);
  for (const auto& p : usadas) {
    if (cursor < p.start) huecos.push_back({cursor, p.start - cursor});
    cursor = p.start + p.size;
  }
  if (cursor < totalDiskSize)
    huecos.push_back({cursor, totalDiskSize - cursor});
  return huecos;
}

Hueco elegirHueco(const std::vector<Hueco>& huecos, long sizeBytes, char fit) {
  Hueco elegido{-1, -1};
  if (huecos.empty()) return elegido;
  if (fit == 'F') {  // First Fit
    for (const auto& h : huecos)
      if (h.tam >= sizeBytes) {
        elegido = h;
        break;
      }
  } else if (fit == 'B') {  // Best Fit
    int best = INT_MAX;
    for (const auto& h : huecos)
      if (h.tam >= sizeBytes && h.tam < best) {
        best = h.tam;
        elegido = h;
      }
  } else {  // Worst Fit
    elegido = *std::max_element(huecos.begin(), huecos.end(),
      [](const Hueco& a, const Hueco& b) { return a.tam < b.tam; });
    if (elegido.tam < sizeBytes) return Hueco{-1, -1};
  }
  return elegido;
}

bool revisarNombreUnicoYExtendida(
  const MBR& mbr, const QString& name, char type, QPlainTextEdit* out) {
  for (const auto& p : mbr.parts) {
    if (type == 'E' && p.status == 1 && p.type == 'E') {
      if (out)
        out->appendPlainText("Ya existe una partición extendida en el disco.");
      return false;
    }
    if (p.status == 1 && name == QString::fromLatin1(p.name)) {
      if (out) out->appendPlainText("Ya existe una partición con ese nombre.");
      return false;
    }
  }
  return true;
}

// Encuentra y copia la partición extendida si existe, retorna true si existe
bool obtenerExtendida(const MBR& mbr, Partition& extendida) {
  for (const auto& p : mbr.parts) {
    if (p.status == 1 && p.type == 'E') {
      extendida = p;
      return true;
    }
  }
  return false;
}

// Lee los EBRs activos en la partición extendida y devuelve pares (EBR, posEBR)
std::vector<std::pair<EBR, long>> leerEBRsConPos(
  std::fstream& file, const Partition& extendida) {
  std::vector<std::pair<EBR, long>> lista;
  long inicioExt = extendida.start;
  long finExt = extendida.start + extendida.size;

  // Posición del primer EBR es inicioExt
  long pos = inicioExt;
  // Tope seguro de iteraciones
  // clang-format off
  size_t maxIter = static_cast<size_t>(extendida.size / std::max(1, static_cast<int>(sizeof(EBR)))) + 10;
  size_t iter = 0;
  // clang-format on
  while (pos >= inicioExt && pos + static_cast<long>(sizeof(EBR)) <= finExt &&
         iter < maxIter) {
    EBR ebr;
    if (!readEBRAt(file, pos, ebr)) break;
    if (ebr.status == 1) lista.push_back({ebr, pos});

    long nextPos = ebr.next;
    // Si next es inválido o no avanza, intentar avanzar físicamente
    if (nextPos <= pos || nextPos < inicioExt ||
        nextPos + static_cast<long>(sizeof(EBR)) > finExt) {
      // Si ebr.size > 0, intentar saltar al final de esta lógica
      if (ebr.size > 0) {
        long candidate = pos + static_cast<long>(sizeof(EBR)) + ebr.size;
        if (candidate > pos &&
            candidate + static_cast<long>(sizeof(EBR)) <= finExt) {
          pos = candidate;
        } else {
          break;
        }
      } else break;  // No hay tamaño, ya no hay más EBRs
    } else pos = nextPos;
    iter++;
  }
  return lista;
}

bool nombreLogicaDisponible(
  const std::vector<std::pair<EBR, long>>& ebrsPos, const QString& name) {
  for (const auto& p : ebrsPos) {
    const EBR& e = p.first;
    if (e.status == 1 && name == QString::fromLatin1(e.name)) return false;
  }
  return true;
}

// Calcula huecos dentro de la extendida usando las posiciones de EBR
std::vector<Hueco> calcularHuecosEnExtendida(
  const Partition& ext, const std::vector<std::pair<EBR, long>>& ebrsPos) {
  std::vector<Hueco> huecos;
  long inicioExt = ext.start;
  long finExt = ext.start + ext.size;
  if (ebrsPos.empty()) {
    huecos.push_back({static_cast<int>(inicioExt), static_cast<int>(ext.size)});
    return huecos;
  }

  // Ordenar por posición del EBR
  std::vector<std::pair<EBR, long>> sorted = ebrsPos;
  std::sort(sorted.begin(), sorted.end(),
    [](const auto& a, const auto& b) { return a.second < b.second; });

  // Antes del primer EBR
  long posPrim = sorted[0].second;
  if (posPrim > inicioExt)
    huecos.push_back(
      {static_cast<int>(inicioExt), static_cast<int>(posPrim - inicioExt)});

  for (size_t i = 0; i + 1 < sorted.size(); ++i) {
    long posThis = sorted[i].second;
    long sizeThis = sorted[i].first.size;
    long finThis = posThis + static_cast<long>(sizeof(EBR)) + sizeThis;
    long posNext = sorted[i + 1].second;
    if (posNext > finThis)
      huecos.push_back(
        {static_cast<int>(finThis), static_cast<int>(posNext - finThis)});
  }
  long posLast = sorted.back().second;
  long finLast =
    posLast + static_cast<long>(sizeof(EBR)) + sorted.back().first.size;
  if (finLast < finExt)
    huecos.push_back(
      {static_cast<int>(finLast), static_cast<int>(finExt - finLast)});
  return huecos;
}

// Inserta una partición en MBR
bool insertarParticionEnMBR(MBR& mbr, const QString& name, char type, char fit,
  long sizeBytes, int inicio) {
  int slot = -1;
  for (int i = 0; i < 4; ++i) {
    if (mbr.parts[i].status == 0) {
      slot = i;
      break;
    }
  }
  if (slot == -1) return false;
  Partition& p = mbr.parts[slot];
  p.status = 1;
  p.type = type;
  p.fit = fit;
  p.start = inicio;
  p.size = static_cast<int>(sizeBytes);
  std::memset(p.name, 0, sizeof(p.name));
  strncpy(p.name, name.toStdString().c_str(), sizeof(p.name) - 1);
  return true;
}

// Escribe nuevo EBR en posición posEBR y corrige enlaces prev/next que existan
bool escribirNuevoEBRConEnlaces(std::fstream& file, const Partition& extendida,
  const std::vector<std::pair<EBR, long>>& ebrsPos, long posEBR, long sizeBytes,
  char fit, const QString& name) {
  long inicioExt = extendida.start;
  long finExt = extendida.start + extendida.size;
  if (posEBR < inicioExt) return false;
  if (posEBR + static_cast<long>(sizeof(EBR)) + sizeBytes > finExt)
    return false;

  EBR nuevo;
  memset(&nuevo, 0, sizeof(EBR));
  nuevo.status = 1;
  nuevo.fit = fit;
  nuevo.start = static_cast<int>(posEBR + static_cast<long>(sizeof(EBR)));
  nuevo.size = static_cast<int>(sizeBytes);
  nuevo.next = -1;
  strncpy(nuevo.name, name.toStdString().c_str(), sizeof(nuevo.name) - 1);

  // Ordenar por posición física
  std::vector<std::pair<EBR, long>> sorted = ebrsPos;
  std::sort(sorted.begin(), sorted.end(),
    [](const auto& a, const auto& b) { return a.second < b.second; });

  long prevPos = -1;
  long nextPos = -1;
  for (size_t i = 0; i < sorted.size(); ++i) {
    long p = sorted[i].second;
    if (p < posEBR) prevPos = p;
    else if (p > posEBR && nextPos == -1) nextPos = p;
  }
  if (nextPos != -1) nuevo.next = static_cast<int>(nextPos);
  else nuevo.next = -1;

  // Actualizar prev.next si aplica
  if (prevPos != -1) {
    EBR prevEBR;
    if (!readEBRAt(file, prevPos, prevEBR)) return false;
    prevEBR.next = static_cast<int>(posEBR);
    if (!writeEBRAt(file, prevPos, prevEBR)) return false;
  }
  // Escribir nuevo EBR
  if (!writeEBRAt(file, posEBR, nuevo)) return false;
  return true;
}

// ---------------- Implementaciones DiskManager  --------------------

void DiskManager::mkdisk(
  const QStringList& args, QPlainTextEdit* out, const QDir& currentDir) {
  long sizeBytes = 0;
  char fit = 'F';      // Primer ajuste por defecto
  QString unit = "m";  // Megabytes por defecto
  QString rawPath;

  if (!mkdiskParams(args, sizeBytes, fit, rawPath, unit, out)) return;
  QDir base = currentDir;
  QString finalPath = base.absoluteFilePath(rawPath);
  QFileInfo info(finalPath);
  QDir dir = info.dir();
  if (!dir.exists()) {
    if (!dir.mkpath(".")) {
      out->appendPlainText("No se pudieron crear las carpetas.\n");
      return;
    }
  }
  QString raidPath;
  int pos = finalPath.lastIndexOf(".disk");
  raidPath = finalPath.left(pos) + "_raid.disk";
  // Crear discos
  {
    std::fstream f(finalPath.toStdString(), std::ios::out | std::ios::binary);
    if (!f.is_open()) {
      out->appendPlainText("No se pudo crear el archivo.\n");
      return;
    }
    if (sizeBytes > 0) {
      f.seekp(sizeBytes - 1);
      f.write("\0", 1);
    }
    f.close();
  }
  {
    std::fstream f(raidPath.toStdString(), std::ios::out | std::ios::binary);
    if (!f.is_open()) {
      out->appendPlainText("No se pudo crear el archivo RAID.\n");
      return;
    }
    if (sizeBytes > 0) {
      f.seekp(sizeBytes - 1);
      f.write("\0", 1);
    }
    f.close();
  }
  // Escribir MBR inicial
  {
    std::fstream file = openDiskForReadWrite(finalPath);
    if (!file.is_open()) {
      out->appendPlainText("No se pudo abrir el archivo para escribir MBR.\n");
      return;
    }
    MBR m;
    memset(&m, '\0', sizeof(MBR));
    m.size = static_cast<int>(sizeBytes);
    m.fit = fit;
    if (!writeMBR(file, m)) {
      out->appendPlainText("Error al escribir MBR.\n");
      return;
    }
    file.close();
  }
  {
    std::fstream file = openDiskForReadWrite(raidPath);
    if (!file.is_open()) {
      out->appendPlainText("No se pudo abrir RAID para escribir MBR.\n");
      return;
    }
    MBR m;
    memset(&m, '\0', sizeof(MBR));
    m.size = static_cast<int>(sizeBytes);
    m.fit = fit;
    if (!writeMBR(file, m)) {
      out->appendPlainText("Error al escribir MBR RAID.\n");
      return;
    }
    file.close();
  }
  out->appendPlainText("Disco creado con éxito.\n");
}

bool DiskManager::mkdiskParams(const QStringList& args, long& sizeBytes,
  char& fit, QString& path, QString& unit, QPlainTextEdit* out) {
  bool sizeFound = false;
  bool pathFound = false;

  // Recorrer la lista de argumentos buscando los argumentos requeridos
  for (const QString& arg : args) {
    QString lowerArg = arg.toLower();
    if (lowerArg.startsWith("-size=")) {
      long size = arg.mid(6).toLong();
      if (size <= 0) {
        out->appendPlainText("Size debe ser mayor que 0.\n");
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
        out->appendPlainText("Fit inválido.\n");
        return false;
      }
    } else if (lowerArg.startsWith("-unit=")) {
      unit = arg.mid(6).toLower();
      if (unit != "k" && unit != "m") {
        out->appendPlainText("Unit inválido, use K o M.\n");
        return false;
      }
    } else if (lowerArg.startsWith("-path=")) {
      pathFound = true;
      path = arg.mid(6);  // mantener mayúsculas
      if (!path.endsWith(".disk")) {
        out->appendPlainText("Extensión de disco inválida.\n");
        return false;
      }
    }
  }
  // Validaciones
  if (!sizeFound) {
    out->appendPlainText("Falta parámetro size.\n");
    return false;
  }
  if (!pathFound) {
    out->appendPlainText("Falta parámetro path.\n");
    return false;
  }
  // Convertir a bytes según unit
  if (unit == "k") sizeBytes *= 1024;
  else sizeBytes *= 1024 * 1024;
  return true;  // Se encontraron los parámetros obligatorios
}

// RMDISK
void DiskManager::rmdisk(const QStringList& args, QPlainTextEdit* out,
  const QDir& currentDir, Terminal* terminal) {
  if (args.isEmpty()) {
    out->appendPlainText("Falta parámetro path.\n");
    return;
  }
  QString rawPath;
  for (const QString& a : args) {
    if (a.toLower().startsWith("-path=")) rawPath = a.mid(6);
  }
  if (rawPath.isEmpty()) {
    out->appendPlainText("Falta parámetro path.\n");
    return;
  }
  QDir base = currentDir;
  QString finalPath = base.absoluteFilePath(rawPath);
  if (!finalPath.endsWith(".disk")) {
    out->appendPlainText("Extensión de disco inválida.\n");
    return;
  }
  QFile* file = new QFile(finalPath);
  if (!file->exists()) {
    out->appendPlainText("El archivo no existe.\n");
    delete file;
    return;
  }
  terminal->esperandoConfirmacion = true;
  terminal->prompt = ">> ¿Seguro que desea eliminar el disco? Y/N: ";

  QObject::connect(
    terminal, &Terminal::confirmacionRecibida, terminal, [=](char r) mutable {
      if (r == 'y') {
        if (!file->remove())
          out->appendPlainText("No se pudo eliminar el archivo.\n");
        else out->appendPlainText("Disco eliminado con éxito.\n");
      } else if (r == 'n') {
        out->appendPlainText("Operación cancelada.\n");
      } else {
        out->appendPlainText("Entrada inválida. Operación cancelada.\n");
      }
      file->deleteLater();
      terminal->esperandoConfirmacion = false;
      QObject::disconnect(
        terminal, &Terminal::confirmacionRecibida, nullptr, nullptr);
    });
}

// FDISK
void DiskManager::fdisk(const QStringList& args, QPlainTextEdit* out,
  const QDir& currentDir, Terminal* terminal) {
  long sizeBytes = 0;
  char unit = 'k';
  char type = 'P';
  char fit = 'W';
  long addValue = 0;
  QString deleteMode;
  QString name;
  QString rawPath;

  if (!fdiskParams(args, sizeBytes, unit, type, rawPath, name, deleteMode,
        addValue, fit, out))
    return;
  QDir base = currentDir;
  QString finalPath = base.absoluteFilePath(rawPath);
  if (!deleteMode.isEmpty()) {
    if (!deleteParticion(finalPath, name, out, terminal))
      out->appendPlainText("Error al eliminar la partición " + name + ".\n");
    return;
  }
  if (addValue != 0) {
    if (addAParticion(finalPath, name, addValue, out))
      out->appendPlainText("Espacio modificado para " + name + ".\n");
    else
      out->appendPlainText("Error al modificar espacio para " + name + ".\n");
    return;
  }
  switch (type) {
    case 'P':
      if (crearPrimaria(finalPath, name, sizeBytes, fit, out))
        out->appendPlainText("...\nPartición primaria creada con éxito.\n");
      else out->appendPlainText("Error al crear partición primaria.\n");
      break;
    case 'E':
      if (crearExtendida(finalPath, name, sizeBytes, fit, out))
        out->appendPlainText("...\nPartición extendida creada con éxito.\n");
      else out->appendPlainText("Error al crear partición extendida.\n");
      break;
    case 'L':
      if (crearLogica(finalPath, name, sizeBytes, fit, out))
        out->appendPlainText("...\nPartición lógica creada con éxito.\n");
      else out->appendPlainText("Error al crear partición lógica.\n");
      break;
  }
}

bool DiskManager::fdiskParams(const QStringList& args, long& sizeBytes,
  char& unit, char& type, QString& path, QString& name, QString& deleteMode,
  long& addValue, char& fit, QPlainTextEdit* out) {
  bool sizeFound = false;
  bool pathFound = false;
  bool nameFound = false;
  bool deleteFound = false;
  bool addFound = false;

  for (const QString& a : args) {
    QString low = a.toLower();
    if (low.startsWith("-size=")) {
      long size = a.mid(6).toLong();
      if (size <= 0) {
        out->appendPlainText("Size debe ser mayor que 0.\n");
        return false;
      }
      sizeBytes = size;  // se convierte a bytes después
      sizeFound = true;
    } else if (low.startsWith("-unit=")) {
      QChar u = low.mid(6)[0];
      if (u == 'k' || u == 'm' || u == 'b') unit = u.toLatin1();
      else {
        out->appendPlainText("Unidad inválida, use K, M o B).\n");
        return false;
      }
    } else if (low.startsWith("-path=")) {
      path = a.mid(6);
      pathFound = true;
    } else if (low.startsWith("-name=")) {
      name = a.mid(6);
      nameFound = true;
    } else if (low.startsWith("-type=")) {
      QChar t = low.mid(6)[0];
      if (t == 'p' || t == 'e' || t == 'l') type = t.toUpper().toLatin1();
      else {
        out->appendPlainText("Tipo inválido, use P, E o L).\n");
        return false;
      }
    } else if (low.startsWith("-fit=")) {
      QString f = low.mid(5).toUpper();
      if (f == "BF") fit = 'B';
      else if (f == "FF") fit = 'F';
      else if (f == "WF") fit = 'W';
      else {
        out->appendPlainText("Fit inválido (BF, FF o WF).\n");
        return false;
      }
    } else if (low.startsWith("-delete=")) {
      deleteMode = low.mid(8);  // fast/full
      deleteFound = true;
    } else if (low.startsWith("-add=")) {
      long size = a.mid(5).toLong();
      addValue = size;  // convertir después
      addFound = true;
    }
  }
  // Validaciones
  if (!pathFound) {
    out->appendPlainText("Falta parámetro path.\n");
    return false;
  }
  if (!nameFound) {
    out->appendPlainText("Falta parámetro name.\n");
    return false;
  }
  // Resolver conflicto: no se puede usar delete y add al mismo tiempo
  if (deleteFound && addFound) {
    out->appendPlainText("No se puede usar -delete y -add al mismo tiempo.\n");
    return false;
  }
  if (deleteFound) {
    if (deleteMode != "fast" && deleteMode != "full") {
      out->appendPlainText("Valor inválido para -delete (use fast o full).\n");
      return false;
    }
    if (sizeFound) {
      out->appendPlainText("No se debe usar -size con -delete.\n");
      return false;
    }
    return true;
  }
  if (addFound) {
    if (sizeFound) {
      out->appendPlainText("No se debe usar -size con -add.\n");
      return false;
    }
    switch (unit) {
      case 'b': break;
      case 'k': addValue *= 1024; break;
      case 'm': addValue *= 1024 * 1024; break;
    }
    return true;
  }
  // Si no es delete ni add -> estamos creando
  if (!sizeFound) {
    out->appendPlainText("Falta parámetro size para crear particiones.\n");
    return false;
  }
  // Convertir size a bytes
  switch (unit) {
    case 'b': break;
    case 'k': sizeBytes *= 1024; break;
    case 'm': sizeBytes *= 1024 * 1024; break;
  }
  return true;
}

// Crear partición genérica
bool crearParticionGenerica(const QString& path, const QString& name, char type,
  long sizeBytes, char fit, QPlainTextEdit* out, bool silencioso = false) {
  std::fstream file = openDiskForReadWrite(path);
  if (!file.is_open()) {
    if (!silencioso) out->appendPlainText("No se pudo abrir el disco.");
    return false;
  }
  MBR mbr;
  if (!readMBR(file, mbr)) {
    if (!silencioso) out->appendPlainText("No se pudo leer MBR.");
    file.close();
    return false;
  }
  if (!haySlotDisponible(mbr)) {
    if (!silencioso)
      out->appendPlainText("No hay slots de partición disponibles.");
    file.close();
    return false;
  }
  if (!revisarNombreUnicoYExtendida(mbr, name, type, out)) {
    file.close();
    return false;
  }
  auto usadas = obtenerParticionesUsadasOrdenadas(mbr);
  auto huecos = calcularHuecos(usadas, mbr.size);
  if (!silencioso) {
    int maxHueco = 0;
    for (const auto& h : huecos)
      if (h.tam > maxHueco) maxHueco = h.tam;
    out->appendPlainText(
      "Espacio disponible: " + QString::number(maxHueco) + " Bytes");
    out->appendPlainText(
      "Espacio necesario : " + QString::number(sizeBytes) + " Bytes");
    if (maxHueco < sizeBytes) {
      out->appendPlainText("...\nNo hay espacio suficiente.");
      file.close();
      return false;
    }
  }
  Hueco elegido = elegirHueco(huecos, sizeBytes, fit);
  if (elegido.inicio == -1) {
    if (!silencioso)
      out->appendPlainText(
        "...\nNo se encontró un hueco adecuado según el fit.");
    file.close();
    return false;
  }
  if (!insertarParticionEnMBR(
        mbr, name, type, fit, sizeBytes, elegido.inicio)) {
    if (!silencioso)
      out->appendPlainText("...\nNo hay slots de partición disponibles.");
    file.close();
    return false;
  }
  // Si extendida, crear EBR inicial (inactivo)
  if (type == 'E') {
    EBR ebr;
    memset(&ebr, 0, sizeof(EBR));
    ebr.status = 0;
    ebr.fit = fit;
    ebr.start = elegido.inicio;
    ebr.size = 0;
    ebr.next = -1;
    file.seekp(elegido.inicio);
    file.write(reinterpret_cast<char*>(&ebr), sizeof(EBR));
  }
  // Guardar MBR
  file.seekp(0);
  file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
  file.flush();
  file.close();
  return true;
}

bool DiskManager::crearPrimaria(const QString& path, const QString& name,
  long sizeBytes, char fit, QPlainTextEdit* out) {
  QString raidPath;
  int pos = path.lastIndexOf(".disk");
  raidPath = path.left(pos) + "_raid.disk";
  crearParticionGenerica(raidPath, name, 'P', sizeBytes, fit, out, true);
  return crearParticionGenerica(path, name, 'P', sizeBytes, fit, out, false);
}

bool DiskManager::crearExtendida(const QString& path, const QString& name,
  long sizeBytes, char fit, QPlainTextEdit* out) {
  QString raidPath;
  int pos = path.lastIndexOf(".disk");
  raidPath = path.left(pos) + "_raid.disk";
  crearParticionGenerica(raidPath, name, 'E', sizeBytes, fit, out, true);
  return crearParticionGenerica(path, name, 'E', sizeBytes, fit, out, false);
}

// Crear lógica
bool DiskManager::crearLogica(const QString& path, const QString& name,
  long sizeBytes, char fitUser, QPlainTextEdit* out) {
  std::fstream file = openDiskForReadWrite(path);
  if (!file.is_open()) {
    out->appendPlainText("No se pudo abrir el disco.");
    return false;
  }
  MBR mbr;
  if (!readMBR(file, mbr)) {
    out->appendPlainText("No se pudo leer MBR.");
    file.close();
    return false;
  }
  Partition extendida;
  if (!obtenerExtendida(mbr, extendida)) {
    out->appendPlainText("No existe una partición extendida.");
    file.close();
    return false;
  }
  auto ebrsPos = leerEBRsConPos(file, extendida);
  if (!nombreLogicaDisponible(ebrsPos, name)) {
    out->appendPlainText("Ya existe una partición lógica con ese nombre.");
    file.close();
    return false;
  }
  auto huecos = calcularHuecosEnExtendida(extendida, ebrsPos);
  int maxHueco = 0;
  for (const auto& h : huecos)
    if (h.tam > maxHueco) maxHueco = h.tam;
  out->appendPlainText(
    "Espacio disponible: " + QString::number(maxHueco) + " Bytes");
  out->appendPlainText(
    "Espacio necesario : " + QString::number(sizeBytes) + " Bytes");
  if (maxHueco < sizeBytes + static_cast<int>(sizeof(EBR))) {
    out->appendPlainText(
      "...\nNo hay espacio suficiente dentro de la extendida.");
    file.close();
    return false;
  }

  Hueco elegido = elegirHueco(
    huecos, sizeBytes + static_cast<int>(sizeof(EBR)), extendida.fit);
  if (elegido.inicio == -1) {
    out->appendPlainText(
      "No se encontró un hueco adecuado dentro de la extendida.");
    file.close();
    return false;
  }
  long posEBR = elegido.inicio;

  // Escribir nuevo EBR en disco principal
  if (!escribirNuevoEBRConEnlaces(
        file, extendida, ebrsPos, posEBR, sizeBytes, extendida.fit, name)) {
    out->appendPlainText("Error al escribir EBR en disco principal.");
    file.close();
    return false;
  }
  // Escribir en RAID (mismo offset)
  QString raidPath;
  int p = path.lastIndexOf(".disk");
  raidPath = path.left(p) + "_raid.disk";
  std::fstream fileRaid = openDiskForReadWrite(raidPath);
  if (!fileRaid.is_open()) {
    out->appendPlainText("EBR creado en principal pero fallo al abrir RAID.");
    file.close();
    return false;
  }
  // Leer MBR RAID y extendida para validar posición
  MBR mbrRaid;
  if (!readMBR(fileRaid, mbrRaid)) {
    out->appendPlainText(
      "EBR creado en principal pero fallo al leer MBR RAID.");
    fileRaid.close();
    file.close();
    return false;
  }
  Partition extRaid;
  if (!obtenerExtendida(mbrRaid, extRaid)) {
    out->appendPlainText(
      "EBR creado en principal pero RAID no tiene extendida.");
    fileRaid.close();
    file.close();
    return false;
  }
  auto ebrsPosRaid = leerEBRsConPos(fileRaid, extRaid);
  if (!escribirNuevoEBRConEnlaces(
        fileRaid, extRaid, ebrsPosRaid, posEBR, sizeBytes, extRaid.fit, name)) {
    out->appendPlainText("EBR creado en principal pero fallo en RAID.");
  }
  fileRaid.close();
  file.close();
  return true;
}

// deleteParticionInterno (helper usado por deleteParticion)
static bool deleteParticionInterno(const QString& path, const QString& name) {
  std::fstream file = openDiskForReadWrite(path);
  if (!file.is_open()) return false;
  MBR mbr;
  if (!readMBR(file, mbr)) {
    file.close();
    return false;
  }
  bool encontrada = false;
  // buscar en MBR
  for (auto& p : mbr.parts) {
    if (p.status == 1 && name == QString::fromLatin1(p.name)) {
      p.status = 0;
      encontrada = true;
      break;
    }
  }
  // buscar en lógicas
  if (!encontrada) {
    Partition extendida;
    if (obtenerExtendida(mbr, extendida)) {
      auto ebrs = leerEBRsConPos(file, extendida);
      for (const auto& [ebr, pos] : ebrs) {
        if (ebr.status == 1 && name == QString::fromLatin1(ebr.name)) {
          EBR mod = ebr;
          mod.status = 0;
          writeEBRAt(file, pos, mod);
          encontrada = true;
          break;
        }
      }
    }
  }
  if (!encontrada) {
    file.close();
    return false;
  }
  // Guardar MBR
  if (!writeMBR(file, mbr)) {
    file.close();
    return false;
  }
  file.close();
  return true;
}

bool DiskManager::deleteParticion(const QString& path, const QString& name,
  QPlainTextEdit* out, Terminal* terminal) {
  // Abrir disco principal
  // Nota: debe ser un puntero para poder usarse en la función lambda
  std::fstream* file = new std::fstream(
    path.toStdString(), std::ios::in | std::ios::out | std::ios::binary);
  if (!file->is_open()) {
    out->appendPlainText("No se pudo abrir el disco.");
    delete file;
    return false;
  }
  MBR mbr;
  file->read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
  // Determinar tipo de partición
  char tipo = '\0';
  bool encontrada = false;
  for (const auto& p : mbr.parts) {
    if (p.status == 1 && name == QString(p.name)) {
      tipo = p.type;  // 'P' o 'E'
      encontrada = true;
      break;
    }
  }
  // Buscar en EBRs si no fue primaria/extendida
  Partition extendida;
  std::vector<std::pair<EBR, long>> ebrsPos;
  if (!encontrada && obtenerExtendida(mbr, extendida)) {
    ebrsPos = leerEBRsConPos(*file, extendida);
    for (const auto& [ebr, pos] : ebrsPos) {
      if (ebr.status == 1 && name == QString(ebr.name)) {
        tipo = 'L';
        encontrada = true;
        break;
      }
    }
  }
  if (!encontrada) {
    out->appendPlainText("No se encontró la partición.");
    file->close();
    return false;
  }
  // Confirmación de borrado
  terminal->esperandoConfirmacion = true;
  terminal->prompt = ">> ¿Seguro que desea eliminar la particion? Y/N: ";
  QObject::connect(
    terminal, &Terminal::confirmacionRecibida, terminal, [=](char r) mutable {
      if (r == 'y') {
        bool exito = false;
        // Eliminar primaria o extendida
        if (tipo == 'P' || tipo == 'E') {
          for (auto& p : mbr.parts) {
            if (p.status == 1 && name == QString(p.name)) {
              p.status = 0;
              exito = true;
              break;
            }
          }
          // Si es extendida, borrar todos los EBRs dentro también
          if (tipo == 'E' && exito) {
            if (obtenerExtendida(mbr, extendida)) {
              auto ebrs = leerEBRsConPos(*file, extendida);
              for (const auto& [ebr, pos] : ebrs) {
                EBR mod = ebr;
                mod.status = 0;
                writeEBRAt(*file, pos, mod);
              }
            }
          }
        }
        // Eliminar lógica
        else if (tipo == 'L') {
          for (const auto& [ebr, pos] : ebrsPos) {
            if (ebr.status == 1 && name == QString(ebr.name)) {
              EBR mod = ebr;
              mod.status = 0;
              exito = writeEBRAt(*file, pos, mod);
              break;
            }
          }
        }
        // Guardar MBR actualizado
        file->seekp(0);
        file->write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        file->close();
        // Actualizar RAID sin imprimir nada
        QString raidPath = path;
        int p = raidPath.lastIndexOf(".disk");
        raidPath = raidPath.left(p) + "_raid.disk";
        deleteParticionInterno(raidPath, name);
        if (exito) {
          QString msg = "Particion ";
          if (tipo == 'P') msg += "primaria";
          else if (tipo == 'E') msg += "extendida";
          else if (tipo == 'L') msg += "logica";
          msg += " eliminada con exito.\n";
          out->appendPlainText(msg);
        } else {
          out->appendPlainText("Error al eliminar la partición.\n");
        }
      } else if (r == 'n' || r == 'N') {
        out->appendPlainText("Operacion cancelada.\n");
      } else {
        out->appendPlainText("Entrada invalida. Operacion cancelada.\n");
      }
      terminal->esperandoConfirmacion = false;
      QObject::disconnect(
        terminal, &Terminal::confirmacionRecibida, nullptr, nullptr);
    });
  return true;
}

// -------------- Add a Particion --------------
bool modificarLogica(std::fstream& file, MBR& mbr, const QString& name,
  long addBytes, const QString& raidPath, QPlainTextEdit* out) {
  // Localizar la Extendida
  Partition extendida;
  if (!obtenerExtendida(mbr, extendida)) {
    out->appendPlainText(
      "No existe partición extendida para modificar lógica.");
    return false;
  }
  // Leer EBRs para encontrar la lógica
  auto ebrsPos = leerEBRsConPos(file, extendida);
  long currentEBRPos = -1;  // Posición de inicio del EBR a modificar
  EBR objetivoEBR;
  for (const auto& [ebr, pos] : ebrsPos) {
    if (ebr.status == 1 && name == QString::fromLatin1(ebr.name)) {
      objetivoEBR = ebr;
      currentEBRPos = pos;
      break;
    }
  }
  if (currentEBRPos == -1) {
    out->appendPlainText("No se encontró la partición lógica '" + name + ".");
    return false;
  }
  long nuevoSize = static_cast<long>(objetivoEBR.size) + addBytes;

  // Validación de reducción (addBytes < 0)
  if (nuevoSize <= 0) {
    out->appendPlainText(
      "El tamaño resultante debe ser un entero positivo."
      "\nTamaño actual: " +
      QString::number(objetivoEBR.size) +
      " Bytes \nSolicitud:     " + QString::number(addBytes) + "Bytes\n...");
    return false;
  }
  // Validación de expansión (addBytes > 0)
  if (addBytes > 0) {
    // Posición del fin de la DATA de la lógica actual
    long finActualData = objetivoEBR.start + objetivoEBR.size;
    long maxExpansion = 0;
    long nextEBRPos = objetivoEBR.next;
    if (nextEBRPos == -1) {
      // Es la última: el límite es el fin de la Extendida
      maxExpansion = extendida.start + extendida.size - finActualData;
    } else {
      // El límite es el inicio del siguiente EBR.
      maxExpansion = nextEBRPos - finActualData;
    }
    if (addBytes > maxExpansion) {
      out->appendPlainText(
        "No hay espacio suficiente para expandir la lógica.\nMáx. "
        "disponible: " +
        QString::number(maxExpansion) + " Bytes\n...");
      return false;
    }
  }

  // Aplicar cambio y guardar EBR
  objetivoEBR.size = static_cast<int>(nuevoSize);
  if (!writeEBRAt(file, currentEBRPos, objetivoEBR)) {
    out->appendPlainText(
      "Error al escribir el EBR modificado en el disco principal.\n");
    return false;
  }
  // Actualizar RAID
  std::fstream fileRaid = openDiskForReadWrite(raidPath);
  if (fileRaid.is_open()) {
    if (!writeEBRAt(fileRaid, currentEBRPos, objetivoEBR)) {
      out->appendPlainText("Falló la escritura del EBR en RAID.");
    }
    fileRaid.close();
  } else {
    out->appendPlainText("Falló al abrir RAID para modificar EBR");
  }
  out->appendPlainText(
    "Partición lógica modificada correctamente.\nNuevo tamaño: " +
    QString::number(nuevoSize) + " Bytes\n...");
  return true;
}

bool DiskManager::addAParticion(const QString& path, const QString& name,
  long addBytes, QPlainTextEdit* out) {
  std::fstream file = openDiskForReadWrite(path);
  if (!file.is_open()) {
    out->appendPlainText("No se pudo abrir el disco.");
    return false;
  }
  MBR mbr;
  if (!readMBR(file, mbr)) {
    out->appendPlainText("No se pudo leer MBR.");
    file.close();
    return false;
  }
  QString raidPath;
  int pos = path.lastIndexOf(".disk");
  raidPath = path.left(pos) + "_raid.disk";
  Partition* objetivoMBR = nullptr;

  // Buscar en MBR y determinar tipo
  for (auto& p : mbr.parts) {
    if (p.status == 1 && name == QString::fromLatin1(p.name)) {
      if (p.type == 'P' || p.type == 'E') {
        objetivoMBR = &p;
        break;
      } else {  // Lógica
        bool success =
          modificarLogica(file, mbr, name, addBytes, raidPath, out);
        file.close();
        return success;
      }
    }
  }
  // Buscar en EBRs si no se encontró en MBR
  if (!objetivoMBR) {
    Partition tempExt;
    if (obtenerExtendida(mbr, tempExt)) {
      auto ebrs = leerEBRsConPos(file, tempExt);
      for (const auto& [ebr, pos] : ebrs) {
        if (ebr.status == 1 && name == QString::fromLatin1(ebr.name)) {
          bool success =
            modificarLogica(file, mbr, name, addBytes, raidPath, out);
          file.close();
          return success;
        }
      }
    }
  }
  // Si todavía no hay objetivo, no existe la partición.
  if (!objetivoMBR) {
    out->appendPlainText(
      "No se encontró la partición con el nombre '" + name + "'.");
    file.close();
    return false;
  }

  // Manejar Primaria/Extendida
  long nuevoSize = static_cast<long>(objetivoMBR->size) + addBytes;
  // Validación de reducción (addBytes < 0)
  if (nuevoSize <= 0) {
    out->appendPlainText("El tamaño resultante debe ser un entero positivo.");
    file.close();
    return false;
  }
  // Validación de expansión (addBytes > 0)
  if (addBytes > 0) {
    auto usadas = obtenerParticionesUsadasOrdenadas(mbr);
    auto huecos = calcularHuecos(usadas, mbr.size);
    long finActual = objetivoMBR->start + objetivoMBR->size;
    long espacioDisponible = 0;
    // Buscar el hueco siguiente
    for (auto& h : huecos) {
      if (h.inicio == finActual) {
        espacioDisponible = h.tam;
        break;
      }
    }
    if (espacioDisponible < addBytes) {
      out->appendPlainText(
        "No hay espacio suficiente para expandir.\nMáx. disponible: " +
        QString::number(espacioDisponible) + " Bytes\n...");
      file.close();
      return false;
    }
  }

  // Guardar MBR
  objetivoMBR->size = static_cast<int>(nuevoSize);
  if (!writeMBR(file, mbr)) {
    out->appendPlainText("Error al guardar MBR en el disco principal.");
    file.close();
    return false;
  }
  file.close();
  // Lo mismo al RAID
  std::fstream fileRaid = openDiskForReadWrite(raidPath);
  if (fileRaid.is_open()) {
    MBR mbrRaid;
    if (readMBR(fileRaid, mbrRaid)) {
      for (auto& p : mbrRaid.parts) {
        if (p.status == 1 && name == QString::fromLatin1(p.name)) {
          p.size = static_cast<int>(nuevoSize);
          writeMBR(fileRaid, mbrRaid);
          break;
        }
      }
    }
    fileRaid.close();
  } else {
    out->appendPlainText("No se pudo abrir el disco RAID para actualizar.");
  }
  out->appendPlainText("Partición modificada correctamente.\nNuevo tamaño: " +
                       QString::number(nuevoSize) + " Bytes\n...");
  return true;
}

// MOUNT / UNMOUNT
static std::vector<DiscoMontado> discosMontados;

void imprimirParticionesDisco(QPlainTextEdit* out, const DiscoMontado& disco) {
  const int largoLinea = 34;
  const int largoNombre = 20;
  const int largoId = 9;

  QString encabezado;
  encabezado += QString("-").repeated(largoLinea) + "\n";
  encabezado += "|      Particiones Montadas      |\n";
  encabezado += QString("-").repeated(largoLinea) + "\n";
  encabezado += "| Nombre              | ID       |\n";
  encabezado += QString("-").repeated(largoLinea) + "\n";

  for (const PartMontada& p : disco.parts) {
    encabezado += "| " + p.name;
    encabezado += QString(" ").repeated(largoNombre - p.name.length()) + "| ";
    encabezado += p.id + QString(" ").repeated(largoId - p.id.length()) + "|\n";
  }
  encabezado += QString("-").repeated(largoLinea) + "\n";
  out->appendPlainText(encabezado);
}

int primerNumeroDisponible(const DiscoMontado& disco) {
  std::vector<int> usados;
  for (const PartMontada& p : disco.parts) {
    QString numStr = p.id.mid(3);
    usados.push_back(numStr.toInt());
  }
  std::sort(usados.begin(), usados.end());
  int numero = 1;
  for (int u : usados) {
    if (u == numero) numero++;
    else break;
  }
  return numero;
}

char primeraLetraDisponible() {
  std::vector<char> usadas;
  for (const DiscoMontado& d : discosMontados) usadas.push_back(d.letra);
  std::sort(usadas.begin(), usadas.end());
  char letra = 'a';
  for (char u : usadas) {
    if (u == letra) letra++;
    else break;
  }
  return letra;
}

void DiskManager::mount(
  const QStringList& args, QPlainTextEdit* out, const QDir& currentDir) {
  if (args.isEmpty()) {
    out->appendPlainText("Falta parámetro path.\n");
    return;
  }
  QString rawPath;
  QString name;
  for (const QString& a : args) {
    if (a.toLower().startsWith("-path=")) rawPath = a.mid(6);
    if (a.toLower().startsWith("-name=")) name = a.mid(6);
  }
  if (rawPath.isEmpty()) {
    out->appendPlainText("Falta parámetro path.\n");
    return;
  }
  if (name.isEmpty()) {
    out->appendPlainText("Falta parámetro name.\n");
    return;
  }

  QDir base = currentDir;
  QString finalPath = base.absoluteFilePath(rawPath);
  if (!finalPath.endsWith(".disk")) {
    out->appendPlainText("Extensión de disco inválida.\n");
    return;
  }
  std::fstream file = openDiskForRead(finalPath);
  if (!file.is_open()) {
    out->appendPlainText("No se pudo abrir el disco.\n");
    return;
  }
  MBR mbr;
  if (!readMBR(file, mbr)) {
    out->appendPlainText("No se pudo leer MBR.\n");
    file.close();
    return;
  }

  bool encontrada = false;
  bool esLogica = false;
  Partition extendida;
  std::vector<std::pair<EBR, long>> ebrsPos;
  for (const Partition& p : mbr.parts) {
    if (p.status == 1 && name == QString::fromLatin1(p.name)) {
      encontrada = true;
      break;
    }
    if (p.status == 1 && p.type == 'E') extendida = p;
  }
  if (!encontrada && extendida.status == 1) {
    ebrsPos = leerEBRsConPos(file, extendida);
    for (auto& par : ebrsPos) {
      if (par.first.status == 1 &&
          QString::fromLatin1(par.first.name) == name) {
        encontrada = true;
        esLogica = true;
        break;
      }
    }
  }
  file.close();
  if (!encontrada) {
    out->appendPlainText("No se encontró la partición.\n");
    return;
  }
  DiscoMontado* disco = nullptr;
  for (auto& d : discosMontados)
    if (d.path == finalPath) {
      disco = &d;
      break;
    }
  if (!disco) {
    char nuevaLetra = primeraLetraDisponible();
    discosMontados.push_back({finalPath, nuevaLetra, {}});
    disco = &discosMontados.back();
  }
  for (auto& p : disco->parts)
    if (p.name == name) {
      out->appendPlainText("La partición ya está montada.\n");
      return;
    }
  int numLibre = primerNumeroDisponible(*disco);
  QString id = QString("vd%1%2").arg(disco->letra).arg(numLibre);
  disco->parts.push_back({name, id});
  imprimirParticionesDisco(out, *disco);
}

void DiskManager::unmount(const QStringList& args, QPlainTextEdit* out) {
  if (args.isEmpty()) {
    out->appendPlainText("Falta parámetro id.\n");
    return;
  }
  QString id;
  for (const QString& a : args)
    if (a.toLower().startsWith("-id=")) id = a.mid(4);
  if (id.isEmpty()) {
    out->appendPlainText("Falta parámetro id.\n");
    return;
  }
  if (!id.startsWith("vd") || id.length() < 4) {
    out->appendPlainText("Formato de id inválido.\n");
    return;
  }

  char letra = id[2].toLatin1();
  bool encontradoDisco = false;
  for (int i = 0; i < (int)discosMontados.size(); ++i) {
    DiscoMontado& disco = discosMontados[i];
    if (disco.letra == letra) {
      encontradoDisco = true;
      bool encontradaParticion = false;
      for (int j = 0; j < (int)disco.parts.size(); ++j) {
        if (disco.parts[j].id == id) {
          encontradaParticion = true;
          disco.parts.erase(disco.parts.begin() + j);
          break;
        }
      }
      if (!encontradaParticion) {
        out->appendPlainText("No existe una partición con ese id.\n");
        return;
      }
      if (disco.parts.empty()) {
        discosMontados.erase(discosMontados.begin() + i);
        out->appendPlainText(
          "Particion desmontada con exito.\nNo quedan particiones montadas en "
          "el disco.\n");
        return;
      }
      out->appendPlainText("Particion desmontada con exito.\n");
      imprimirParticionesDisco(out, disco);
      return;
    }
  }
  if (!encontradoDisco)
    out->appendPlainText("No existe un disco con esa letra.\n");
}

// -------------------------- REP (visualizar) --------------------------
void DiskManager::rep(
  const QStringList& args, QPlainTextEdit* out, const QDir& currentDir) {
  QString id, path;
  for (const QString& arg : args) {
    if (arg.startsWith("-id=")) id = arg.mid(4).trimmed();
    else if (arg.startsWith("-path=")) path = arg.mid(6).trimmed();
  }
  if (id.isEmpty()) {
    out->appendPlainText("Falta el parámetro -id=");
    return;
  }
  if (path.isEmpty()) {
    out->appendPlainText("Falta el parámetro -path=");
    return;
  }

  char letra = id[2].toLatin1();
  bool encontradoDisco = false;
  QString diskFilePath;
  for (auto& disco : discosMontados) {
    if (disco.letra == letra) {
      encontradoDisco = true;
      for (auto& part : disco.parts) {
        if (part.id == id) {
          diskFilePath = disco.path;
          break;
        }
      }
      break;
    }
  }
  if (!encontradoDisco) {
    out->appendPlainText("No se ha montado el disco.\n");
    return;
  }
  if (diskFilePath.isEmpty()) {
    out->appendPlainText("No se encontró la partición montada en ese disco.\n");
    return;
  }
  std::fstream file = openDiskForRead(diskFilePath);
  if (!file.is_open()) {
    out->appendPlainText("No se pudo abrir el archivo del disco.\n");
    return;
  }
  MBR mbr;
  if (!readMBR(file, mbr)) {
    out->appendPlainText("Error leyendo MBR.\n");
    file.close();
    return;
  }
  int totalSize = mbr.size;

  long extStart = -1;
  long extEnd = -1;
  std::vector<PartitionInfo> blocks;
  blocks.push_back({"MBR", 0, static_cast<int>(sizeof(MBR)), "MBR"});
  std::vector<Partition> activeParts;
  for (const auto& p : mbr.parts)
    if (p.status == 1 && p.size > 0) activeParts.push_back(p);
  std::sort(activeParts.begin(), activeParts.end(),
    [](const Partition& a, const Partition& b) { return a.start < b.start; });
  int lastPos = sizeof(MBR);
  for (const auto& p : activeParts) {
    if (p.start > lastPos)
      blocks.push_back({"", lastPos, p.start - lastPos, "LIBRE"});
    QString typeStr = (p.type == 'E') ? "EXTENDIDA" : "PRIMARIA";
    blocks.push_back({QString::fromLatin1(p.name), p.start, p.size, typeStr});
    if (p.type == 'E') {
      extStart = p.start;
      extEnd = p.start + p.size;
    }
    lastPos = p.start + p.size;
  }
  if (lastPos < mbr.size)
    blocks.push_back({"", lastPos, mbr.size - lastPos, "LIBRE"});

  if (extStart != -1) {
    auto logicalsWithPos =
      leerEBRsConPos(file, Partition{1, 'E', 0, static_cast<int>(extStart),
                             static_cast<int>(extEnd - extStart), {0}});
    // Convertir EBRs
    std::vector<EBR> logicals;
    for (auto& p : logicalsWithPos) logicals.push_back(p.first);

    if (!logicals.empty()) {
      std::vector<PartitionInfo> newBlocks;
      for (const auto& b : blocks) {
        if (b.type != "EXTENDIDA") {
          newBlocks.push_back(b);
          continue;
        }
        int currentExtPos = b.start;
        for (const auto& log : logicals) {
          int ebrPos = log.start - static_cast<int>(sizeof(EBR));
          if (ebrPos > currentExtPos)
            newBlocks.push_back(
              {"", currentExtPos, ebrPos - currentExtPos, "LIBRE"});
          newBlocks.push_back(
            {"EBR", ebrPos, static_cast<int>(sizeof(EBR)), "EBR"});
          newBlocks.push_back(
            {QString::fromLatin1(log.name), log.start, log.size, "LÓGICA"});
          currentExtPos = log.start + log.size;
        }
        if (currentExtPos < (b.start + b.size)) {
          newBlocks.push_back(
            {"", currentExtPos, (b.start + b.size) - currentExtPos, "LIBRE"});
        }
      }
      blocks = std::move(newBlocks);
    }
  }
  file.close();

  // ----------------- Generar Imagen ---------------
  const int IMAGE_WIDTH = 1000;
  const int DISK_BAR_HEIGHT = 150;
  const int PADDING = 20;  // Padding del lienzo
  const int EXTENDED_HEADER_HEIGHT = 30;
  const int BLOCK_UNIT_WIDTH = 100;
  const int METADATA_UNIT_WIDTH = BLOCK_UNIT_WIDTH / 2;
  const int INNER_MARGIN = 5;
  const QColor BORDER_COLOR(142, 173, 196);  // Azul claro

  QPixmap pixmap(IMAGE_WIDTH, DISK_BAR_HEIGHT + 2 * PADDING);

  QPainter painter;
  int requiredWidth = 0;
  // Calcular el ancho total requerido
  for (const auto& b : blocks) {
    if (b.size <= 0) continue;
    if (b.type == "MBR" || b.type == "EBR")
      requiredWidth += METADATA_UNIT_WIDTH;
    else requiredWidth += BLOCK_UNIT_WIDTH;
  }
  requiredWidth += 2 * PADDING;

  pixmap = QPixmap(requiredWidth, DISK_BAR_HEIGHT + 2 * PADDING);
  pixmap.fill(QColor(Qt::white));
  painter.begin(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);

  painter.setPen(QPen(BORDER_COLOR, 1));
  QFont font = painter.font();
  font.setPointSize(8);
  painter.setFont(font);

  const int START_Y = PADDING;
  const int START_X = PADDING;
  int currentX = START_X;

  // Calcular rango de la parte extendida en pixeles
  int extendedStartPixel = -1;
  int extendedEndPixel = -1;
  bool findingExtendedRange = false;

  // Calcular el rango de la zona extendida
  for (const auto& b : blocks) {
    if (b.size <= 0) continue;
    int blockWidth;
    if (b.type == "MBR" || b.type == "EBR") blockWidth = METADATA_UNIT_WIDTH;
    else blockWidth = BLOCK_UNIT_WIDTH;

    // Si el inicio del bloque está dentro de extStart/extEnd o es el bloque
    // extendida vacío
    bool isBlockInExtendedZone =
      (b.start >= extStart && b.start < extEnd && extStart != -1);
    if (b.type == "EXTENDIDA" || isBlockInExtendedZone) {
      if (!findingExtendedRange) {
        findingExtendedRange = true;
        extendedStartPixel = currentX;
      }
      extendedEndPixel = currentX + blockWidth;
    } else if (findingExtendedRange) {
      findingExtendedRange = false;
    }
    currentX += blockWidth;
  }

  // Marco exterior del disco
  currentX = START_X;
  painter.drawRect(START_X, START_Y, requiredWidth - 2 * PADDING + INNER_MARGIN,
    DISK_BAR_HEIGHT + INNER_MARGIN);

  // Bloques
  for (const auto& b : blocks) {
    if (b.size <= 0) continue;
    int blockWidth;
    if (b.type == "MBR" || b.type == "EBR") blockWidth = METADATA_UNIT_WIDTH;
    else blockWidth = BLOCK_UNIT_WIDTH;
    double percentage = (totalSize > 0) ? (double)b.size / totalSize : 0.0;
    bool isInternalBlock =
      (extStart != -1 && b.start >= extStart && (b.start + b.size) <= extEnd);

    int drawX = currentX + INNER_MARGIN;
    int drawY = START_Y + INNER_MARGIN;
    int drawWidth = blockWidth - INNER_MARGIN;
    int drawHeight = DISK_BAR_HEIGHT - INNER_MARGIN;

    // Ajuste para bloques internos de la Extendida
    if (isInternalBlock && b.type != "EXTENDIDA") {
      drawY = START_Y + EXTENDED_HEADER_HEIGHT + INNER_MARGIN;
      drawHeight = DISK_BAR_HEIGHT - EXTENDED_HEADER_HEIGHT - INNER_MARGIN;
    }
    // Nota: el bloque EXTENDIDA (vacío) usa la altura completa del disco menos
    // el margen.
    else if (b.type == "EXTENDIDA") {
      drawY = START_Y + INNER_MARGIN;
      drawHeight = DISK_BAR_HEIGHT - INNER_MARGIN;
    }
    // Dibujar Rectángulo
    painter.setPen(QPen(BORDER_COLOR, 1));  // Color del borde definido
    painter.setBrush(Qt::white);
    painter.drawRect(drawX, drawY, drawWidth, drawHeight);
    QString typeText = b.type;
    QString infoText;
    if (b.type == "MBR" || b.type == "EBR") infoText = typeText;
    else infoText = QString::asprintf("%.1f%%", percentage * 100);
    painter.setPen(QPen(Qt::black));
    // Tipo (arriba)
    painter.drawText(drawX, drawY + drawHeight / 3, drawWidth, drawHeight / 4,
      Qt::AlignCenter, typeText);
    // Porcentaje (abajo, si no es MBR o EBR)
    if (b.type != "MBR" && b.type != "EBR") {
      painter.drawText(drawX, drawY + drawHeight * 2 / 3, drawWidth,
        drawHeight / 4, Qt::AlignCenter, infoText);
    }
    currentX += blockWidth;
  }

  // Dibujar encabezado si hay partición extendida
  if (extStart != -1 && extendedStartPixel != -1) {
    int headerWidth = extendedEndPixel - extendedStartPixel;
    if (headerWidth > 0) {
      painter.setPen(QPen(BORDER_COLOR, 1));
      painter.setBrush(Qt::white);
      // Ajuste de margen para el encabezado
      int headerDrawX = extendedStartPixel + INNER_MARGIN;
      int headerDrawY = START_Y + INNER_MARGIN;
      int headerDrawWidth = headerWidth - INNER_MARGIN;
      int headerDrawHeight = EXTENDED_HEADER_HEIGHT - INNER_MARGIN;
      // Dibujar rectángulo y texto del encabezado
      painter.drawRect(
        headerDrawX, headerDrawY, headerDrawWidth, headerDrawHeight);
      painter.setPen(QPen(Qt::black));  // Texto NEGRO
      painter.drawText(headerDrawX, headerDrawY, headerDrawWidth,
        headerDrawHeight, Qt::AlignCenter, "EXTENDIDA");
    }
  }
  painter.end();

  // Guardar la imagen
  QString finalPath = path;
  QFileInfo fi(path);
  if (!fi.isAbsolute()) finalPath = currentDir.absoluteFilePath(path);
  if (pixmap.save(finalPath))
    out->appendPlainText("Reporte gráfico generado exitosamente.\n");
  else out->appendPlainText("Error al intentar guardar el reporte.\n");
}

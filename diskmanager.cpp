#include "diskmanager.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <climits>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

#include "terminal.h"

using namespace std;

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
  int next;  // siguiente EBR
  char name[16];
};

struct Hueco {
  int inicio;
  int tam;
};

// -------------------------------------------------------
// ----------------------- MKDISK ------------------------
void DiskManager::mkdisk(
  const QStringList& args, QPlainTextEdit* out, const QDir& currentDir) {
  long sizeBytes = 0;
  char fit = 'F';      // Primer ajuste por defecto
  QString unit = "m";  // Megabytes por defecto
  QString rawPath;

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
      out->appendPlainText("No se pudieron crear las carpetas.\n");
      return;
    }
  }

  QString raidPath;
  int pos = finalPath.lastIndexOf(".disk");
  raidPath = finalPath.left(pos) + "_raid.disk";

  // Crear discos
  if (!createEmptyDisk(finalPath, sizeBytes, out)) return;
  if (!createEmptyDisk(raidPath, sizeBytes, out)) return;
  if (!writeInitialMBR(finalPath, sizeBytes, fit, out)) return;
  if (!writeInitialMBR(raidPath, sizeBytes, fit, out)) return;
  out->appendPlainText("Disco creado con éxito.\n");
}

bool DiskManager::createEmptyDisk(
  const QString& path, long sizeBytes, QPlainTextEdit* out) {
  fstream file(path.toStdString(), ios::out | ios::binary);
  if (!file.is_open()) {
    out->appendPlainText("No se pudo crear el archivo.\n");
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

bool DiskManager::writeInitialMBR(
  const QString& path, long sizeBytes, char fit, QPlainTextEdit* out) {
  fstream file(path.toStdString(), ios::in | ios::out | ios::binary);
  if (!file.is_open()) {
    out->appendPlainText("No se pudo abrir el archivo para escribir MBR.\n");
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

// -------------------------------------------------------
// ----------------------- RMDISK ------------------------
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
  // Resolver path relativo según currentDir
  QDir base = currentDir;
  QString finalPath = base.absoluteFilePath(rawPath);
  if (!finalPath.endsWith(".disk")) {
    out->appendPlainText("Extensión de disco inválida.\n");
    return;
  }

  QFile* file = new QFile(
    finalPath);  // Debe ser puntero para poder usarse en la función lambda
  if (!file->exists()) {
    out->appendPlainText("El archivo no existe.\n");
    return;
  }

  // Confirmar si en verdad desea borrar el archivo
  terminal->esperandoConfirmacion = true;  // activar confirmación
  terminal->prompt = ">> ¿Seguro que desea eliminar el disco? Y/N: ";

  // Conectar la señal de confirmación a un lambda que maneja la respuesta
  // Nota: la función lambda se llama cuando se reciba la señal de confirmación
  // [=] captura por valor todas las variables externas (crea copias)
  // mutable permite modificar las variables capturadas por valor en el lambda
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
      file->deleteLater();  // limpiar (método de Qt)
      terminal->esperandoConfirmacion = false;
      // Desconectar lambda después de usar
      QObject::disconnect(
        terminal, &Terminal::confirmacionRecibida, nullptr, nullptr);
    });
}

// -------------------------------------------------------
// ----------------------- FDISK -------------------------
void DiskManager::fdisk(const QStringList& args, QPlainTextEdit* out,
  const QDir& currentDir, Terminal* terminal) {
  long sizeBytes = 0;
  char unit = 'k';  // default
  char type = 'P';  // default
  char fit = 'W';   // default: Worst Fit
  long addValue = 0;
  QString deleteMode;
  QString name;
  QString rawPath;

  // Comprobar si se pasaron todos los argumentos obligatorios. Nota: los
  // parámetros se pasan por referencia, la función cambia los valores por
  // defecto si se le pasaron.
  if (!fdiskParams(args, sizeBytes, unit, type, rawPath, name, deleteMode,
        addValue, fit, out))
    return;

  // Resolver path relativo según currentDir
  QDir base = currentDir;
  QString finalPath = base.absoluteFilePath(rawPath);

  // TODO: Buscar otra forma para no repetir la lógica de fdiskParams aquí
  if (!deleteMode.isEmpty()) {
    // Borrar partición
    if (!deleteParticion(finalPath, name, out, terminal))
      out->appendPlainText("Error al eliminar la partición " + name + ".\n");
    return;
  }

  if (addValue != 0) {
    // Agregar/quitar espacio
    if (addAParticion(finalPath, name, sizeBytes, out))
      out->appendPlainText("Espacio modificado para " + name + ".\n");
    else
      out->appendPlainText("Error al modificar espacio para " + name + ".\n");
    return;
  }

  // Crear partición (primaria/extendida/lógica)
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

bool haySlotDisponible(const MBR& mbr) {
  for (const auto& p : mbr.parts)
    if (p.status == 0) return true;
  return false;
}

std::vector<Partition> obtenerParticionesUsadasOrdenadas(const MBR& mbr) {
  std::vector<Partition> usadas;
  for (int i = 0; i < 4; i++)
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
  } else if (fit == 'W') {  // Worst Fit
    elegido = *std::max_element(huecos.begin(), huecos.end(),
      [](const Hueco& a, const Hueco& b) { return a.tam < b.tam; });
  }
  return elegido;
}

bool revisarNombreUnicoYExtendida(
  const MBR& mbr, const QString& name, char type, QPlainTextEdit* out) {
  for (const auto& p : mbr.parts) {
    if (type == 'E' && p.status == 1 && p.type == 'E') {
      out->appendPlainText("Ya existe una partición extendida en el disco.");
      return false;
    }
    if (p.status == 1 && name == p.name) {
      out->appendPlainText("Ya existe una partición con ese nombre.");
      return false;
    }
  }
  return true;
}

bool insertarParticionEnMBR(MBR& mbr, const QString& name, char type, char fit,
  long sizeBytes, int inicio) {
  int slot = -1;
  for (int i = 0; i < 4; i++) {
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
  p.size = sizeBytes;
  std::memset(p.name, 0, sizeof(p.name));
  strncpy(p.name, name.toStdString().c_str(), sizeof(p.name) - 1);
  return true;
}

bool mostrarEspacioDisponible(
  const std::vector<Hueco>& huecos, long sizeBytes, QPlainTextEdit* out) {
  int maxHueco = 0;
  for (const auto& h : huecos)
    if (h.tam > maxHueco) maxHueco = h.tam;

  out->appendPlainText(
    "Espacio disponible: " + QString::number(maxHueco) + " Bytes");
  out->appendPlainText(
    "Espacio necesario : " + QString::number(sizeBytes) + " Bytes");

  if (maxHueco < sizeBytes) {
    out->appendPlainText("...\nNo hay espacio suficiente.");
    return false;
  }
  return true;
}

bool crearParticionGenerica(const QString& path, const QString& name, char type,
  long sizeBytes, char fit, QPlainTextEdit* out, bool silencioso = false) {
  std::fstream file(
    path.toStdString(), std::ios::in | std::ios::out | std::ios::binary);
  if (!file.is_open()) {
    if (!silencioso) out->appendPlainText("No se pudo abrir el disco.");
    return false;
  }
  MBR mbr;
  file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
  // Validar que haya slots antes de revisar nombre, espacio, etc.
  if (!haySlotDisponible(mbr)) {
    if (!silencioso)
      out->appendPlainText("No hay slots de partición disponibles.");
    return false;
  }
  if (!revisarNombreUnicoYExtendida(mbr, name, type, out)) return false;

  // Obtener particiones usadas y sus respectivos huecos
  std::vector<Partition> usadas = obtenerParticionesUsadasOrdenadas(mbr);
  std::vector<Hueco> huecos = calcularHuecos(usadas, mbr.size);

  // Mostrar espacio disponible solo si si hay slots
  // Nota: solo mostrar para disco normal, no para el Raid
  if (!silencioso)
    if (!mostrarEspacioDisponible(huecos, sizeBytes, out)) return false;

  // Elegir hueco según fit
  Hueco elegido = elegirHueco(huecos, sizeBytes, fit);
  if (elegido.inicio == -1) {
    if (!silencioso)
      out->appendPlainText(
        "...\nNo se encontró un hueco adecuado según el fit.");
    return false;
  }
  // Insertar en MBR
  if (!insertarParticionEnMBR(
        mbr, name, type, fit, sizeBytes, elegido.inicio)) {
    if (!silencioso)
      out->appendPlainText("...\nNo hay slots de partición disponibles.");
    return false;
  }

  // Si se creó una partición extendida, crear el EBR inicial (inactivo)
  if (type == 'E') {
    EBR ebr;
    memset(&ebr, '\0', sizeof(EBR));
    ebr.status = 0;
    ebr.fit = fit;
    ebr.start = elegido.inicio;  // inicio donde empieza la extendida
    ebr.size = 0;
    ebr.next = -1;
    file.seekp(elegido.inicio);
    file.write(reinterpret_cast<char*>(&ebr), sizeof(EBR));
  }
  file.seekp(0);
  file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
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

// -------------------- Crear Partición Lógica -------------------------
bool obtenerExtendida(const MBR& mbr, Partition& extendida) {
  for (const auto& p : mbr.parts) {
    if (p.status == 1 && p.type == 'E') {
      extendida = p;
      return true;
    }
  }
  return false;
}

static bool posValidaEnExtendida(long pos, const Partition& ext) {
  return pos >= ext.start &&
         (pos + (long)sizeof(EBR)) <= (ext.start + ext.size);
}

static bool leerEBREnPos(std::fstream& file, long pos, EBR& out) {
  file.seekg(pos);
  file.read(reinterpret_cast<char*>(&out), sizeof(EBR));
  return static_cast<bool>(file);
}

static bool escribirEBREnPos(std::fstream& file, long pos, const EBR& ebr) {
  file.seekp(pos);
  file.write(reinterpret_cast<const char*>(&ebr), sizeof(EBR));
  return static_cast<bool>(file);
}

std::vector<std::pair<EBR, long>> leerEBRsConPos(
  std::fstream& file, const Partition& extendida) {
  std::vector<std::pair<EBR, long>> lista;
  long inicioExt = extendida.start;
  long finExt = extendida.start + extendida.size;
  long pos = inicioExt;

  // Límite de iteraciones para evitar bucles en disco corrupto
  size_t maxIter = static_cast<size_t>(extendida.size / sizeof(EBR)) + 10;
  size_t iter = 0;

  while (pos + static_cast<long>(sizeof(EBR)) <= finExt && iter < maxIter) {
    EBR ebr{};
    if (!leerEBREnPos(file, pos, ebr)) break;
    if (ebr.status == 1) lista.push_back({ebr, pos});

    // Calcular avance: sizeof(EBR) + tamaño de la partición (si tiene sentido)
    long avance = sizeof(EBR);
    if (ebr.size > 0) {
      if (ebr.size > (finExt - pos - static_cast<long>(sizeof(EBR)))) break;
      avance += ebr.size;
    } else {
      // ebr.size == 0 -> solo avanzar sizeof(EBR) para saltar slot
    }
    if (avance <= 0) break;
    pos += avance;
    iter++;
  }
  return lista;
}

bool nombreLogicaDisponible(
  const std::vector<std::pair<EBR, long>>& ebrsPos, const QString& name) {
  for (const auto& p : ebrsPos) {
    const EBR& e = p.first;
    if (e.status == 1 && name == QString(e.name)) return false;
  }
  return true;
}

std::vector<Hueco> calcularHuecosEnExtendida(
  const Partition& ext, const std::vector<std::pair<EBR, long>>& ebrsPos) {
  std::vector<Hueco> huecos;
  long inicioExt = ext.start;
  long finExt = ext.start + ext.size;

  if (ebrsPos.empty()) {
    huecos.push_back({static_cast<int>(inicioExt), static_cast<int>(ext.size)});
    return huecos;
  }
  // Ordenar por la posición física del EBR (no por ebr.start)
  std::vector<std::pair<EBR, long>> sorted = ebrsPos;
  std::sort(sorted.begin(), sorted.end(),
    [](const std::pair<EBR, long>& a, const std::pair<EBR, long>& b) {
      return a.second < b.second;
    });
  // Hueco antes del primer EBR (desde inicioExt hasta posPrimerEBR)
  long posPrim =
    sorted[0].second;  // pos del primer EBR, donde está el EBR físico
  if (posPrim > inicioExt) {
    huecos.push_back(
      {static_cast<int>(inicioExt), static_cast<int>(posPrim - inicioExt)});
  }

  for (int i = 0; i < sorted.size() - 1; ++i) {
    long posEBRActual = sorted[i].second;
    long tamActual = sorted[i].first.size;
    long finActual = posEBRActual + static_cast<long>(sizeof(EBR)) + tamActual;
    long posSiguiente = sorted[i + 1].second;
    if (posSiguiente > finActual) {
      huecos.push_back({static_cast<int>(finActual),
        static_cast<int>(posSiguiente - finActual)});
    }
  }
  // Hueco después del último EBR
  long posLastEBR = sorted.back().second;
  long finLast =
    posLastEBR + static_cast<long>(sizeof(EBR)) + sorted.back().first.size;
  if (finExt > finLast) {
    huecos.push_back(
      {static_cast<int>(finLast), static_cast<int>(finExt - finLast)});
  }
  return huecos;
}

bool escribirNuevoEBR(std::fstream& file, const Partition& extendida,
  const std::vector<std::pair<EBR, long>>& ebrsPos, long posEBR, long sizeBytes,
  char fit, const QString& name) {
  long inicioExt = extendida.start;
  long finExt = extendida.start + extendida.size;
  // Validar límites
  if (posEBR < inicioExt) return false;
  if (posEBR + static_cast<long>(sizeof(EBR)) + sizeBytes > finExt)
    return false;

  // Crear EBR a escribir
  EBR nuevo{};
  memset(&nuevo, 0, sizeof(EBR));
  nuevo.status = 1;
  nuevo.fit = fit;
  nuevo.start = posEBR + static_cast<long>(sizeof(EBR));  // inicio de datos
  nuevo.size = static_cast<int>(sizeBytes);
  strncpy(nuevo.name, name.toStdString().c_str(), sizeof(nuevo.name) - 1);
  nuevo.next = -1;

  // Ordenar EBRs existentes por pos
  std::vector<std::pair<EBR, long>> sorted = ebrsPos;
  std::sort(sorted.begin(), sorted.end(),
    [](const std::pair<EBR, long>& a, const std::pair<EBR, long>& b) {
      return a.second < b.second;
    });

  // Encontrar prev y next por posición física del EBR
  long prevPos = -1;
  long nextPos = -1;
  for (int i = 0; i < sorted.size(); ++i) {
    long p = sorted[i].second;
    if (p < posEBR) prevPos = p;
    if (p > posEBR && nextPos == -1) nextPos = p;
  }

  // Actualizar next del nuevo (apuntar a pos del siguiente EBR físico si
  // existe)
  if (nextPos != -1) {
    nuevo.next = static_cast<int>(nextPos);
  } else {
    nuevo.next = -1;
  }
  // Si existe prev, actualizar su next para que apunte a posEBR
  if (prevPos != -1) {
    EBR prevEBR;
    if (!leerEBREnPos(file, prevPos, prevEBR)) return false;
    prevEBR.next = static_cast<int>(posEBR);
    if (!escribirEBREnPos(file, prevPos, prevEBR)) return false;
  }
  // Si no hay prev, no hay EBR anterior que actualizar
  // Escribir el nuevo EBR
  if (!escribirEBREnPos(file, posEBR, nuevo)) return false;
  return true;
}

bool crearEBRInterno(const QString& path, long posEBR, long sizeBytes, char fit,
  const QString& name, QPlainTextEdit* out) {
  std::fstream file(
    path.toStdString(), std::ios::in | std::ios::out | std::ios::binary);
  if (!file.is_open()) {
    out->appendPlainText("No se pudo abrir el disco RAID.");
    return false;
  }

  MBR mbr;
  file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
  Partition extendida;
  if (!obtenerExtendida(mbr, extendida)) {
    out->appendPlainText("El disco RAID no tiene extendida.");
    file.close();
    return false;
  }

  auto ebrsPos = leerEBRsConPos(file, extendida);
  bool ok =
    escribirNuevoEBR(file, extendida, ebrsPos, posEBR, sizeBytes, fit, name);
  file.close();
  if (!ok) out->appendPlainText("Error al escribir EBR en RAID.");
  return ok;
}

bool DiskManager::crearLogica(const QString& path, const QString& name,
  long sizeBytes, char fitUser, QPlainTextEdit* out) {
  // Abrir disco principal
  std::fstream file(
    path.toStdString(), std::ios::in | std::ios::out | std::ios::binary);
  if (!file.is_open()) {
    out->appendPlainText("No se pudo abrir el disco.");
    return false;
  }

  MBR mbr;
  file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
  Partition extendida;
  if (!obtenerExtendida(mbr, extendida)) {
    out->appendPlainText("No existe una partición extendida.");
    file.close();
    return false;
  }

  // Leer EBRs activos del disco principal (con sus posiciones)
  auto ebrsPos = leerEBRsConPos(file, extendida);
  // Validar nombre
  if (!nombreLogicaDisponible(ebrsPos, name)) {
    out->appendPlainText("Ya existe una partición lógica con ese nombre.");
    file.close();
    return false;
  }
  std::vector<Hueco> huecos = calcularHuecosEnExtendida(extendida, ebrsPos);
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

  // Elegir hueco usando fit de la extendida
  Hueco elegido = elegirHueco(
    huecos, sizeBytes + static_cast<int>(sizeof(EBR)), extendida.fit);
  if (elegido.inicio == -1) {
    out->appendPlainText(
      "No se encontró un hueco adecuado dentro de la extendida.");
    file.close();
    return false;
  }
  long posEBR = elegido.inicio;
  // Escribir en disco principal
  if (!escribirNuevoEBR(
        file, extendida, ebrsPos, posEBR, sizeBytes, extendida.fit, name)) {
    out->appendPlainText("Error al escribir EBR en disco principal.");
    file.close();
    return false;
  }

  // Insertar en disco RAID (misma posición)
  QString raidPath;
  int p = path.lastIndexOf(".disk");
  raidPath = path.left(p) + "_raid.disk";
  if (!crearEBRInterno(raidPath, posEBR, sizeBytes, extendida.fit, name, out)) {
    out->appendPlainText("EBR creado en principal pero fallo en RAID.");
  }
  file.close();
  return true;
}
// -----------------------------------------------------------------

// -----------------------------------------------------------------
static bool deleteParticionInterno(const QString& path, const QString& name) {
  std::fstream file(
    path.toStdString(), std::ios::in | std::ios::out | std::ios::binary);
  if (!file.is_open()) return false;

  MBR mbr;
  file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
  bool encontrada = false;

  // Particiones primarias/extendida
  for (auto& p : mbr.parts) {
    if (p.status == 1 && name == QString(p.name)) {
      p.status = 0;
      encontrada = true;
      break;
    }
  }
  // Revisar lógicas
  if (!encontrada) {
    Partition extendida;
    if (obtenerExtendida(mbr, extendida)) {
      auto ebrsPos = leerEBRsConPos(file, extendida);
      for (const auto& [ebr, pos] : ebrsPos) {
        if (ebr.status == 1 && name == QString(ebr.name)) {
          EBR mod = ebr;
          mod.status = 0;
          escribirEBREnPos(file, pos, mod);
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
  file.seekp(0);
  file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
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
                escribirEBREnPos(*file, pos, mod);
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
              exito = escribirEBREnPos(*file, pos, mod);
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
// -----------------------------------------------------------------

// -----------------------------------------------------------------
bool DiskManager::addAParticion(const QString& path, const QString& name,
  long addBytes, QPlainTextEdit* out) {
  std::fstream file(
    path.toStdString(), std::ios::in | std::ios::out | std::ios::binary);
  if (!file.is_open()) {
    out->appendPlainText("No se pudo abrir el disco.\n");
    return false;
  }
  // Leer MBR
  MBR mbr;
  file.seekg(0);
  file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
  // Buscar partición
  Partition* objetivo = nullptr;
  for (auto& p : mbr.parts) {
    if (p.status == 1 && name == QString(p.name)) {
      if (p.type != 'P' && p.type != 'E') {
        out->appendPlainText(
          "Solo se puede modificar primarias o extendida.\n");
        return false;
      }
      objetivo = &p;
      break;
    }
  }
  if (!objetivo) {
    out->appendPlainText("No se encontró la partición.\n");
    return false;
  }
  long nuevo = objetivo->size + addBytes;
  // Validar tamaño final
  if (nuevo <= 0) {
    out->appendPlainText("El tamaño resultante sería inválido.\n");
    return false;
  }
  // Obtener huecos actuales
  auto usadas = obtenerParticionesUsadasOrdenadas(mbr);
  auto huecos = calcularHuecos(usadas, mbr.size);
  // Buscar hueco que esté justo después
  long finActual = objetivo->start + objetivo->size;
  long espacioDisponible = -1;
  for (auto& h : huecos) {
    if (h.inicio == finActual) {
      espacioDisponible = h.tam;
      break;
    }
  }
  if (addBytes > 0) {
    // Expansión, requiere hueco suficiente
    if (espacioDisponible < addBytes) {
      out->appendPlainText("No hay espacio suficiente para expandir.\n");
      return false;
    }
  }
  objetivo->size = nuevo;

  // Guardar MBR
  file.seekp(0);
  file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
  file.flush();
  file.close();
  out->appendPlainText("Partición modificada correctamente.\n");
  return true;
}
// -----------------------------------------------------------------

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
    }
    else if (low.startsWith("-unit=")) {
      QChar u = low.mid(6)[0];
      if (u == 'k' || u == 'm' || u == 'b') unit = u.toLatin1();
      else {
        out->appendPlainText("Unidad inválida, use K, M o B).\n");
        return false;
      }
    }
    else if (low.startsWith("-path=")) {
      path = a.mid(6);
      pathFound = true;
    }
    else if (low.startsWith("-name=")) {
      name = a.mid(6);
      nameFound = true;
    }
    else if (low.startsWith("-type=")) {
      QChar t = low.mid(6)[0];
      if (t == 'p' || t == 'e' || t == 'l') type = t.toUpper().toLatin1();
      else {
        out->appendPlainText("Tipo inválido, use P, E o L).\n");
        return false;
      }
    }
    else if (low.startsWith("-fit=")) {
      QString f = low.mid(5).toUpper();
      if (f == "BF") fit = 'B';
      else if (f == "FF") fit = 'F';
      else if (f == "WF") fit = 'W';
      else {
        out->appendPlainText("Fit inválido (BF, FF o WF).\n");
        return false;
      }
    }
    else if (low.startsWith("-delete=")) {
      deleteMode = low.mid(8);  // fast/full
      deleteFound = true;
    }
    else if (low.startsWith("-add=")) {
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

// -------------------------------------------------------
// ------------------ MOUNT Y UNMOUNT ---------------------
struct PartMontada {
  QString name;
  QString id;  // (vda1, vdb2, ...)
};
struct DiscoMontado {
  QString path;
  char letra;
  std::vector<PartMontada> parts;
};
std::vector<DiscoMontado> discosMontados;

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
    QString numStr = p.id.mid(3);  // después de vdX
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
  // Resolver ruta relativa
  QDir base = currentDir;
  QString finalPath = base.absoluteFilePath(rawPath);
  if (!finalPath.endsWith(".disk")) {
    out->appendPlainText("Extensión de disco inválida.\n");
    return;
  }
  std::fstream file(finalPath.toStdString(), std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    out->appendPlainText("No se pudo abrir el disco.\n");
    return;
  }
  MBR mbr;
  file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
  bool encontrada = false;
  bool esLogica = false;
  Partition extendida;
  std::vector<std::pair<EBR, long>> ebrsPos;
  // Buscar primaria y detectar extendida
  for (const Partition& p : mbr.parts) {
    if (p.status == 1 && name == QString(p.name)) {
      encontrada = true;
      break;
    }
    if (p.status == 1 && p.type == 'E') {
      extendida = p;
    }
  }
  // Buscar lógica
  if (!encontrada && extendida.status == 1) {
    ebrsPos = leerEBRsConPos(file, extendida);
    for (auto& par : ebrsPos) {
      if (par.first.status == 1 && QString(par.first.name) == name) {
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
  // Buscar disco ya montado
  DiscoMontado* disco = nullptr;
  for (auto& d : discosMontados) {
    if (d.path == finalPath) {
      disco = &d;
      break;
    }
  }

  // Si no existe, asignar letra usando tu función auxiliar
  if (!disco) {
    char nuevaLetra = primeraLetraDisponible();
    discosMontados.push_back({finalPath, nuevaLetra, {}});
    disco = &discosMontados.back();
  }
  // Revisar si esa partición ya está montada
  for (auto& p : disco->parts) {
    if (p.name == name) {
      out->appendPlainText("La partición ya está montada.\n");
      return;
    }
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
  for (const QString& a : args) {
    if (a.toLower().startsWith("-id=")) id = a.mid(4);
  }
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
  // Recorrer discos montados
  for (int i = 0; i < discosMontados.size(); i++) {
    DiscoMontado& disco = discosMontados[i];
    if (disco.letra == letra) {
      encontradoDisco = true;
      bool encontradaParticion = false;
      for (int j = 0; j < disco.parts.size(); j++) {
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

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
#include <QFont>
#include <QImage>
#include <QPainter>

struct PartitionInfo {
  QString name;
  int start;
  int size;
  QString type;  // "MBR", "PRIMARY", "EXTENDED", "LOGICAL", "FREE"
};

void DiskManager::rep(
  const QStringList& args, QPlainTextEdit* out, const QDir& currentDir) {
  QString id;
  for (const QString& arg : args) {
    if (arg.startsWith("-id=")) id = arg.mid(4).trimmed();
  }

  if (id.isEmpty()) {
    out->appendPlainText("Falta el parámetro -id=");
    return;
  }

  // Buscar disco montado
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
    out->appendPlainText("No se ha montado el disco.");
    return;
  }

  // Abrir disco
  std::fstream file(
    diskFilePath.toStdString(), std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    out->appendPlainText("No se pudo abrir el archivo del disco.");
    return;
  }

  // Leer MBR
  MBR mbr;
  file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
  int totalSize = mbr.size;

  // Variables para recordar dónde está la extendida para efectos visuales
  long extStart = -1;
  long extEnd = -1;

  std::vector<PartitionInfo> blocks;
  blocks.push_back({"MBR", 0, (int)sizeof(MBR), "MBR"});

  // 1. Filtrar particiones activas del MBR
  std::vector<Partition> activeParts;
  for (auto& p : mbr.parts) {
    if (p.status == 1 && p.size > 0) activeParts.push_back(p);
  }

  // Ordenar primarias
  std::sort(activeParts.begin(), activeParts.end(),
    [](const Partition& a, const Partition& b) { return a.start < b.start; });

  int lastPos = sizeof(MBR);

  // 2. Procesar MBR (Primarias y Extendida 'macro')
  for (auto& p : activeParts) {
    // Hueco antes de la partición
    if (p.start > lastPos)
      blocks.push_back({"", lastPos, p.start - lastPos, "FREE"});

    QString typeStr = (p.type == 'E') ? "EXTENDED" : "PRIMARY";
    blocks.push_back({QString::fromLatin1(p.name), p.start, p.size, typeStr});

    if (p.type == 'E') {
      extStart = p.start;
      extEnd = p.start + p.size;
    }
    lastPos = p.start + p.size;
  }

  // Hueco al final del disco
  if (lastPos < mbr.size)
    blocks.push_back({"", lastPos, mbr.size - lastPos, "FREE"});

  // 3. Si hay extendida, analizar su contenido interno (EBRs)
  if (extStart != -1) {
    std::vector<EBR> logicals;
    long pos = extStart;

    // Leer todos los EBRs activos
    while (pos != -1 && pos < extEnd) {
      EBR e;
      file.seekg(pos);
      file.read(reinterpret_cast<char*>(&e), sizeof(EBR));
      if (e.status == 1) {  // Solo si está activa
        logicals.push_back(e);
      }
      if (e.next == -1 || e.next <= pos) break;
      pos = e.next;
    }

    // Si hay lógicas, "explotamos" el bloque EXTENDED en sub-bloques
    if (!logicals.empty()) {
      std::vector<PartitionInfo> newBlocks;

      for (auto& b : blocks) {
        // Si no es la extendida, pasa directo
        if (b.type != "EXTENDED") {
          newBlocks.push_back(b);
          continue;
        }

        // Estamos en la extendida, vamos a llenarla por dentro
        int currentExtPos = b.start;

        for (auto& log : logicals) {
          // OJO AQUI: El struct EBR está ANTES de los datos (log.start)
          int ebrPos = log.start - sizeof(EBR);

          // 1. Espacio libre antes del EBR (dentro de la extendida)
          if (ebrPos > currentExtPos) {
            newBlocks.push_back(
              {"", currentExtPos, ebrPos - currentExtPos, "FREE"});
          }

          // 2. El EBR propiamente dicho
          newBlocks.push_back({"EBR", ebrPos, (int)sizeof(EBR), "EBR"});

          // 3. La partición lógica (Datos)
          newBlocks.push_back(
            {QString::fromLatin1(log.name), log.start, log.size, "LOGICAL"});

          currentExtPos = log.start + log.size;
        }

        // 4. Espacio libre al final de la extendida
        if (currentExtPos < (b.start + b.size)) {
          newBlocks.push_back(
            {"", currentExtPos, (b.start + b.size) - currentExtPos, "FREE"});
        }
      }
      blocks = newBlocks;
    }
  }

  file.close();

  // 4. Imprimir con formato jerárquico
  out->appendPlainText("\n=== REPORTE DE DISCO ===");
  // Imprimimos el encabezado de la extendida si existe para dar contexto visual
  if (extStart != -1) {
    out->appendPlainText(QString("Partición Extendida: Inicio %1 - Fin %2")
        .arg(extStart)
        .arg(extEnd));
  }

  out->appendPlainText(QString("%1 %2 %3 %4")
      .arg("TIPO", -15)
      .arg("NOMBRE", -15)
      .arg("START", -10)
      .arg("SIZE", -10));

  for (auto& b : blocks) {
    bool isInsideExtended =
      (b.start >= extStart && (b.start + b.size) <= extEnd && extStart != -1);

    // Si es el contenedor global extendida (caso vacía), no lo indentamos
    if (b.type == "EXTENDED") isInsideExtended = false;

    QString prefix = isInsideExtended ? "  |__ " : "";
    QString displayType = b.type;

    // Pequeño detalle visual: diferenciar FREE interno de externo
    if (b.type == "FREE" && isInsideExtended) displayType = "FREE (Int)";

    out->appendPlainText(QString("%1%2 %3 %4 %5")
        .arg(prefix)
        .arg(displayType,
          -15 + (isInsideExtended ? -4 : 0))  // Ajuste padding si tiene prefijo
        .arg(b.name.isEmpty() ? "---" : b.name, -15)
        .arg(b.start, -10)
        .arg(b.size, -10));
  }
}

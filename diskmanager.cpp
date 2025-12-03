#include "diskmanager.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <fstream>

#include "terminal.h"

using namespace std;

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
void DiskManager::fdisk(
  const QStringList& args, QPlainTextEdit* out, const QDir& currentDir) {
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
    if (deleteParticion())
      out->appendPlainText("Partición " + name + " eliminada con éxito.\n");
    else out->appendPlainText("Error al eliminar la partición " + name + ".\n");
    return;
  }

  if (addValue != 0) {
    // Agregar/quitar espacio
    if (addAParticion())
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
      if (creaLogica())
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
  const std::vector<Partition>& usadas, int mbrSize) {
  std::vector<Hueco> huecos;
  int cursor = sizeof(MBR);
  for (const auto& p : usadas) {
    if (cursor < p.start) huecos.push_back({cursor, p.start - cursor});
    cursor = p.start + p.size;
  }
  if (cursor < mbrSize) huecos.push_back({cursor, mbrSize - cursor});
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

// ------------------ Mostrar espacio -------------------
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
  long sizeBytes, char fit, QPlainTextEdit* out) {
  std::fstream file(
    path.toStdString(), std::ios::in | std::ios::out | std::ios::binary);
  if (!file.is_open()) {
    out->appendPlainText("No se pudo abrir el disco.");
    return false;
  }
  MBR mbr;
  file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
  // Validar que haya slots antes de revisar nombre, espacio, etc.
  if (!haySlotDisponible(mbr)) {
    out->appendPlainText("No hay slots de partición disponibles.");
    return false;
  }
  if (!revisarNombreUnicoYExtendida(mbr, name, type, out)) return false;

  // Obtener particiones usadas y sus respectivos huecos
  std::vector<Partition> usadas = obtenerParticionesUsadasOrdenadas(mbr);
  std::vector<Hueco> huecos = calcularHuecos(usadas, mbr.size);

  // Mostrar espacio disponible solo si SÍ hay slots
  if (!mostrarEspacioDisponible(huecos, sizeBytes, out)) return false;

  // Elegir hueco según fit
  Hueco elegido = elegirHueco(huecos, sizeBytes, fit);
  if (elegido.inicio == -1) {
    out->appendPlainText("...\nNo se encontró un hueco adecuado según el fit.");
    return false;
  }
  // Insertar en MBR
  if (!insertarParticionEnMBR(
        mbr, name, type, fit, sizeBytes, elegido.inicio)) {
    out->appendPlainText("...\nNo hay slots de partición disponibles.");
    return false;
  }
  file.seekp(0);
  file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
  file.close();
  return true;
}

bool DiskManager::crearPrimaria(const QString& path, const QString& name,
  long sizeBytes, char fit, QPlainTextEdit* out) {
  return crearParticionGenerica(path, name, 'P', sizeBytes, fit, out);
}

bool DiskManager::crearExtendida(const QString& path, const QString& name,
  long sizeBytes, char fit, QPlainTextEdit* out) {
  return crearParticionGenerica(path, name, 'E', sizeBytes, fit, out);
}

bool DiskManager::creaLogica() {
  return 1;
}

bool DiskManager::deleteParticion() {
  return 1;
}
bool DiskManager::addAParticion() {
  return 1;
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

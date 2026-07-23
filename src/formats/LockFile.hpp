#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace digitalforge::editor {
class Project;
}

namespace digitalforge::formats {

// Nombre convencional del archivo de bloqueo, junto al .dfproj.
inline constexpr const char* kLockFileName = "digitalforge.lock.json";

// Una entrada del lock: la version EXACTA de un componente que el proyecto usa
// realmente, con las huellas que permiten detectar que la definicion
// instalada cambio desde que se generó el lock. A diferencia del manifiesto
// (que declararia rangos aceptables), el lock fija lo exacto.
struct LockedComponent {
    std::string typeId;
    uint32_t definitionVersion = 0;
    std::string publicInterfaceHash;
    std::string simulationHash;
};

// Una biblioteca de la que el proyecto depende. Hoy solo existe la integrada;
// el `contentHash` es el de las huellas publicas de todos sus componentes en
// uso, que es lo unico verificable sin un paquete DFLIB en disco.
struct LockedLibrary {
    std::string libraryId;
    std::string version;
    std::string contentHash;
};

// Contenido de un archivo de bloqueo ya resuelto.
struct LockFile {
    uint32_t lockFormatVersion = 0;
    std::string digitalForgeVersion;
    std::vector<LockedLibrary> libraries;
    std::vector<LockedComponent> components;
};

// Construye el lock a partir del estado ACTUAL del proyecto: recorre todos los
// documentos, junta los tipos de componente realmente usados y calcula sus
// huellas. `digitalForgeVersion` se pasa desde la capa de aplicacion (formats
// no ve app::kAppVersion).
[[nodiscard]] LockFile buildLockFile(const editor::Project& project, const std::string& digitalForgeVersion);

[[nodiscard]] nlohmann::json serializeLockFile(const LockFile& lock);
[[nodiscard]] LockFile parseLockFile(const nlohmann::json& json);

// Escribe/lee el lock junto al proyecto. `projectDir` es la carpeta del
// .dfproj. Devuelve false si no existe un lock que leer (no es un error: un
// proyecto puede no tener lock todavia).
void writeLockFile(const editor::Project& project, const std::string& digitalForgeVersion,
                   const std::string& projectDir);
[[nodiscard]] bool readLockFile(const std::string& projectDir, LockFile& out);

// Como difiere el entorno ACTUAL del que registró un lock guardado. Es lo que
// permite "detectar actualizaciones" y "evitar cambios silenciosos" sin abrir
// el proyecto entero.
enum class LockComparisonKind : uint8_t {
    Added,      // el proyecto ahora usa un tipo que el lock no tenia
    Removed,    // el lock tenia un tipo que el proyecto ya no usa
    Changed,    // mismo typeId, pero las huellas o la version difieren
};

struct LockDifference {
    LockComparisonKind kind = LockComparisonKind::Changed;
    std::string typeId;
};

// Diferencias entre `stored` (lo que quedo grabado) y `current` (lo que el
// entorno de hoy produce). Vacio = entorno reproducido de forma identica.
[[nodiscard]] std::vector<LockDifference> compareLockFiles(const LockFile& stored, const LockFile& current);

} // namespace digitalforge::formats

#pragma once

#include <cstdint>

namespace digitalforge::formats {

// Version INDEPENDIENTE de cada formato en disco. La especificacion de
// metadata exige que no se reutilice un unico numero global: un cambio en el
// esquema del proyecto no debe forzar a subir la version del archivo de
// bloqueo ni viceversa. Cada formato sube su propio contador cuando su
// esquema cambia de forma incompatible.
//
// DFML/DFMC/DFLIB todavia no existen; sus versiones se agregaran aca cuando
// se implementen, sin tocar las de abajo.
namespace versions {

// Esquema del .dfc (documento de circuito, ver ProjectSerializer). Sigue en 1:
// los campos agregados este ciclo (pinKey, compatibility) son aditivos.
inline constexpr uint32_t kProjectSchema = 1;

// Esquema del manifiesto .dfproj (ver ProjectManifestSerializer).
inline constexpr uint32_t kManifestSchema = 1;

// Esquema del archivo de bloqueo digitalforge.lock.json (ver LockFile).
inline constexpr uint32_t kLockSchema = 1;

} // namespace versions

} // namespace digitalforge::formats

#pragma once

namespace digitalforge::core {

// Version semantica de DigitalForge (MAJOR.MINOR.PATCH), en la capa mas baja
// para que tanto la aplicacion como los formatos en disco (archivo de bloqueo,
// ver formats/LockFile) la usen sin depender uno del otro. Es un numero de
// version puro; la cadena "marketing" con sufijos como "PRE-ALPHA" vive aparte
// en la capa de aplicacion (app::kAppVersion).
inline constexpr const char* kDigitalForgeVersion = "0.1.4";

} // namespace digitalforge::core

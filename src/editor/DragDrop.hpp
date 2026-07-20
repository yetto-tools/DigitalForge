#pragma once

namespace digitalforge {

// Tipo MIME compartido para arrastrar una entrada de la paleta de componentes
// hacia el lienzo: la carga util son los bytes UTF-8 del typeId del
// componente. Se declara una sola vez aqui (en lugar de que lo posea
// cualquiera de los dos lados) ya que tanto src/ui (el origen del arrastre)
// como src/editor (el destino del soltado) necesitan coincidir en su valor.
inline constexpr const char* kComponentDragMimeType = "application/x-digitalforge-component";

} // namespace digitalforge

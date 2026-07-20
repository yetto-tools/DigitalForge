#pragma once

#include <string>

namespace digitalforge::editor {
class CircuitDocument;
}

namespace digitalforge::formats {

// Importa un archivo .circ de Logisim-evolution, reemplazando todo el
// contenido de `document` con su circuito principal (el indicado por
// <main name="...">, o el primer <circuit> si no hay <main>).
//
// Alcance v1 (deliberadamente acotado, ver src/formats/LogisimImporter.cpp
// para el detalle): un unico circuito plano (sin sub-circuitos anidados);
// solo "Wiring/Pin" y las 6 compuertas variadicas de 2 entradas de
// "Gates" (AND/OR/NAND/NOR/XOR/XNOR, tamano "Narrow"/30, orientacion este -
// la unica geometria de pines verificada empiricamente hasta ahora). La
// posicion de cada componente se preserva aproximadamente (escalada a la
// grilla de DigitalForge); el trazado exacto de cada cable de Logisim NO se
// preserva - solo su conectividad (que pin se une a que pin, o a que punto
// de union en una derivacion de 3 o mas), ya que DigitalForge enruta cada
// cable en base a donde termina realmente colocado cada componente, no a
// coordenadas absolutas heredadas de Logisim.
//
// Lanza std::runtime_error si el archivo no se puede abrir o el XML es
// invalido. Lanza std::invalid_argument, con un listado explicito de que
// tipos de componente de Logisim no tienen equivalente todavia en
// DigitalForge, si el circuito usa alguno - nunca se importa parcialmente
// descartando logica en silencio.
void importLogisimCircFile(editor::CircuitDocument& document, const std::string& path);

} // namespace digitalforge::formats

# DigitalForge - Guia de incorporacion

DigitalForge es un editor/simulador de circuitos logicos digitales, escrito en
C++20 con Qt6 Widgets. Este documento resume el estado del proyecto para que
otra sesion de Claude Code (en otra maquina) pueda retomar el trabajo sin
contexto previo.

## Arquitectura

- `src/core/` - motor de simulacion, sin dependencia de Qt. `Circuit` (topologia
  estatica: gates + nets + adyacencia derivada), `Gate` (POD compacto,
  `static_assert(sizeof(Gate) <= 16)`), `LogicValue` (enum de 5 valores: Zero,
  One, HighImpedance, Unknown, Error), `Simulator` (estado de ejecucion +
  `EventQueue` orientada a eventos con timestamp).
- `src/components/` - sistema de definicion de componentes: `ComponentDefinition`
  (typeId, `properties` como `vector<PropertyDescriptor>`, closures
  `derivePins`/`buildSimulation`), `Property.hpp` (`PropertyType`/`PropertyValue`
  como `std::variant<bool,int64_t,uint64_t,std::string>`), `ComponentRegistry`
  (registro dinamico), `ComponentInstance` (PropertyMap + pines + binding de
  simulacion). `BasicComponentLibrary.cpp` registra los tipos basicos (gates.*,
  wiring.*, io.led, io.seven_segment, debug.probe, etc.).
- `src/editor/` - modelo de documento/proyecto sobre Qt (sin QtWidgets donde es
  posible): `CircuitDocument` (un circuito editable + `QUndoStack`), `Project`
  (manifiesto multi-documento tipo Visual Studio: `.dfproj` + varios `.dfc`,
  deliberadamente sin dependencia de `CircuitScene`/QtWidgets), `ComponentItem`
  (representacion grafica en el lienzo, `QGraphicsItem`), `CircuitScene`,
  `UndoCommands.hpp` (comandos de deshacer: mover, rotar, cambiar propiedad,
  eliminar, etc.).
- `src/ui/` - paneles: `PropertyInspector` (panel de propiedades del/de los
  componente(s) seleccionado(s)), `ProjectTree` (arbol de documentos del
  proyecto), `SimulationToolbar`, `IconFactory` (iconos dibujados
  proceduralmente, adaptables a modo claro/oscuro).
- `src/app/MainWindow.*` - ventana principal, conecta todo lo anterior.
- `src/formats/` - serializacion (`ProjectSerializer` para `.dfc`,
  `ProjectManifestSerializer` para `.dfproj`), formato JSON.
- `tests/` - suite headless (Catch2, sin Qt) para `core`/`components`.
- `tests_qt/` - suite que si linkea Qt (para clases que dependen de
  QUndoStack/QGraphicsScene/etc.).
- `benchmark/` - benchmarks de rendimiento del simulador.

## Como compilar y correr

Requiere Qt 6 (Widgets) + un toolchain compatible (en la maquina original: Qt
6.7.3 mingw_64 + MinGW 13.1.0, generador "MinGW Makefiles"). En una maquina
nueva, ubicar la instalacion local de Qt (normalmente algo como
`<QtRoot>/6.x.y/mingw_64` o `.../msvc2019_64`) y el MinGW que trae Qt bajo
`<QtRoot>/Tools/mingw...` - **no asumir que las rutas de la maquina original
existen aca**.

```
# Configurar (una vez), ejemplo generico:
cmake -S . -B build-gui -G "MinGW Makefiles" -DDIGITALFORGE_BUILD_GUI=ON \
      -DCMAKE_PREFIX_PATH="<ruta a la instalacion de Qt>"

# Compilar (anteponer los bin de Qt y del MinGW de Qt al PATH primero):
cmake --build build-gui -j
```

**DLL gotcha (Windows/MinGW)**: no se ha corrido `windeployqt`, asi que
cualquier `.exe` generado (`DigitalForge.exe`, `digitalforge_qt_tests.exe`)
falla al abrir con `STATUS_DLL_NOT_FOUND` a menos que los DLL de runtime de Qt
y MinGW (y `platforms/qwindows.dll` para la app grafica) esten en el PATH del
proceso exacto que lo ejecuta, o copiados junto al `.exe`. El PATH no persiste
entre invocaciones de shell separadas en este tipo de entorno - hay que
volver a anteponerlo en cada comando, o copiar los DLL una sola vez.

`digitalforge_tests` (headless, sin Qt) no tiene este problema.

## Tests

Dos suites, ambas con Catch2:
- `build-gui/tests/digitalforge_tests.exe` - headless (core + components).
- `build-gui/tests_qt/digitalforge_qt_tests.exe` - requiere el PATH de Qt (ver
  arriba).

Correr ambas despues de cualquier cambio antes de darlo por terminado.

## Estado actual (ultima entrega)

Fase 4 - "Subcircuitos": un documento del `Project` usado como
componente/caja negra dentro de otro (`ComponentCategory::Subcircuits`,
typeId unico `structural.subcircuit`, propiedad `targetPath` = ruta
relativa al `.dfc` referenciado). Era la unica pieza de "Fuera de
alcance" que quedaba de las fases originales (estaba pausada de una
investigacion de diseno de una sesion anterior sin registro en el repo).

Mas grande que Fase 2/3 porque rompe el supuesto de que todo componente se
resuelve con una closure estatica sin conocimiento externo: derivar los
pines/simular un subcircuito necesita leer el documento *referenciado*.
Solucion (detalle completo en `docs/component-status.md`):

- `src/components/ExternalDocumentView.hpp` (nuevo): interfaz minima que
  `editor::CircuitDocument` implementa (`boundaryPins`/`flattenInto`/
  `containsSubcircuit`), para que `components::` no dependa de `editor::`
  (que ya depende de `components::` - evita un ciclo).
- `ComponentDefinition` gano un par de closures **opcionales**
  (`deriveExternalPins`/`buildExternalSimulation`) que solo usa
  `structural.subcircuit` - los otros 25 tipos no se tocaron.
- Los pines del subcircuito son los `wiring.input`/`wiring.output` del
  documento referenciado, ordenados por posicion en el lienzo (Y y luego
  X) y nombrados con su `Etiqueta` (sin propiedades nuevas en
  wiring.input/output).
- El aplanado (`CircuitDocument::flattenInto`) clona cada componente
  interno dentro del `Circuit` de quien lo incrusta, salvo los de frontera
  (que se alian directamente al net externo, sin agregar su propio
  `GateType::InputPin` - evitaria un segundo driver). `core::` no cambio en
  absoluto.
- `Project` reinstala el resolver de documentos hermanos (por ruta
  relativa) cada vez que la lista de documentos cambia, y `loadFromFile`
  ahora carga en dos pasadas con orden de dependencia (hojas primero) -
  ver el bug de orden de carga que expuso el test de round-trip, arreglado
  ahi mismo.

Alcance v1 (decidido con el usuario): pines derivados por posicion en el
lienzo (no por orden de creacion ni un indice explicito nuevo); solo un
nivel de anidamiento (un documento con un subcircuito no puede a su vez
usarse como subcircuito de otro - se valida en `CircuitDocument::setProperty`);
disponibilidad automatica en la paleta de cualquier documento del proyecto
via la categoria "Subcircuits"; el documento destino debe tener ya una ruta
guardada (si el proyecto nunca se guardo, no hay candidatos todavia). El
Property Inspector edita `targetPath` con el editor de String generico (una
caja de texto con la ruta relativa a mano) - un selector desplegable de
documentos queda para despues, igual que la propagacion reactiva si el
documento referenciado cambia de forma en otra pestaña (ver
`ComponentInstance::refreshDerivedPins()`, ya armado para eso).

Tests nuevos: 1 caso en `tests/test_components.cpp` (subcircuito sin
contexto externo: 0 pines, no tira al simular) y `tests_qt/test_subcircuit.cpp`
completo (4 casos: pines derivados/ordenados, simulacion aplanada de punta
a punta, rechazo de auto-referencia y de anidamiento, y el round-trip de
guardado/carga que expuso el bug de orden mencionado arriba).

Todo el codigo compila limpio (sin warnings nuevos) y ambas suites de tests
pasan (82 casos/430 aserciones headless, 25/89 Qt). **Pendiente**: verificar
en la app real (dos documentos, uno usado como subcircuito del otro,
cableado y simulado a traves del aplanado).

## Fuera de alcance (fases futuras, documentado pero no iniciado)

- **Bus / multi-bit real**: `LogicValue` es un enum escalar de 1 bit usado en
  todo `core::`; un bus real de N bits requeriria ensanchar el tipo de valor
  en todo el motor, no solo agregar una propiedad.
- **Simulacion analogica real (voltaje/corriente/resistencia)**: paradigma de
  valores continuos, completamente distinto al motor discreto de 5 valores
  actual.

## Convenciones importantes

- **Texto de UI y comentarios de codigo: en espanol, sin acentos**, siguiendo
  el estilo ya existente en el codigo (ver comentarios en cualquier archivo
  de `src/` como referencia).
- **No "corregir" silenciosamente valores que parezcan raros** (tamanos,
  colores, margenes) sin confirmar con el usuario primero: varias veces esos
  valores fueron ajustados a mano por el usuario mientras probaba la app, y
  no son errores.
- Los cambios visibles en la GUI se verifican relanzando la app y pidiendole
  al usuario que los pruebe el mismo - la automatizacion de mouse/teclado
  contra la ventana real no es confiable en este tipo de entorno; usar
  `gdb.exe` (del toolchain de MinGW de Qt) para diagnosticar crashes en vez de
  intentar reproducirlos por automatizacion.

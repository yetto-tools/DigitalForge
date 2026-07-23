# DigitalForge

Editor y simulador de lógica digital multiplataforma, con una implementación
propia (no derivada de Logisim Evolution) optimizada para manejar grandes
cantidades de compuertas y circuitos integrados.

## Descargas

Los binarios de cada versión están en la
[sección de Releases](https://github.com/yetto-tools/DigitalForge/releases):

- **Windows**: `DigitalForge-Setup-<versión>.exe` (instalador).
- **Linux**: `DigitalForge-<versión>-x86_64.AppImage` (dale permiso de
  ejecución con `chmod +x` y córrelo directamente).

La aplicación comprueba al arrancar si hay una versión más nueva publicada y lo
avisa; también se puede consultar manualmente desde **Ayuda → Buscar
actualizaciones**. Los releases los construye y publica automáticamente GitHub
Actions (ver `.github/workflows/release.yml`) al empujar un tag `v*`.

## Estado del proyecto

Versión actual: **0.1.0** (pre-alpha). El núcleo de simulación y el editor
gráfico Qt6 funcionan de extremo a extremo: se puede colocar componentes,
cablearlos, simular y guardar/abrir proyectos.

### Núcleo de simulación

- Valores lógicos de cinco estados (`0`, `1`, `Z`, `X`, `Error`) con las
  operaciones NOT/AND/OR/NAND/NOR/XOR/XNOR.
- Representación compacta de compuertas y redes (`std::vector` + índices,
  sin punteros ni herencia).
- Simulador dirigido por eventos (`Simulator`) con `start/pause/reset/step/
  runUntilStable/setInput/getNetValue`.
- Protección contra oscilaciones infinitas, eventos repetidos y conflictos
  entre múltiples salidas.
- Compila sin Qt (`core` y `components` son independientes de la GUI).

### Editor gráfico (Qt6)

- Lienzo de edición con colocación, cableado ortogonal con ruteo automático
  (`WireRouting`), uniones, selección, arrastre y ajuste a la rejilla.
- Deshacer/rehacer para todas las operaciones de edición (`UndoCommands`).
- Simulación interactiva con visualización de estados lógicos por color,
  barra de herramientas de simulación, tabla de verdad y registro de formas
  de onda.
- Paneles acoplables: paleta de componentes, árbol de proyecto, inspector de
  propiedades, minimapa; disposición por defecto restaurable.
- Control de zoom (10%–400%) en la barra de estado, selector de tema
  (del sistema / claro / oscuro), iconos nítidos generados por código.
- Carpeta de trabajo configurable y asociación de archivos `.dfproj`/`.dfc`.

### Biblioteca de componentes integrada (~40 tipos)

- Compuertas básicas y buffers/inversores triestado.
- Multiplexores, decodificadores, codificadores de prioridad.
- Aritmética: sumador, restador, comparador.
- Memoria: latch SR, flip-flops D y JK, registro.
- Cableado: entrada, salida, reloj, constante, túnel, tierra, resistencias de
  pull, transistores, compuerta de transmisión, etc.
- E/S: LED, display hexadecimal, matriz LED (directa y multiplexada),
  terminal; sonda de depuración; subcircuitos.

### Componentes definidos en JSON (Fase 3)

Además de la biblioteca integrada en C++, se pueden agregar tipos de
componente en archivos JSON, sin recompilar. Cada archivo declara pines fijos
y un comportamiento como *netlist* de compuertas primitivas del núcleo
(`And`/`Or`/`Xor`/`Not`/`DFlipFlop`/`TriStateBuffer`/constantes/...). Al
arrancar se cargan los `*.json` de `components/` (junto al ejecutable) o de la
carpeta indicada por `DIGITALFORGE_COMPONENTS_DIR`; un archivo malformado se
reporta y no impide cargar el resto. El formato, la validación y los ejemplos
están en [docs/component-format.md](docs/component-format.md).

Esto **no** es DFML todavía: es un cargador declarativo para combinacionales y
secuenciales armados de primitivas, no un lenguaje con compilador.

### Persistencia, formatos y compatibilidad

- Proyectos en JSON (`.dfproj` / `.dfc`) con `ProjectSerializer` y manifiesto.
- Importador de circuitos de Logisim (`LogisimImporter`).
- SHA-256 propio (`core::Sha256`) y huellas de componentes
  (`ComponentFingerprints`) para detectar cambios de interfaz, comportamiento
  o apariencia.
- Archivo de bloqueo `digitalforge.lock.json` junto al proyecto y reporte de
  compatibilidad al abrir. Ver [docs/dfml-metadata-status.md](docs/dfml-metadata-status.md).
- Versionado semántico de la app y versiones de esquema independientes por
  formato (`FormatVersions.hpp`).

### Empaquetado

- Instalador de Windows (Inno Setup) y Flatpak de Linux (ver más abajo).

## Limitaciones actuales

- El lenguaje DFML, su compilador (DFMC) y los paquetes externos (DFLIB) aún no
  existen. Los componentes vienen de la biblioteca integrada en C++ o de
  archivos JSON por netlist (ver arriba); esa carga JSON todavía no permite
  pines ni comportamiento dependientes de propiedades (netlist fijo), y dibuja
  los componentes con la caja genérica.
- La metadata desconocida no crítica se pierde al reguardar (criterio 29) y el
  reporte de compatibilidad todavía no se muestra en la interfaz (criterio 30).
- Los buses de múltiples bits aún no están implementados (cada net es de 1 bit).
- Reordenar los pines de una definición ya colocada aún reconecta mal en
  caliente (la reconexión por clave solo protege la carga de proyectos).

## Dependencias

- CMake 3.24 o superior.
- Compilador con soporte de C++20 (GCC reciente, Clang reciente o MSVC reciente).
- Catch2 (se descarga automáticamente vía `FetchContent` al configurar).
- Qt 6.5 o superior (solo cuando se active `DIGITALFORGE_BUILD_GUI`, a partir de la Fase 2).

## Compilación en Windows (PowerShell)

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\benchmark\digitalforge_benchmark.exe
```

Con el generador MinGW Makefiles (usado durante el desarrollo de la Fase 1),
los ejecutables quedan en `build\tests\` y `build\benchmark\` en lugar de
`build\Release\`.

## Compilación de la GUI (Windows)

Requiere Qt 6.5+ (mingw_64) y el MinGW que viene con ese kit de Qt (deben
coincidir, o el `.exe` falla al arrancar por DLLs de runtime desajustadas -
ver mas abajo). Ejemplo con Qt 6.7.3 mingw_64:

```powershell
$env:PATH = "C:\Qt\6.7.3\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;$env:PATH"
cmake -S . -B build-gui -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DDIGITALFORGE_BUILD_GUI=ON
cmake --build build-gui --target DigitalForge -j
```

El ejecutable queda en `build-gui\src\app\DigitalForge.exe`. Si no arranca por
falta de DLLs, hace falta copiar junto al `.exe` (o tener en el PATH de esa
sesion) `Qt6Core.dll`/`Qt6Gui.dll`/`Qt6Widgets.dll` (de
`C:\Qt\6.7.3\mingw_64\bin`), `libstdc++-6.dll`/`libgcc_s_seh-1.dll`/
`libwinpthread-1.dll` (del mismo MinGW que compilo el proyecto) y
`platforms\qwindows.dll` - `windeployqt` automatiza esto (ver la seccion de
instalador de Windows mas abajo).

## Compilación en Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/benchmark/digitalforge_benchmark
```

## Generar instalador de Windows

Requiere tener [Inno Setup 6](https://jrsoftware.org/isdl.php) instalado.
El script compila la GUI en modo `Release`, corre `windeployqt` para reunir
las DLLs de Qt y del runtime de MinGW, y empaqueta todo con Inno Setup:

```powershell
powershell -File packaging\windows\build_installer.ps1
```

El instalador queda en `packaging\windows\output\DigitalForge-Setup-<version>.exe`.

## Generar Flatpak de Linux

Requiere `flatpak` y `flatpak-builder` instalados, y el remoto Flathub
agregado (`flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo`).
Desde la raíz del repo:

```bash
flatpak-builder --user --install --force-clean \
    packaging/linux/build-dir \
    packaging/linux/io.github.yetto_tools.DigitalForge.yml
flatpak run io.github.yetto_tools.DigitalForge
```

## Pruebas

El núcleo se prueba con Catch2 (`tests/`), cubriendo tablas de verdad,
propagación de `Unknown`/`HighImpedance`, detección de conflictos, orden de
eventos, circuitos combinacionales, detección de oscilaciones, huellas de
componentes y ruteo de cables. El editor y los formatos tienen una suite Qt
aparte (`tests_qt/`: documento de circuito, serializador de proyecto, archivo
de bloqueo, compatibilidad y ruteo), que se compila solo con
`DIGITALFORGE_BUILD_GUI=ON`.

```bash
ctest --test-dir build --output-on-failure
```

## Benchmark

`digitalforge_benchmark` construye, sin interfaz gráfica, tres topologías
reproducibles (cadena de inversores, árbol de NAND, red combinacional
aleatoria con semilla fija) en tamaños de 10,000 / 100,000 / 1,000,000
compuertas, y reporta eventos procesados, tiempo de estabilización y una
estimación de memoria.

## Formato de componentes

Los componentes se registran en un `ComponentRegistry` en memoria desde dos
fuentes:

- La **biblioteca integrada en C++** (`src/components/BasicComponentLibrary.cpp`),
  que cubre los tipos paramétricos (ancho configurable, etc.) y los 74LSxx.
- **Archivos JSON por netlist** que se cargan al arrancar sin recompilar — la
  Fase 3. Formato, validación y ejemplos en
  [docs/component-format.md](docs/component-format.md).

A futuro, los componentes también se podrán definir con el lenguaje DFML y
empaquetar como bibliotecas externas (DFLIB); la capa de metadata y
compatibilidad ya está preparada para ese modelo (ver
[docs/dfml-metadata-status.md](docs/dfml-metadata-status.md)).

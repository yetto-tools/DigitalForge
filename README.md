# DigitalForge

Editor y simulador de lógica digital multiplataforma, con una implementación
propia (no derivada de Logisim Evolution) optimizada para manejar grandes
cantidades de compuertas y circuitos integrados.

## Estado del proyecto

**Fase 1 completa: núcleo de simulación sin interfaz gráfica.**

Implementado:
- Valores lógicos de cinco estados (`0`, `1`, `Z`, `X`, `Error`) con las
  operaciones NOT/AND/OR/NAND/NOR/XOR/XNOR.
- Representación compacta de compuertas y redes (`std::vector` + índices,
  sin punteros ni herencia).
- Simulador dirigido por eventos (`Simulator`) con `start/pause/reset/step/
  runUntilStable/setInput/getNetValue`.
- Protección contra oscilaciones infinitas, eventos repetidos y conflictos
  entre múltiples salidas.
- Suite de pruebas (Catch2) y benchmark headless hasta 1,000,000 de compuertas.

Pendiente (fases siguientes): interfaz gráfica Qt6, bibliotecas JSON (básica
y 74LSxx), guardado/carga de proyectos, subcircuitos, sondas y optimizaciones
adicionales de memoria/renderizado.

## Limitaciones actuales

- No hay interfaz gráfica todavía (Fase 2).
- No hay carga de componentes desde JSON todavía (Fase 3).
- No hay persistencia de proyectos todavía (Fase 4).
- Los buses de múltiples bits, subcircuitos y retardos de propagación
  configurables aún no están implementados.

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

## Compilación en Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/benchmark/digitalforge_benchmark
```

## Pruebas

El núcleo se prueba con Catch2 (`tests/`), cubriendo tablas de verdad,
propagación de `Unknown`/`HighImpedance`, detección de conflictos, orden de
eventos, circuitos combinacionales y detección de oscilaciones.

```bash
ctest --test-dir build --output-on-failure
```

## Benchmark

`digitalforge_benchmark` construye, sin interfaz gráfica, tres topologías
reproducibles (cadena de inversores, árbol de NAND, red combinacional
aleatoria con semilla fija) en tamaños de 10,000 / 100,000 / 1,000,000
compuertas, y reporta eventos procesados, tiempo de estabilización y una
estimación de memoria.

## Formato de bibliotecas

A partir de la Fase 3, los componentes combinacionales simples y los
circuitos integrados 74LSxx se definirán mediante archivos JSON (ver
`docs/component-format.md`, pendiente de esa fase).

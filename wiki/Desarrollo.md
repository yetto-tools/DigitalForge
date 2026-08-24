# Desarrollo

## Requisitos

- CMake 3.24 o posterior.
- Compilador compatible con C++20.
- Qt 6.8 o posterior para la interfaz gráfica.
- Catch2 y nlohmann/json se obtienen mediante CMake.

El núcleo y el sistema de componentes pueden compilarse sin Qt.

## Compilación básica

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Interfaz gráfica

```bash
cmake -S . -B build-gui -DDIGITALFORGE_BUILD_GUI=ON
cmake --build build-gui
ctest --test-dir build-gui --output-on-failure
```

En Windows con MinGW, Qt y el compilador deben pertenecer al mismo kit. Si el
ejecutable no encuentra las DLL, ejecuta `windeployqt` o agrega al entorno las
rutas del kit correcto.

## Pruebas

- `tests/`: núcleo y componentes, sin Qt.
- `tests_qt/`: documentos, editor, formatos, subcircuitos, tabla de verdad,
  formas de onda y otras funciones dependientes de Qt.
- `benchmark/`: topologías reproducibles de hasta un millón de compuertas.

Ejecuta ambas suites después de modificar código que afecte a la aplicación.

## Estructura del repositorio

```text
src/core/        Motor de simulación
src/components/  Definiciones e instancias de componentes
src/editor/      Modelo y herramientas del editor
src/ui/          Paneles y presentación
src/app/         Ventana principal y arranque
src/formats/     Serialización, manifiestos y compatibilidad
tests/           Pruebas sin Qt
tests_qt/        Pruebas con Qt
packaging/       Instaladores y paquetes
```

## Contribuciones

Antes de enviar un cambio:

1. Mantén la separación entre el núcleo y Qt.
2. Usa identificadores `typeId` estables para los componentes.
3. Añade o actualiza pruebas.
4. Ejecuta las suites correspondientes.
5. Documenta los cambios visibles en `CHANGELOG.md`.


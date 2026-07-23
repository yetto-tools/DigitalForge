# Metadata y compatibilidad — estado de implementación

Seguimiento de los **criterios de aceptación de metadata** de la especificación
DFML frente a lo que hay realmente en el código. Se actualiza a medida que
cada pieza se implementa; un criterio solo se marca **Sí** cuando tiene código
y pruebas que lo cubren.

Contexto importante: DFML, su compilador, el formato compilado DFMC y los
paquetes DFLIB **todavía no existen**. Los componentes son
`ComponentDefinition` construidas en C++ (`src/components/BasicComponentLibrary.cpp`)
y registradas en un `ComponentRegistry` en memoria. La capa de metadata se
implementó primero porque no depende del lenguaje: protege los proyectos desde
hoy y es la base que DFMC y el archivo de bloqueo reutilizarán.

## Estado por criterio

| # | Criterio | Estado | Dónde |
|---|---|---|---|
| 1 | `libraryId` estable por biblioteca | No | Requiere el formato de biblioteca externa |
| 2 | Versionado semántico de biblioteca | No | Ídem |
| 3 | `typeId` estable por componente | Sí | `ComponentDefinition::typeId`, ya era estable y se guarda en el proyecto |
| 4 | `definitionVersion` por componente | Sí | `ComponentDefinition::definitionVersion` |
| 5 | Versión del lenguaje DFML por archivo | No | No hay archivos DFML |
| 6 | Compatibilidad declarada con DigitalForge | No | Llega con el manifiesto de biblioteca |
| 7 | Características requeridas por definición | No | Falta el sistema de feature flags |
| 8 | `sourceHash` | No aplica aún | No hay archivo fuente que hashear |
| 9 | `dependencyHash` | No aplica aún | No hay grafo de dependencias entre bibliotecas |
| 10 | `publicInterfaceHash` | Sí | `components::computeFingerprints` |
| 11 | `pinInterfaceHash` | Sí | Ídem — por clave de pin y dirección, **sin el índice** |
| 12 | `propertyInterfaceHash` | Sí | Ídem — id, tipo, default, límites, enums, flags |
| 13 | `simulationHash` | Sí, con salvedad | Depende de `behaviorVersion` declarada: `buildSimulation` es una closure de C++ y su cuerpo no es inspeccionable |
| 14 | `appearanceHash` | Sí, con salvedad | Ídem con `appearanceVersion` |
| 15 | `packageHash` | Sí | Secuencia física del DIP (`physicalPinout`) |
| 16 | Hashes almacenados en DFMC | No | No hay DFMC |
| 17 | El proyecto almacena versiones y hashes | Sí | `formats::serializeProject` escribe `definitionVersion` + `compatibility` por instancia |
| 18 | Archivo de bloqueo con versiones exactas | Sí | `formats::LockFile` escribe `digitalforge.lock.json` junto al proyecto al guardar; `compareLockFiles` detecta cambios silenciosos |
| 19 | Un cambio visual no rompe la simulación | Sí | `CompatibilityVerdict::AppearanceOnly` |
| 20 | Un cambio interno compatible no elimina conexiones | Sí | `RequiresRecompile`; la carga nunca descarta el componente |
| 21 | Un cambio de pin incompatible no se resuelve por índice | Sí | Los cables guardan `pinKey`; al cargar se reconecta por clave, y una clave inexistente deja el cable sin restaurar y lo reporta (`unresolvedWires`) en vez de reconectar por índice |
| 22 | Advertencias por propiedades obsoletas | No | Falta marcar propiedades como obsoletas |
| 23 | Componentes obsoletos con reemplazo declarado | No | Falta el bloque `lifecycle` |
| 24 | Alias explícitos de `typeId` | No | — |
| 25 | Migraciones declarativas y seguras | No | — |
| 26 | Invalidación correcta de cachés | No aplica aún | No hay caché compilada |
| 27 | Dependencias transitivas invalidan la caché | No aplica aún | Ídem |
| 28 | Extensiones críticas no soportadas producen error | No | — |
| 29 | La metadata desconocida no crítica se conserva | **No** | El serializador reescribe el JSON desde cero: los campos desconocidos se pierden al guardar |
| 30 | DigitalForge explica por qué algo es incompatible | **Parcial** | `ProjectCompatibilityReport` tiene el veredicto por instancia; falta mostrarlo en la interfaz |

## Lo implementado, en detalle

**`src/core/Sha256.{hpp,cpp}`** — SHA-256 autocontenido. `core`/`components`
compilan sin Qt, así que no puede usarse `QCryptographicHash`. Validado contra
los vectores publicados en FIPS 180-4. `Sha256Builder` acumula campos con
prefijo de longitud, de modo que `{"ab","c"}` y `{"a","bc"}` no colisionen.

**`src/components/ComponentFingerprints.{hpp,cpp}`** — las seis huellas y
`compareFingerprints()`, que clasifica en `Identical` / `AppearanceOnly` /
`RequiresRecompile` / `RequiresMigration`, de mayor a menor gravedad.

- La clave persistente de pin es `PinTemplate::name`, no un campo nuevo: ya era
  estable y significativa (`CLK`, `R0C0`). Renombrar un pin es, correctamente,
  un cambio de interfaz.
- `displayName`, `description` y `multiline` quedan **fuera** de las huellas:
  son presentación, no contrato.

**`src/formats/ProjectSerializer.{hpp,cpp}`** — cada instancia guarda
`definitionVersion` y un bloque `compatibility` con las seis huellas en
hexadecimal. Al abrir, `loadProject()` acepta un `ProjectCompatibilityReport*`
opcional y compara contra las definiciones instaladas.

Decisiones deliberadas:

- La carga **nunca se aborta** por una incompatibilidad. El objetivo es poder
  explicar qué cambió, no perder el trabajo del usuario.
- Una huella ausente o corrupta se trata como **no comparable**, no como
  distinta: inventar una incompatibilidad a partir de un dato ilegible sería
  una falsa alarma.
- `hasStoredMetadata` distingue "no hay nada contra qué comparar" (proyecto
  anterior a las huellas) de "todo coincide".
- El `schemaVersion` del proyecto sigue en 1: el bloque es aditivo y opcional,
  así que los proyectos viejos abren y los nuevos también abren en versiones
  anteriores de la aplicación.

## Reconexión por clave de pin (criterio 21)

Cada extremo de cable guarda ahora `pinKey` además de `pinIndex`. Al cargar
manda la clave:

- **Pines reordenados**: el cable se reconecta al pin cuya clave coincide,
  aunque su índice haya cambiado. El índice guardado se ignora.
- **Clave inexistente** (pin eliminado o renombrado sin alias): el cable **no**
  se reconecta por índice — se deja sin restaurar y se agrega a
  `ProjectCompatibilityReport::unresolvedWires`. Los componentes no se tocan,
  así que no se pierde el resto del trabajo.
- **Archivos anteriores** a este campo: sin `pinKey` guardada, el índice es lo
  único que ese archivo registró, así que se sigue usando. El `schemaVersion`
  del proyecto sigue en 1 (campo aditivo).

## Archivo de bloqueo (criterio 18)

`formats::LockFile` genera `digitalforge.lock.json` junto al `.dfproj` en cada
guardado (`Project::saveToFile`). Registra:

- `lockFormatVersion` — versión **independiente** del esquema del lock
  (`formats::versions::kLockSchema`), separada del esquema del proyecto y del
  manifiesto (ver `FormatVersions.hpp`, criterio de versiones de formato
  distintas).
- `digitalForgeVersion` — versión semántica de la app (`core::kDigitalForgeVersion`).
- Por cada tipo de componente **realmente usado**: `definitionVersion`,
  `publicInterfaceHash` y `simulationHash`.
- La biblioteca integrada con un `contentHash` acumulado de las interfaces
  públicas de sus tipos en uso.

`compareLockFiles(stored, current)` clasifica las diferencias en
`Added` / `Removed` / `Changed`, que es lo que permite "detectar
actualizaciones" y "evitar cambios silenciosos" sin abrir el proyecto entero.

Nota: hoy solo existe la biblioteca integrada, así que el lock tiene una sola
entrada de librería que versiona con la app. Cuando existan bibliotecas
externas (DFLIB), cada componente dirá de cuál viene y el lock listará varias.

## Huecos conocidos

1. **Criterio 29**: `serializeProject()` reconstruye el JSON desde cero, así
   que cualquier campo desconocido presente en el archivo se pierde al guardar.
2. **Criterios 13 y 14**: `simulationHash` y `appearanceHash` dependen de que
   el autor recuerde subir `behaviorVersion` / `appearanceVersion`. Olvidarlo
   no rompe proyectos, pero deja un cambio sin detectar. Con DFML esto se
   resolvería solo, porque el hash saldría del archivo fuente.
3. **Criterio 30**: el reporte existe pero no se muestra en la interfaz.

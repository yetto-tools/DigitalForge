# Formato de componentes en JSON

DigitalForge puede cargar tipos de componente definidos en archivos JSON,
sin recompilar. Cada archivo describe **un** tipo de componente y su
comportamiento como un *netlist* de compuertas primitivas del núcleo. Es la
primera etapa de la Fase 3: no reemplaza a la biblioteca integrada en C++
(que sigue cubriendo los tipos paramétricos y los 74LSxx), sino que la
complementa con componentes que el usuario puede agregar por su cuenta.

> Esto **no** es DFML. DFML, su compilador (DFMC) y los paquetes externos
> (DFLIB) siguen sin existir. Este formato es un cargador declarativo y
> autocontenido, pensado para componentes combinacionales y secuenciales
> armados a partir de las primitivas que el simulador ya sabe evaluar.

## Dónde se cargan

Al abrir la aplicación se carga cada archivo `*.json` de:

1. la carpeta indicada por la variable de entorno `DIGITALFORGE_COMPONENTS_DIR`
   (pensada para desarrollo y pruebas), o
2. si esa variable no está definida, la carpeta `components/` junto al
   ejecutable (donde el instalador deja la biblioteca).

Los archivos se procesan en orden alfabético. Un archivo malformado **no**
impide cargar los demás: se registra el error y se continúa. Los errores
quedan en `CircuitDocument::componentLibraryReport()`.

## Estructura del archivo

```json
{
  "typeId": "custom.and3",
  "displayName": "AND-3",
  "description": "Compuerta AND de tres entradas.",
  "category": "Gates",
  "definitionVersion": 1,
  "pins": [
    {"name": "A", "dir": "in"},
    {"name": "B", "dir": "in"},
    {"name": "C", "dir": "in"},
    {"name": "Y", "dir": "out"}
  ],
  "netlist": [
    {"gate": "And", "in": ["A", "B"], "out": "t"},
    {"gate": "And", "in": ["t", "C"], "out": "Y"}
  ]
}
```

### Campos

| Campo | Obligatorio | Descripción |
|---|---|---|
| `typeId` | sí | Identificador estable y único (p. ej. `custom.and3`). No debe chocar con uno ya registrado. |
| `displayName` | no | Nombre visible; por defecto, el `typeId`. |
| `description` | no | Texto descriptivo. |
| `category` | no | Rama de la paleta: `Wiring`, `Gates`, `Plexers`, `Arithmetic`, `Memory`, `Subcircuits`, `IO`, `Base`, `Analysis`, `74LSxx`. Por defecto `Base`. |
| `definitionVersion`, `behaviorVersion`, `appearanceVersion` | no | Enteros positivos para el sistema de compatibilidad (ver `docs/dfml-metadata-status.md`). Por defecto 1. |
| `pins` | sí | Lista no vacía de pines. Cada pin: `name` (único, no vacío) y `dir` (`in` u `out`). El orden es el de conexión. |
| `netlist` | sí | Lista no vacía de compuertas primitivas (ver abajo). |

### Compuertas del netlist

Cada entrada del `netlist` es una compuerta primitiva del núcleo:

```json
{"gate": "And", "in": ["A", "B"], "out": "Y"}
```

- `gate`: nombre del tipo primitivo (ver tabla). Distingue mayúsculas.
- `in`: nombres de las nets de entrada (puede omitirse en las fuentes de 0 entradas).
- `out`: nombre de la net de salida (siempre exactamente una).

Un **nombre de net** es o bien un pin del componente, o bien una net interna
(cualquier nombre que no sea un pin). Las nets internas se crean solas al
usarse por primera vez.

Primitivas disponibles y su aridad:

| `gate` | Entradas | Notas |
|---|---|---|
| `Buffer`, `Not` | 1 | |
| `And`, `Or`, `Nand`, `Nor`, `Xor`, `Xnor` | ≥ 2 | |
| `TriStateBuffer` | 2 | `in`: D, OE |
| `DFlipFlop` | 2 o 4 | `in`: D, CLK, y opcionalmente PRE, CLR asincronicos activos en alto (con estado propio). Q arranca en Zero (no flotante) al construir/reiniciar la simulacion - igual convencion que otros simuladores de referencia (Logisim, Proteus), necesaria para que un flip-flop en modo toggle (J=K atados, o un `memory.tFlipFlop`) resuelva solo sin depender de un reset explicito. |
| `ConstantZero`, `ConstantOne` | 0 | fuente fija |
| `WeakZero`, `WeakOne` | 0 | fuente débil (pull-down/pull-up) |

`InputPin` **no** es válido dentro de un netlist: las entradas del componente
son sus propios pines de entrada, controlados por lo que se cablee afuera.

## Validación (en tiempo de carga)

El cargador rechaza, con un mensaje que nombra el `typeId`, cualquier
definición que:

- tenga un campo obligatorio faltante o del tipo equivocado;
- use un `gate` desconocido o `InputPin`;
- dé a una compuerta una cantidad de entradas inválida para su tipo;
- haga que una compuerta **maneje un pin de entrada** del componente;
- deje un **pin de salida sin ninguna compuerta que lo maneje**;
- **lea una net que nada controla** (ni pin de entrada ni salida de otra compuerta);
- repita el nombre de un pin.

## Cómo se dibujan

En esta primera versión un componente cargado de JSON se dibuja como una caja
redondeada con su `displayName` centrado y sus pines (el render genérico del
editor). Para obtener ese dibujo limpio conviene **no** usar los prefijos
reservados de tipos con dibujo propio (`gates.`, `plexers.`, `arithmetic.`,
`memory.`, `ic74ls.`, ni los `typeId` exactos de `wiring.*`/`io.*`); por eso
los ejemplos usan el prefijo `custom.`.

## Limitaciones actuales

- Los pines y el netlist son **fijos**: todavía no hay pines ni comportamiento
  que dependan de propiedades (como el ancho `bits` del sumador integrado).
- Las únicas propiedades editables son las de presentación (etiqueta, color,
  tamaño, notas), inyectadas automáticamente.
- El dibujo es genérico (sin forma bespoke por tipo).
- No hay `partNumber` ni layout físico de DIP.

## Ejemplos incluidos

En `components/` del repositorio hay tres ejemplos: `custom.and3`,
`custom.majority3` (votador de mayoría) y `custom.halfAdder` (medio sumador,
con dos pines de salida).

# Estado de componentes

Este documento se actualiza al final de cada fase B (B1-B7). Solo se marca
un componente como **Implementado** cuando tiene comportamiento real, pruebas
que lo cubren y el proyecto compila y pasa `ctest` con él incluido.

## FASE B1 — Núcleo combinacional

| Componente | typeId | Estado | Notas |
|---|---|---|---|
| Entrada | `wiring.input` | Implementado | `GateType::InputPin`; propiedad `initialValue` (0/1/Z/X) aplicada vía `Simulator::setInput` una vez existe el simulador. |
| Reloj | `wiring.clock` | Implementado | Agregado a pedido explícito (falta respecto a la librería "Wiring" de Logisim). Misma `ComponentDefinition` que `wiring.input` (`GateType::InputPin`) con una propiedad `periodMs` en vez de `initialValue` — la `ComponentDefinition` en sí es puramente sincrónica. El único componente de todo el proyecto respaldado por un `QTimer` real: `editor::CircuitDocument::rebuildClockTimers()`/`onClockTimeout()` alternan su valor cada medio `periodMs` mientras `isLiveSimulation()` es true (pausar congela el reloj como cualquier otra propagación), reconstruido en cada `rebuildSimulation()`. Reusa `paintInput()` para el dibujo en el lienzo (el glifo 0/1 en vivo ya alcanza) y un ícono de onda cuadrada propio en la paleta. |
| Salida | `wiring.output` | Implementado | Sumidero puro (sin `Gate` propio), solo observa una red. |
| Constante | `wiring.constant` | Implementado | Propiedad `value` (0/1) selecciona `ConstantZero`/`ConstantOne`. |
| Resistencia pull-up/pull-down | `wiring.pullResistor` | Implementado | Agregado a pedido explícito (misma lista de "Wiring" de Logisim). Propiedad `value` (0/1) selecciona los nuevos `GateType::WeakZero`/`WeakOne` — un "driver débil" que `Simulator::applyGateOutput()` resuelve en un segundo nivel, aparte de los drivers normales (`isWeakType()`): solo toma el control de la red si ningún driver normal la impulsa activamente, y nunca genera un conflicto (`Error`) contra uno normal. |
| Reinicio al encender | `wiring.powerOnReset` | Implementado | Agregado a pedido explícito. Misma `ComponentDefinition` que `wiring.clock` (`GateType::InputPin`) con una propiedad `pulseMs`. `editor::CircuitDocument::rebuildPowerOnResetTimers()`/`onPowerOnResetTimeout()`: arranca en `One` en cada `rebuildSimulation()` ("encender" = reconstruir, lo que ya hace el botón Reiniciar) y cae a `Zero` una única vez, pasado `pulseMs` (`QTimer` de un solo disparo, a diferencia del periódico de `wiring.clock`) — pausar/reanudar después de que ya disparó no rearma otro pulso (`firedPowerOnResets_`). |
| No conectar | `wiring.doNotConnect` | Implementado | Agregado a pedido explícito. Puramente documental: 0 pines, `ComponentSimBinding::Kind::None` (sin ninguna huella en la simulación — primer uso real de ese valor en todo el proyecto). |
| Túnel | `wiring.tunnel` | Implementado | Agregado a pedido explícito. `Kind::None`: la conexión real la hace `CircuitDocument::rebuildSimulation()`, uniendo (union-find, mismo mecanismo que los cables/junctions) todas las instancias que comparten la propiedad `label` — a diferencia del resto de los tipos, esta `label` tiene `affectsSimulation = true`. |
| Tierra lógica | `wiring.ground` | Implementado | Agregado a pedido explícito. Fuente fija en 0 (`GateType::ConstantZero`, driver fuerte — a diferencia de la resistencia pull-down, un corto real contra un 1 sí debe ser un conflicto), con el símbolo real de tierra (IEC) en vez de un cuerpo genérico. |
| Transistor | `wiring.transistor` | Implementado | Agregado a pedido explícito. Logisim mismo simplifica su Transistor a un paso *unidireccional* condicionado por la compuerta — exactamente lo que ya hace `GateType::TriStateBuffer`. NMOS es ese `TriStateBuffer` directo (EN = Gate); PMOS es el mismo `TriStateBuffer` con Gate invertido antes (`GateType::Not`), igual idea que `gates.tristateInverter`. Propiedad `type` (NMOS/PMOS). Sin cambios en `core::`. |
| Compuerta de transmisión | `wiring.transmissionGate` | Implementado | Agregado a pedido explícito. Par NMOS+PMOS en paralelo sobre el mismo `TriStateBuffer`: `EN = AND(N, NOT(P))` — solo conduce con control complementario correcto (N=1, P=0); si N y P están mal cableados, queda bloqueada en vez de ignorar a P. Sin cambios en `core::`. |
| Cable (1 bit) | — | **No es un componente independiente** | Una conexión de 1 bit ya existe como red compartida en `Circuit`; no tiene lógica ni propiedades propias en esta capa. Tendrá `WireItem` visual en la fase de GUI. |
| AND | `gates.and` | Implementado | Entradas configurables (2-64) vía propiedad `inputCount`. |
| OR | `gates.or` | Implementado | Idem. |
| NOT | `gates.not` | Implementado | Entrada fija de 1 pin. |
| NAND | `gates.nand` | Implementado | Entradas configurables. |
| NOR | `gates.nor` | Implementado | Entradas configurables. |
| XOR | `gates.xor` | Implementado | Entradas configurables (paridad). |
| XNOR | `gates.xnor` | Implementado | Entradas configurables (paridad negada). |
| LED | `io.led` | Implementado | Propiedad `activeHigh`; sumidero puro. Sin apariencia gráfica real todavía (pendiente de GUI). |
| Sonda lógica | `debug.probe` | Implementado | Sumidero puro que expone el valor exacto de cinco estados de una red. |
| Display 7 segmentos | `io.seven_segment` | Implementado | Adelantado desde FASE B6 a pedido explícito. Pines `a..g` fijos + `dot` opcional (propiedad `hasDecimalPoint`). Propiedad `commonAnode` (ánodo común = activo en bajo, cátodo común = activo en alto, por defecto). Sumidero puro sobre múltiples redes (`ComponentSimBinding::observedNets`, generalizado desde un único `observedNet`). No decodifica BCD/hex: cada segmento se maneja de forma independiente, igual que el hardware real. |

**No implementado todavía** (explícitamente fuera de alcance de B1, per las
listas de fases B2-B7): buses multi-bit, negación individual por entrada,
buffer/inversor triestado, multiplexores, aritmética, memoria, resto de E/S
(display hexadecimal con decodificador BCD, matriz LED, terminal, etc.),
subcircuitos, apariencia gráfica real (GUI Qt), analizador de señales, tabla
de verdad automática.

## Plexers (posterior a B1)

| Componente | typeId | Estado | Notas |
|---|---|---|---|
| Decodificador | `plexers.decoder` | Implementado | Propiedad `selectBits` (1-4). |
| Multiplexor | `plexers.multiplexer` | Implementado | Idem. |
| Demultiplexor | `plexers.demultiplexer` | Implementado | Idem. |
| Codificador de prioridad | `plexers.priorityEncoder` | Implementado | Idem, mas pin `valid`. |

## Fase 2 — Aritmética

Sumador/restador/comparador de N bits, siguiendo el mismo patrón que los
Plexers: un ancho configurable (`bits`, 1-64) expuesto como pines
individuales de 1 bit por operando (`A0..A(bits-1)`/`B0..B(bits-1)`), no un
bus real (sigue fuera de alcance según `ONBOARDING.md`). Lógica sintetizada
internamente con los primitivos de siempre (`GateType::And/Or/Not/Xor`).

| Componente | typeId | Estado | Notas |
|---|---|---|---|
| Sumador | `arithmetic.adder` | Implementado | Ripple-carry de N bits; pines `Cin`/`Cout`. |
| Restador | `arithmetic.subtractor` | Implementado | Full-subtractor de N bits (no reutiliza el sumador); pines `Bin`/`Bout` (préstamo). |
| Comparador | `arithmetic.comparator` | Implementado | Comparador de magnitud sin signo de N bits; salidas `GT`/`EQ`/`LT`. Sin pines de cascada (tipo 74LS85) - eso queda para `ComponentCategory::Ic74LS`. |

## Fase 3 — Memoria

Primeros componentes con estado real del proyecto. Los latches (nivel, no
biestables) siguen siendo 100% combinacionales con realimentacion (el
simulador ya soporta ciclos de gates sin cambios, ver el test de oscilacion
en `test_simulator.cpp`); los biestables disparados por flanco si necesitaron
un unico tipo de gate nuevo con estado propio en `core::` (`GateType::DFlipFlop`,
manejado aparte en `Simulator::step()` detectando el flanco de CLK) - todo lo
demas se compone encima de el a nivel de componente, igual que Plexers y
Aritmetica se componen sobre `gates.*`.

| Componente | typeId | Estado | Notas |
|---|---|---|---|
| Latch SR | `memory.srLatch` | Implementado | Dos `GateType::Nor` cruzados. S=R=1 simultaneo es la combinacion invalida clasica (emerge sola de las ecuaciones). |
| Flip-Flop D | `memory.dFlipFlop` | Implementado | Usa el nuevo `GateType::DFlipFlop` nativo; expone Q y Qn. |
| Flip-Flop JK | `memory.jkFlipFlop` | Implementado | Conversion JK->D combinacional realimentando el propio Q (`dEquiv = (J & !Q) \| (!K & Q)`) sobre el mismo `GateType::DFlipFlop`. |
| Registro | `memory.register` | Implementado | Propiedad `bits` (1-64, reutiliza el patron de Fase 2); un `DFlipFlop` por bit compartiendo un CLK comun. Sin `Qn` por bit. |

## Fase 4 — Subcircuitos

Un documento del `Project` usado como componente/caja negra dentro de otro -
la unica pieza de "Fuera de alcance" de `ONBOARDING.md` que quedaba de las
fases originales. A diferencia de Fases 2/3 (auto-contenidas dentro de
`ComponentDefinition`/`ComponentRegistry`), esta requiere leer el grafo de
*otro* `CircuitDocument`, algo que ninguna closure estatica de
`BasicComponentLibrary.cpp` puede alcanzar por si sola. Solucion:

- `src/components/ExternalDocumentView.hpp` (nuevo): interfaz de solo
  lectura (`boundaryPins`/`flattenInto`/`containsSubcircuit`) que
  `editor::CircuitDocument` implementa, mas `ExternalContext` (un
  `std::function` que resuelve una ruta relativa a una vista) - evita que
  `components::` dependa de `editor::` (que ya depende de `components::`).
- `ComponentDefinition` gana un par de closures **opcionales** y paralelas
  (`deriveExternalPins`/`buildExternalSimulation`), usadas solo por
  `structural.subcircuit`; los otros 25 tipos no se tocaron.
- `editor::CircuitDocument` implementa esa interfaz: `boundaryPins()` deriva
  los pines de sus propios `wiring.input`/`wiring.output`, ordenados por
  posicion en el lienzo (Y y luego X) y nombrados con su `Etiqueta`;
  `flattenInto()` clona cada componente interno (menos los de frontera, que
  se alian directamente al net externo sin agregar su propio
  `GateType::InputPin`, para no crear un segundo driver) dentro del
  `core::Circuit` de quien lo incrusta - `core::` no cambio en absoluto.
- `editor::Project` reinstala un resolver (documento hermano por ruta
  relativa) en cada documento cada vez que la lista cambia, y hace de
  `loadFromFile` una carga en dos pasadas (todas las entradas de documento
  primero, contenido despues, en un orden que carga los documentos "hoja"
  antes que los que los referencian) para que el orden del manifiesto nunca
  rompa un subcircuito ya cableado.

| Componente | typeId | Estado | Notas |
|---|---|---|---|
| Subcircuito | `structural.subcircuit` | Implementado | Propiedad `targetPath` (ruta relativa al `.dfc` referenciado). Solo un nivel de anidamiento (un documento con un subcircuito no puede a su vez usarse como subcircuito de otro); el documento destino debe tener ya una ruta guardada. |

## Fase A - Negacion por entrada y buffer/inversor triestado

Primera adicion a `core::` desde `DFlipFlop`: `GateType::TriStateBuffer` (2
entradas fijas, D y EN) es combinacional puro (a diferencia de `DFlipFlop`,
no necesita trato especial en `Simulator::step()` - `evaluateCombinationalGate`
alcanza). La negacion por entrada, en cambio, es pura composicion en
`components::` (igual que Plexers/Aritmetica/Memoria): no existe una forma
de tener una propiedad "por indice" cuyo rango dependa de otra propiedad
(`inputCount` varia 2-64), asi que se opto por una unica propiedad bitmask
(`invertMask`) en vez de N propiedades booleanas.

| Componente | typeId | Estado | Notas |
|---|---|---|---|
| Buffer triestado | `gates.tristateBuffer` | Implementado | Pines D/OE/Y. OE=1 repite D en Y, cualquier otro valor de OE deja Y en alta impedancia. |
| Inversor triestado | `gates.tristateInverter` | Implementado | Idem, pero Y = NOT(D) cuando OE=1. Sintetiza un NOT interno (`circuit.addNet()`) antes del `GateType::TriStateBuffer` compartido - no se agrego un segundo GateType. |
| Negacion por entrada | `invertMask` (propiedad nueva en gates.and/or/nand/nor/xor/xnor) | Implementado | Bit i en 1 invierte la entrada i (via un NOT interno sintetizado, mismo patron que `negateEach()` de Plexers) antes de que llegue a la puerta principal. Solo los bits hasta `inputCount` son significativos; sin validacion de rango dinamica. |

## Fase B - Resto de E/S

Mismo patron `Kind::Sink` + `observedNets` que `io.led`/`io.seven_segment` -
sin cambios en `core::`/`ComponentSimBinding`. A diferencia de
`io.seven_segment` (7 entradas independientes sin decodificar),
`io.hexDisplay` decodifica internamente un valor de 4 bits: por eso **no**
tiene una propiedad `commonAnode` (no hay una etapa de wiring real cuya
polaridad importe, ya que el componente informa directamente que segmentos
se ven encendidos vía `hexDisplayState()`).

| Componente | typeId | Estado | Notas |
|---|---|---|---|
| Display hexadecimal | `io.hexDisplay` | Implementado | 4 pines `bit0..bit3` (valor 0-15) + `dot` opcional. `hexDisplayState()` decodifica a los 7 segmentos via `hexDigitSegments()` (tabla pura 0-F); si algun bit no es 0/1 limpio, el digito completo queda invalido/apagado (no se puede decodificar una direccion ambigua). |
| Matriz LED | `io.ledMatrix` | Implementado | Propiedades `rows`/`cols` (1-8 cada una, tope 64 celdas). Un pin `R{r}C{c}` por celda; reutiliza el mismo criterio `activeHigh` que `io.led` via `ledMatrixCellIsLit()`. Sin direccionamiento/multiplexado. |
| Terminal | `io.terminal` | Implementado (v1 sin estado) | 8 pines `bit0..bit7` (bit0 = LSB). `terminalCharacter()` decodifica el byte actual a ASCII en cada paso. **Sin scrollback/historial**: `ComponentInstance`/`ComponentSimBinding` son tipos de valor sin slot para estado persistente por instancia - un historial real queda para una fase futura, en un panel de UI que observe `simulationStepped()`, no en esta capa. |

## Fase C - Tabla de verdad automatica

Nueva herramienta de analisis, no un componente: un panel dock de solo
lectura (`ui::TruthTablePanel`) mas la logica pura que hace el barrido
(`editor::computeTruthTable()`), separadas para que la segunda se pueda
testear sin instanciar ningun widget (la suite Qt corre sobre
`QCoreApplication`, no `QApplication` - ver `tests_qt/test_truth_table.cpp`).

- `editor::computeTruthTable(CircuitDocument&, maxInputs=20)`: enumera las
  `wiring.input`/`wiring.output` del documento, guarda el valor original de
  cada entrada, barre las 2^N combinaciones (`setInputValue` + la nueva
  `CircuitDocument::runUntilStable()`) y restaura los valores originales al
  terminar. Lanza `std::invalid_argument` si falta alguna entrada/salida o
  si hay mas de `maxInputs` entradas (un barrido mas grande es
  impracticable - no hay evaluacion async/incremental en este simulador).
- `CircuitDocument::runUntilStable()` (nuevo, chico): a diferencia de
  `setInputValue()`, que solo corre el simulador a un estado estable cuando
  `isLiveSimulation()` es true, esta nueva funcion fuerza un settle completo
  sin importar ni alterar el modo en vivo/pausado del documento - necesaria
  porque el barrido debe ver el resultado estable de cada combinacion sin
  tocar el estado de "Ejecutar/Pausar" que ve el usuario.
- `ui::TruthTablePanel`: boton "Generar" explicito (no se recalcula sola);
  se invalida (mensaje "el circuito cambio") ante `componentAdded`/
  `componentAboutToBeRemoved`/`propertyChanged`, mismo patron de
  reconexion que `PropertyInspector`. Aparece como pestaña junto a
  "Propiedades" en `MainWindow`.

## Fase D - Analizador de senales (waveform)

Segunda herramienta de analisis (no un componente), mismo patron de
separacion que Fase C: logica pura (`editor::WaveformRecorder`, un
`QObject` sin ningun widget) mas la presentacion
(`ui::WaveformPanel`/`ui::WaveformCanvas`) - la primera se testea sin
`QApplication` (`tests_qt/test_waveform_recorder.cpp`).

Confirmado durante el diseno: este simulador no tiene un reloj/timer de
simulacion continuo (`SimulationToolbar::onRun()` solo activa
`isLiveSimulation()`; la propagacion real ocurre de forma sincronica
dentro de cada `setInputValue()`/`step()`). Por eso el eje horizontal de
la forma de onda es un contador de muestra local
(`WaveformRecorder::currentSampleIndex()`), no el `clock_` real de
`Simulator` - exponer ese tick real se pospone (pueden ocurrir varios
eventos internos por cada `step()`/`setInputValue()`, asi que hoy no seria
un eje de tiempo significativo).

- `editor::WaveformRecorder`: hasta `kMaxWatchedNets` (8) redes vigiladas
  (identificadas por `editor::WireEndpoint`, el mismo tipo que ya usan
  `WireItem`/`JunctionItem` para pintar). Muestrea en cada
  `simulationStepped()` del documento, agregando una muestra nueva por red
  solo cuando su valor cambio (compresion "run-length" implicita).
  `simulationRebuilt()` limpia las muestras de cada red pero mantiene la
  lista de que se esta vigilando.
- `ui::WaveformPanel`: boton "Agregar seleccion" (emite `addRequested()`,
  que `MainWindow` conecta a la seleccion actual de la escena activa - el
  panel no conoce `CircuitScene`/`ComponentItem` directamente). v1 solo
  admite vigilar componentes de un solo pin (wiring.input/output,
  debug.probe, etc.) - un selector de pin especifico dentro de un
  componente multi-pin queda para una fase futura.
- `ui::WaveformCanvas`: `QWidget` de dibujo puro (tres niveles alto/medio/
  bajo segun One/ambiguo(X,Z,Error)/Zero, colores distintos, adaptable a
  tema claro/oscuro igual que `IconFactory`). Sin zoom/scroll en v1.

## Diseno visual (iconos/simbolos ANSI-IEEE)

Mejora visual sobre fases ya entregadas (no una fase de componentes nueva):
Plexers, Aritmetica, Memoria y Subcircuitos caian en el dibujo generico
(rectangulo redondeado + nombre centrado, sin forma distintiva) tanto en el
icono de paleta como en el lienzo. `src/editor/MsiShapes.hpp` (nuevo,
mismo patron que `GateShapes.hpp`) define las formas compartidas entre
`ui::IconFactory` y `editor::ComponentItem`:

- Multiplexor/Codificador de prioridad: trapecio "muchos a uno" (ancho del
  lado de entradas, angosto del lado de salida). Decodificador/
  Demultiplexor: el trapecio espejado, "uno a muchos".
- Sumador/Restador/Comparador: rectangulo + un glifo vectorial chico
  ("+"/"-"/"=").
- Flip-Flop D/JK/Registro: rectangulo + muesca triangular de reloj (solo
  los disparados por flanco - el Latch SR, de nivel, no la tiene).
- Subcircuito: rectangulo con doble contorno (documento embebido).

Ademas, `io.hexDisplay`/`io.ledMatrix`/`io.terminal` (Fase B) pasaron de la
caja generica a un dibujo real en el lienzo que aprovecha sus propios
decodificadores ya existentes (`hexDisplayState`/`ledMatrixCellIsLit`/
`terminalCharacter`): el display hexadecimal reutiliza literalmente el
mismo dibujo de digito de 7 segmentos que `io.seven_segment` (factorizado
en `drawSevenSegmentDigit()`/`computeSevenSegmentDigitRect()`), la matriz
LED dibuja una grilla real de circulos coloreados en vivo, y la terminal
muestra el caracter ASCII decodificado del byte actual.

**Fuera de alcance, senalado explicitamente**: ningun bloque MSI
(Plexers/Aritmetica/Memoria/Subcircuitos) muestra el nombre de sus pines en
el lienzo todavia - el `width_` por defecto (32px) no alcanza para texto
legible en componentes de hasta 130 pines (p. ej. el sumador de 64 bits);
requeriria repensar el sizing de `ComponentItem::rebuildPins()`, un cambio
aparte.

## Driver BCD y color configurable (E/S)

- `ic74ls.bcdDriver` (nuevo, primer componente de `ComponentCategory::Ic74LS`,
  hasta ahora sin usar): decodifica un valor BCD de 4 bits (`bit0..bit3`,
  0-9) a las 7 salidas `a..g` de un display de 7 segmentos - para cablearse
  a un `io.seven_segment` real en vez de depender de `io.hexDisplay` (que
  decodifica y muestra en un unico componente). Sintetizado con logica real
  (`Kind::Driver`, no `Sink`): un "detector de digito" AND de 4 entradas por
  cada valor 0-9 (reutilizando `negateEach()`/`selectTermsForCombination()`,
  igual patron que Plexers), mas un OR por segmento sobre los digitos donde
  `hexDigitSegments()` dice que ese segmento debe encenderse. Las
  combinaciones 10-15 (BCD invalido) no encienden ningun segmento - ningun
  digito 0-9 las cubre.
  Propiedad `variant` (enum `7447`/`7448`, default `7447`): el 7447 real
  tiene salidas activas en bajo (para un display de anodo comun - One =
  segmento apagado); el 7448 las tiene activas en alto (catodo comun - One
  = encendido, la convencion de "encendido" que ya usa el resto del
  proyecto). Para 7447 se sintetiza un NOT extra por segmento sobre el OR
  compartido, mismo patron que `gates.tristateInverter`.
- `io.hexDisplay`/`io.ledMatrix` ganaron la misma propiedad `color`
  (enum red/green/yellow/blue/white) que ya tenia `io.led`, factorizada en
  `makeColorOptionProperty()` - antes el verde/rojo de los segmentos/celdas
  encendidas estaba fijo, sin poder elegirlo como en el LED simple.
- `ic74ls.bcdDriver` gano su propio dibujo en el lienzo/icono de paleta
  (`paintIc74ls()`/rama nueva en `IconFactory`): cuerpo oscuro tipo
  plastico de IC DIP real (color de fondo por defecto "#2B2B2B", via la
  misma propiedad `bodyColor` de siempre - sigue siendo personalizable),
  muesca semicircular concava de "pin 1" (un recorte real, no un bulto -
  se "borra" el tramo del borde con un chord del color de fondo antes de
  dibujar el arco), y el nombre de cada pin (`bit0..bit3` a la izquierda,
  `a..g` a la derecha) - a diferencia de Plexers/Aritmetica/Memoria/
  Subcircuitos (ver la nota de alcance de mas arriba), ic74ls.* si muestra
  etiquetas de pin: gana su propio ancho (90px, no el generico 32px) y su
  propio pitch vertical entre pines (16px, el doble del generico 8px) para
  que hasta 7 etiquetas por lado (`a..g`) no queden amontonadas ni se
  superpongan entre si.

## Area de interaccion de los componentes (hit-testing)

`ComponentItem` no sobreescribia `shape()`, asi que Qt usaba por defecto un
`QPainterPath` derivado de `boundingRect()` para hit-testing (seleccion por
clic, `CircuitScene::items()`/`tryToggleInput()`) - esa caja incluye la
franja de la etiqueta de instancia debajo del cuerpo y el margen del stub
de pin (`io.seven_segment`/`io.hexDisplay`), asi que un clic ahi
(visualmente "afuera" del componente) igual contaba como un clic *sobre*
el componente - el bug reportado: conmutaba una entrada de un clic
estando fuera de su contorno visible durante la simulacion.
`ComponentItem::shape()` ahora se acota al cuerpo real (`0,0,width_,height_`)
- no es el contorno exacto de cada forma (el "D" de AND, el trapecio de un
mux, etc.), pero excluye los dos casos obvios de "afuera".

`ic74ls.*` tambien se achico/agrando varias veces mas a pedido explicito
(72px -> 64px -> 54px -> 49px -> 52px de ancho; alto +3px, +5px, +3px).
Las columnas de etiquetas de pin dejaron de compartir un ancho fijo comun:
ahora se miden de verdad con `QFontMetricsF` por columna (`bit0..bit3`
necesita bastante mas lugar que `a..g`, una sola letra), liberando el hueco
del medio para el numero de parte real completo (`7447`/`7448`, segun
la propiedad `variant`) - asi se puede ver a simple vista que variante es
un `ic74ls.bcdDriver` sin abrir el inspector. El texto completo (6
caracteres) no entra horizontal en un hueco tan angosto, asi que se dibuja
rotado 90 grados (aprovechando el alto del cuerpo en vez del ancho),
centrado tanto en el hueco (horizontal) como en el cuerpo (vertical). Solo
se dibuja si el hueco/alto disponibles alcanzan para el texto (no en una
instancia angostada al extremo via `customWidth`).

## Paneles del lado derecho ocultos por defecto

Propiedades/Tabla de verdad/Analizador de senales (los tres tabificados en
el mismo grupo, ver `setupDocks()`) ocupaban todo el lado derecho desde el
arranque aunque la mayoria de las sesiones no los usa de inmediato. Ahora
arrancan ocultos (`QDockWidget::hide()`); un unico `QAction` nuevo ("Panel
derecho", checkable) agregado tanto al menu Ver como a la barra de
herramientas principal (mismo objeto en los dos lugares, Qt comparte el
estado marcado) los muestra/oculta en conjunto.
`MainWindow::onComponentContextMenuRequested()` (clic derecho ->
"Propiedades") sigue mostrando el panel puntualmente aunque el toggle
general este apagado - en ese caso el boton puede quedar visualmente
"sin marcar" con el panel igual visible; es una inconsistencia menor, no
resuelta.

## Indicador de anodo/catodo comun en io.seven_segment

La propiedad `commonAnode` solo se veia en el inspector de propiedades, sin
ninguna marca en el lienzo. `paintSevenSegment()` ahora dibuja un
indicador chico en la esquina superior derecha (la inferior derecha ya
tiene el punto decimal): "A" para anodo comun, "K" para catodo comun (K en
vez de C, misma convencion que evita confundirlo con capacitancia). Solo
`io.seven_segment` tiene esta propiedad - `io.hexDisplay` no (decodifica
internamente, sin una etapa de wiring real cuya polaridad importe - ver la
nota de Fase B), asi que no aplica ahi.

## Desplazamiento con Ctrl/Shift + rueda del mouse

`CircuitView::wheelEvent()` solo hacia zoom con la rueda sola. Ahora,
siguiendo el mismo esquema que Proteus: Shift+rueda desplaza horizontal,
Ctrl+rueda desplaza vertical, rueda sola sigue haciendo zoom sin cambios.
(El paneo con el boton central del mouse arrastrando, ya existente, sigue
funcionando igual).

## Doble clic ya no conmuta una entrada fuera de simulacion

`CircuitScene::mouseDoubleClickEvent()` llamaba a `tryToggleInput()`
incondicionalmente, sin importar `isLiveSimulation()` (a diferencia del
clic simple, que ya estaba bien acotado a modo simulacion). Aunque
`setInputValue()` solo propaga a estable en modo en vivo, el propio
`wiring.input` igual cambiaba de color de inmediato con un doble clic en
edicion - logica de simulacion aplicandose fuera de simulacion, sin
corresponder a ninguna accion de edicion real. Ahora el doble clic
tambien exige `isLiveSimulation()`.

## Los valores default/de propiedad ahora se aplican de inmediato

Bug real, no acotado a un solo tipo: `CircuitDocument::setProperty()`
solo reconstruia la simulacion (`rebuildSimulation()`) si la propiedad
cambiaba la *cantidad de pines* del componente - ignoraba por completo el
flag `PropertyDescriptor::affectsSimulation` (que hasta ahora solo se
usaba para ordenar el formulario en `PropertyInspector`, nunca para decidir
si reconstruir). Cualquier propiedad marcada `affectsSimulation = true`
que no altere pines (`initialValue` de `wiring.input`, `invertMask`,
`variant` del driver BCD, `activeHigh`, `commonAnode`, `propagationDelay`,
etc.) quedaba guardada en la propiedad pero el `core::Simulator` en
ejecucion seguia con el valor viejo hasta el proximo rebuild "de
casualidad" (otro cambio que si alterara pines en cualquier componente) o
hasta Reiniciar/recargar el proyecto - el bug reportado ("los valores
default no se estan aplicando": cambiar el valor inicial de una entrada en
edicion no se reflejaba, y tampoco al correr la simulacion despues).

Ahora `setProperty()` tambien reconstruye si `PropertyDescriptor::affectsSimulation`
es true, sin importar si cambiaron los pines. Es seguro: `setProperty()`
solo es alcanzable en pausa (`PropertyInspector` exige `requireEditable()`
antes de llamarlo), asi que reconstruir ahi es tan seguro como pulsar
Reiniciar. Test de regresion en `tests_qt/test_circuit_document.cpp`.

## Guardado estilo Visual Studio: carpeta por proyecto + nombres unicos + .dfc

Antes, "Nuevo proyecto" no pedia ubicacion (quedaba en memoria hasta el
primer Guardar/Guardar como), y cada documento nunca guardado se escribia
como `<nombre>.dfcirc` junto al `.dfproj`, sin una carpeta propia del
proyecto ni validacion de nombres duplicados - dos documentos con el mismo
nombre se pisaban el archivo en disco en silencio al guardar.

- **Extension**: los documentos de circuito pasan de `.dfcirc` a `.dfc`
  (`Project::saveToFile`, filtros de dialogo en `MainWindow`/`ProjectTree`,
  comentarios en `ProjectManifestSerializer.hpp`). Los `.dfcirc` ya
  commiteados en la raiz (demos y scratch) se renombraron con `git mv` y
  los `.dfproj` que los referencian se actualizaron. `.dfproj` en si no
  cambia de extension.
- **Carpeta por proyecto (estilo Visual Studio)**: `MainWindow::promptForNewProjectPath()`
  (nuevo) reutiliza el mismo dialogo de "Guardar como" de siempre, pero
  interpreta el nombre elegido como nombre de carpeta - crea
  `<ubicacion>/<nombre>/` y devuelve `<ubicacion>/<nombre>/<nombre>.dfproj`
  como ruta final. `onNewProject()` lo pide *antes* de tocar nada (si se
  cancela, el proyecto actual queda intacto); `onSaveAs()` usa el mismo
  helper. El proyecto anonimo inicial que crea el arranque de la app no se
  ve afectado - sigue en memoria hasta el primer Guardar.
- **Nombres de documento unicos**: `Project::addDocument`/`renameDocument`/`importDocument`
  ahora rechazan (`std::invalid_argument`) un nombre ya usado por otro
  documento del mismo proyecto, sin distinguir mayusculas/minusculas
  (`Project::hasDocumentNamed()`, nuevo). `ui::ProjectTree::addNewDocument()`/
  `renameDocument()` ganaron el mismo `try/catch` -> `QMessageBox::warning`
  que ya tenian import/export.
- **Un documento ya no puede "escapar" de la carpeta del proyecto**:
  `importDocument()` antes caia de vuelta a referenciar el archivo externo
  en su ubicacion original si la copia hacia la carpeta del proyecto
  fallaba; ahora lanza un error en ese caso (solo seguia permitiendo la
  referencia externa mientras el proyecto nunca se hubiera guardado,
  situacion que con el nuevo flujo de "Nuevo proyecto" practicamente no
  ocurre).

Tests nuevos en `tests_qt/test_project.cpp`: rechazo de nombre duplicado
en `addDocument`/`renameDocument`/`importDocument` (incluye el caso
sin distinguir mayusculas, y que renombrar un documento a su propio nombre
actual no cuenta como colision).

## El .dfproj ya no se reescribe en cada Guardar sin necesidad

`Project::saveToFile()` reescribia el manifiesto (.dfproj) completo en
*cada* llamada, sin importar si la lista de documentos habia cambiado
desde el guardado anterior. Ahora solo se reescribe si
`manifestDirty_` es true (agregar/quitar/renombrar/importar un documento
desde el ultimo guardado) o si `path` es una ubicacion distinta de la
actual (primer guardado o "Guardar como" - ahi si hace falta, para que el
manifiesto exista en la carpeta destino). Cada documento (`.dfc`) en
cambio se sigue reescribiendo completo en todo guardado, a proposito: son
"los importantes" (deben contener el circuito completo y actualizado) y
no hay una senal de "sucio" confiable para saltearlos sin riesgo -
`QUndoStack::isClean()` solo refleja ediciones hechas via `QUndoCommand`,
no las que mutan `CircuitDocument` directamente (se intento esa
optimizacion y rompio un test existente que hace justamente eso - ver el
commit).

Test de regresion en `tests_qt/test_project.cpp`: borra el `.dfproj` a
mano entre dos guardados sin cambios de lista de documentos y confirma
que no reaparece (no se reescribio); agregar un documento si dispara que
reaparezca en el siguiente guardado.

## "Guardar todo" y pestanas de documento estilo Visual Studio

- **Guardar todo**: nueva accion en el menu Archivo (Ctrl+Shift+S), mismo
  comportamiento que "Guardar" (que ya guarda todos los documentos del
  proyecto de una) - se agrega aparte solo por claridad/paridad con Visual
  Studio, ambas llaman a `MainWindow::onSave()`.
- **Pestanas de documento**: `MainWindow::documentTabBar_` (`QTabBar`,
  nuevo) arriba del lienzo (`setupCentralWidgets()` ahora envuelve
  `view_` en un contenedor con `QVBoxLayout`). Cada documento abierto
  tiene una pestana (id guardado via `QTabBar::setTabData()`); clickear
  una pestana activa ese documento (`onDocumentTabChanged` ->
  `Project::setActiveDocument`), y activar un documento sin pestana
  todavia (p. ej. seleccionado desde `ui::ProjectTree` estando "cerrado")
  le crea una (`addDocumentTab()`). **Cerrar una pestana no saca el
  documento del proyecto** (eso lo sigue haciendo unicamente
  `ui::ProjectTree`) - solo deja de mostrarlo, igual que cerrar un
  archivo abierto en Visual Studio sin sacarlo de la solucion; siempre
  queda al menos una pestana abierta (`onDocumentTabCloseRequested`
  ignora el pedido si es la unica).

## Corregir "Agregar documento nuevo" con el nombre por defecto

Bug real, no una limitacion de diseno: el dialogo de `ui::ProjectTree::addNewDocument()`
sugeria siempre el mismo nombre por defecto literal, "Documento" - pero
`Project::newProject()` ya crea el primer documento de *cualquier*
proyecto nuevo con exactamente ese mismo nombre. Aceptar la sugerencia tal
cual (el flujo mas obvio: abrir el dialogo, apretar Aceptar) chocaba de
entrada contra la validacion de nombres unicos agregada en el commit
anterior, dando la falsa impresion de que el proyecto no admitia mas de un
documento.

`Project::suggestUniqueDocumentName(base)` (nuevo, publico): devuelve
`base` tal cual si nadie lo usa, o `base` + "2", "3", etc. hasta encontrar
uno libre. `ProjectTree::addNewDocument()` ahora lo usa para calcular el
nombre sugerido en vez del literal fijo. `Project::hasDocumentNamed()`
paso de privado a publico (ya existia desde el commit anterior).

Test de regresion en `tests_qt/test_project.cpp`: sugiere "Documento2" (no
"Documento") para un proyecto recien creado, y "Documento3" con ambos ya
ocupados.

## Diferenciar "Nuevo proyecto" de "Nuevo documento"

"Nuevo" en el menu Archivo era una sola accion ambigua (creaba un
*proyecto* entero) con el icono de una hoja suelta - el mismo tipo de
icono que uno esperaria para "documento", sin ninguna distincion visual
entre ambos conceptos, y sin ninguna forma de agregar un documento desde
el menu Archivo (solo existia via el menu contextual de
`ui::ProjectTree`).

- `icons::newProject()` (nuevo): el mismo contorno de carpeta que ya usaba
  `icons::open()` (un proyecto es una carpeta), con una insignia "+" verde
  - visualmente distinto de `icons::newDocument()` (una hoja suelta, sin
    cambios), que ahora es exclusivo de "Nuevo documento".
- Menu Archivo: "Nuevo proyecto..." (Ctrl+Shift+N) y "Nuevo documento..."
  (Ctrl+N) como dos acciones separadas - mismos atajos que Visual Studio.
  "Nuevo documento" llama a `ui::ProjectTree::addNewDocument()` (ahora
  publico), la misma operacion que ya ofrecia el menu contextual del
  arbol de proyecto.

## Panel de Propiedades: solo automatico en edicion

Antes, seleccionar cualquier componente (incluido el clic simple que ya
usa `tryToggleInput()` para conmutar una entrada mientras la simulacion
esta en ejecucion) actualizaba el panel de Propiedades automaticamente sin
importar el modo - molesto durante la simulacion, ya que la mayoria de las
propiedades no se pueden editar en ese estado (`requireEditable()` las
bloquea) y el panel saltando con cada clic era mas distractivo que util.

- `MainWindow::onSceneSelectionChanged()` ahora se corta temprano si
  `isLiveSimulation()` es true - la seleccion visual (resaltado azul en el
  lienzo) sigue funcionando igual, solo se dejo de sincronizar
  automaticamente con el inspector.
- Nueva via explicita para verlas igual durante la simulacion: clic derecho
  sobre un componente -> "Propiedades" (`CircuitScene::componentContextMenuRequested`,
  nueva senal; `MainWindow::onComponentContextMenuRequested` arma el menu y,
  al confirmar, selecciona ese componente en el inspector y trae al frente
  la pestana "Propiedades").

## Familia 74xx completa

`ComponentCategory::Ic74LS` tenia un unico componente (`ic74ls.bcdDriver`).
Se agregaron los 13 que faltaban, a pedido explicito del usuario (3 lotes:
quad-gate clasicas, flip-flops/contador, plexers/aritmetica en formato IC).
Cero cambios en `editor::`/`ui::`: `ComponentItem::paintIc74ls()`/
`IconFactory`/`ComponentPalette` ya eran 100% genericos por prefijo de
typeId (`ic74ls.`) y por categoria, asi que los 13 heredan el dibujo de IC
DIP oscuro y el agrupado de paleta sin ningun codigo nuevo fuera de
`components::`. Todos comparten `bodyColor` default `"#2B2B2B"` (plastico
de IC real, igual que `bcdDriver`) y el orden de pines "todas las entradas
primero, despues todas las salidas" que ya usa el resto del proyecto.
`makeDFlipFlopDefinition()`/`makeJkFlipFlopDefinition()` se factorizaron a
`addDFlipFlopStage()`/`addJkFlipFlopStage()` (funciones libres) para que
7474/7476 puedan reusar exactamente la misma logica dos veces (dos
biestables independientes por paquete) sin duplicarla.

| Componente | typeId | Notas |
|---|---|---|
| 7400 | `ic74ls.quadNand2` | 4 NAND de 2 entradas independientes. |
| 7402 | `ic74ls.quadNor2` | 4 NOR de 2 entradas independientes. |
| 7404 | `ic74ls.hexInverter` | 6 inversores independientes. |
| 7408 | `ic74ls.quadAnd2` | 4 AND de 2 entradas independientes. |
| 7432 | `ic74ls.quadOr2` | 4 OR de 2 entradas independientes. |
| 7486 | `ic74ls.quadXor2` | 4 XOR de 2 entradas independientes. |
| 7474 | `ic74ls.dualDFlipFlop` | Dos FF D independientes. Sin PRE/CLR (mismo recorte que `memory.dFlipFlop`). |
| 7476 | `ic74ls.dualJkFlipFlop` | Dos FF JK independientes. Sin PRE/CLR (mismo recorte que `memory.jkFlipFlop`). |
| 7490 | `ic74ls.decadeCounter` | Contador BCD **sincronico** (0-9, envuelve a 0): `D0=NOT(Q0)`, `D1=Q1 XOR (Q0 AND NOT Q3)`, `D2=Q2 XOR (Q0 AND Q1)`, `D3=Q3 XOR ((Q0 AND Q1 AND Q2) OR (Q0 AND Q3))`, ecuaciones verificadas contra el ciclo 0-9 completo tanto a mano como con un test dedicado. `CLR` es sincronico (`D AND NOT(CLR)`, se aplica en el proximo flanco) - no replica las etapas divide-por-2/divide-por-5 cascadeables ni los pines R0(1)/R0(2)/R9(1)/R9(2) del 7490 real (el primitivo `GateType::DFlipFlop` no tiene un tercer pin de clear). |
| 74151 | `ic74ls.mux8to1` | Mux de 8 lineas + `Strobe` activo en bajo (fuerza `Y=0`/`W=1` si esta en alto) + salida complementaria `W`. |
| 74138 | `ic74ls.decoder3to8` | Decoder de 8 salidas activas en bajo, habilitado por `G1` (alto) y `G2A`/`G2B` (bajo) - deshabilitado, todas las salidas quedan en alto. |
| 7483 | `ic74ls.adder4bit` | `arithmetic.adder` con `bits` fijo en 4 (sin la propiedad). |
| 7485 | `ic74ls.comparator4bit` | `arithmetic.comparator` (bits=4) mas cascada real (`CasGT`/`CasEQ`/`CasLT`, ausente en `arithmetic.comparator`): si los 4 bits son iguales, el resultado final lo deciden las entradas de cascada en vez de la comparacion local. Uso individual (sin cascada real): cablear `CasEQ` a una `wiring.constant` en 1 y `CasGT`/`CasLT` en 0 - un pin de cascada sin cablear queda flotante, igual que en el datasheet real. |

Tests nuevos en `tests/test_components.cpp`: un `runCombinationalIc()`
generico (cablea N entradas + M salidas de una sola vez, mismo patron que
ya usaba `runBcdDriver()`) para los 9 componentes puramente combinacionales,
mas tests dedicados con flancos sucesivos de CLK para 7474/7476/7490 (este
ultimo el caso mas importante: 11 flancos completos, incluida la limpieza
sincronica y el envolver 9->0, validando empiricamente las ecuaciones de
arriba en vez de solo a mano). El test de "registra exactamente el
conjunto actual" (`registeredTypeIds().size()`) subio de 32 a 45.

## Arquitectura

- `src/components/Property.hpp`: `PropertyValue` (variant) + `PropertyDescriptor` con validación de tipo/rango/opciones.
- `src/components/ComponentDefinition.hpp`: descripción inmutable y compartida de un tipo de componente (pines derivados de propiedades, función de construcción sobre `core::Circuit`; mas el par opcional deriveExternalPins/buildExternalSimulation de Fase 4).
- `src/components/ExternalDocumentView.hpp`: interfaz de bajo acoplamiento entre `components::` y `editor::` para subcircuitos (ver Fase 4).
- `src/components/ComponentInstance.{hpp,cpp}`: valor concreto por instancia (propiedades, pines, redes enlazadas, resultado de simulación). No es un `QObject`; no hay herencia polimórfica.
- `src/components/ComponentRegistry.{hpp,cpp}`: catálogo por `typeId` estable (nunca por nombre visible).
- `src/components/BasicComponentLibrary.{hpp,cpp}`: registra los 53 componentes actuales (12 de FASE B1 + Reloj + Plexers + Fase 2 de aritmética + Fase 3 de memoria + Fase 4 de subcircuitos + Fase A de negación/triestado + Fase B de E/S restante + driver BCD + los 13 de la familia 74xx completa + Resistencia pull-up/pull-down, Reinicio al encender, No conectar, Túnel, Tierra lógica, Transistor y Compuerta de transmisión).
- `src/core/GateType.hpp`/`Gate.hpp`/`Simulator.cpp`: `DFlipFlop` sigue siendo el unico tipo de gate con estado; `TriStateBuffer` (Fase A) es combinacional puro pese a ser un tipo nuevo. `WeakZero`/`WeakOne` (resistencia pull-up/pull-down): `Simulator::applyGateOutput()` los resuelve en un segundo nivel, aparte de los drivers normales (`isWeakType()`) - solo toman una red si ningun driver normal la impulsa activamente, y nunca generan Error contra uno normal. `core::` no sabe nada de subcircuitos - el aplanado ocurre entero en `editor::`.
- Serialización: `nlohmann::json` (agregado vía FetchContent en `cmake/Json.cmake`). Se serializan únicamente `typeId + instanceId + propiedades`; nunca índices de red o de gate (dependientes de memoria/build). `targetPath` de un subcircuito se serializa como cualquier otra propiedad String.

## Pruebas

`tests/test_components.cpp` (55 casos): funcionamiento normal, propagación de
X y Z, conflicto/Error entre múltiples fuentes, LED activo alto/bajo,
serialización y deserialización (incluye rechazo de `typeId` no coincidente),
propiedades inválidas (rango, tipo incorrecto, id desconocido), recálculo de
pines al cambiar una propiedad, conexión/desconexión de pines, un circuito
Entrada→AND→LED de extremo a extremo verificando estabilidad del simulador,
9 casos dedicados al display de 7 segmentos, 6 casos de Plexers, 5 casos de
aritmética, 4 casos de memoria (latch SR set/reset/hold, flip-flop D con
flancos ascendente/descendente y cambios de D sin flanco, flip-flop JK
recorriendo la tabla set/reset/hold/toggle, y un registro de 3 bits
cargando simultaneamente en un solo flanco), 1 caso de subcircuito sin
contexto externo (0 pines, no tira al simular), 3 casos de Fase A (buffer
triestado, inversor triestado, invertMask sobre gates.and), y 8 casos de
Fase B (decodificacion hexadecimal, matriz LED con tamaño configurable,
terminal ASCII, todos incluyendo el caso de entrada ambigua X/Z).

`tests/test_simulator.cpp` suma 2 casos sobre el primitivo crudo
`GateType::DFlipFlop` (flanco limpio vs. D-only vs. flanco descendente, y el
caso ambiguo de un primer flanco sin CLK=0 previo confirmado), mas 2 casos
sobre el primitivo crudo `GateType::TriStateBuffer` (EN habilitado/deshabilitado,
y conflicto Error entre dos buffers triestado habilitados con datos distintos),
mas 2 casos sobre `GateType::WeakZero`/`WeakOne` (un driver debil solo toma
una red sin nada mas manejandola, y pierde limpio - sin Error - contra
cualquier driver normal; dos debiles opuestos sin nada normal si generan
Error, igual criterio que dos fuertes).

`tests/test_components.cpp` agrega 5 casos para wiring.pullResistor/
powerOnReset/doNotConnect/tunnel/ground (forma de pines y comportamiento de
`buildSimulation()`, mismo patron que el test de wiring.clock), mas 2 casos
para wiring.transistor (NMOS conduce con Gate=1/PMOS con Gate=0, bloqueado
al reves) y wiring.transmissionGate (conduce solo con N=1,P=0; bloqueada en
las 3 combinaciones restantes), y sube el conteo de
`registeredTypeIds().size()` de 46 a 53.
`tests_qt/test_circuit_document.cpp` agrega un caso de wiring.powerOnReset
(QTimer de un solo disparo, no se rearma tras pausar/reanudar despues de
disparar) y uno de wiring.tunnel (conecta por etiqueta compartida sin
cable, se desconecta al cambiar la etiqueta).

`tests_qt/test_subcircuit.cpp` (4 casos): pines derivados y
ordenados por posicion + simulacion aplanada de punta a punta, rechazo de
auto-referencia y de anidamiento de mas de un nivel, y un round-trip
completo de guardado/carga de un proyecto con un subcircuito ya cableado
(el caso que expuso el bug de orden de carga arreglado en
`Project::loadFromFile`).

`tests_qt/test_truth_table.cpp` (5 casos): tabla completa de un AND
de 2 entradas, restauracion del valor original de una entrada despues del
barrido, encabezados de columna (label vs. fallback IN{id}/OUT{id}), y
rechazo tanto por falta de entradas/salidas como por exceder el limite de
entradas configurado.

`tests_qt/test_waveform_recorder.cpp` (nuevo, 6 casos): captura inmediata
del valor actual al agregar una red vigilada, rechazo de duplicados y del
limite de `kMaxWatchedNets`, compresion "solo agrega muestra si el valor
cambio", `simulationRebuilt()` limpiando muestras sin perder la lista de
redes vigiladas, `removeWatch`/`clearWatches`, y `setDocument()`
descartando las redes del documento anterior.

`tests/test_components.cpp` suma ademas 4 casos de `ic74ls.bcdDriver`
(digitos 0/1/9 decodificados al patron estandar de 7 segmentos en 7448,
default 7447 activo en bajo invertido respecto de 7448, y rechazo/apagado
de las combinaciones 10-15 invalidas).

`tests_qt/test_project.cpp` suma 4 casos de unicidad de nombre de
documento (`addDocument`/`renameDocument`/`importDocument` rechazando un
nombre duplicado, incluido sin distinguir mayusculas) y
`tests_qt/test_circuit_document.cpp` 1 caso de regresion (ver la nota de
"los valores default no se aplicaban" mas arriba).

Total del proyecto: **suite headless (`digitalforge_tests.exe`) 108 casos /
633 aserciones, suite Qt (`digitalforge_qt_tests.exe`) 43 casos / 171
aserciones** - todo pasa, cero warnings con
`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.

# Registro de cambios

Todas las versiones publicables de DigitalForge se listan aquí. El formato
sigue, a grandes rasgos, [Keep a Changelog](https://keepachangelog.com/es/),
y el versionado es [semántico](https://semver.org/lang/es/).

## v0.1.5 — pre-alpha

### Editor gráfico (Qt6)
- **Propiedades como diálogo modal**: el panel de Propiedades deja de ser un
  dock anclado y pasa a abrirse a demanda (doble clic sobre un componente, o
  "Propiedades" del menú contextual) en un diálogo modal.
- Atajo `Ctrl+R` para rotar el componente seleccionado (el atajo de
  "Reiniciar simulación" pasa a `Ctrl+Shift+R` para no chocar).
- **Fix**: las derivaciones sobre un cable existente ya no quedan
  desalineadas de la grilla de fondo.
- **Fix**: un punto de unión recién soltado en el vacío (grado 1) ya se
  dibuja y puede volver a agarrarse/arrastrarse — antes quedaba invisible e
  inencontrable.
- **Resaltado de nodo completo**: seleccionar un cable o un punto de unión
  ilumina automáticamente todo el nodo eléctrico (todos los cables/uniones
  que comparten esa misma señal).
- **Panel "Errores y advertencias" (DRC)**: lista pines obligatorios sin
  conectar, conflictos de manejadores (incluye cortocircuitos entre fuentes
  fijas) y oscilación detectada; un clic en cada fila selecciona los
  pines/uniones implicados en el lienzo.

### Tablas de excitación de flip-flops
- Nuevo tipo de documento "Tabla de excitación": registra estado actual y
  siguiente por bit, con el tipo de flip-flop (SR, JK, T, D) seleccionable
  por bit. Las columnas de estado actual y estado siguiente son editables
  por igual (clic para ciclar 0/1/X).
- "Generar mapas": produce los mapas de Karnaugh de excitación
  correspondientes al tipo de flip-flop elegido.
- "Generar circuito con flip-flops": sintetiza el circuito secuencial
  completo a partir de la tabla de excitación, con los flip-flops ya
  colocados y cableados (D, T, JK).

## v0.1.4 — pre-alpha

### Correcciones
- **v0.1.3 tampoco alcanzaba**: exigir version y huella coincidentes antes de
  restaurar el layout de paneles no evito el mismo crash "aparece el splash
  y se cierra" en un caso real (mismo instalador, version y huella
  identicas). La causa exacta de por que restaurar un "MainWindow/state"
  puntual a veces deja corrupto el auto-hide de paneles no se pudo aislar
  con certeza; en vez de seguir intentando adivinar cuando es seguro
  hacerlo, se elimino la unica operacion que alguna vez crasheo: ya no se
  auto-oculta ningun panel a partir de una sesion anterior (si, tenias
  paneles colapsados, arrancan pineados y podes volver a ocultarlos con un
  click). El tamano/posicion de ventana y el resto de la disposicion de
  docks/toolbars se siguen restaurando con normalidad.

## v0.1.3 — pre-alpha

### Correcciones
- **v0.1.2 no era suficiente**: la huella de docks/toolbars por si sola no
  alcanzaba para detectar un `MainWindow/state` guardado por una instalacion
  anterior (versiones distintas pueden compartir el mismo conjunto de
  paneles). Ahora se exige tambien que la version que guardo el estado
  coincida con la actual; si no, se usa la disposicion de fabrica.
- Instalador de Windows: limpia por completo la carpeta de una instalacion
  anterior antes de copiar los archivos nuevos, para no dejar restos de
  versiones previas (DLLs o plugins descontinuados) conviviendo con la
  instalacion actual.

## v0.1.2 — pre-alpha

### Correcciones
- **Crash al reabrir la app** ("aparece el splash y se cierra"): un
  `MainWindow/state` guardado por una versión anterior (con otro conjunto de
  paneles/barras) dejaba corrupto el layout interno de Qt al restaurar los
  paneles auto-ocultos de la sesión previa. Ahora se guarda una huella de la
  disposición de paneles/barras junto con el estado, y se descarta el estado
  guardado (usando la disposición de fábrica) si no coincide con la actual.

## v0.1.1 — pre-alpha

### Editor gráfico (Qt6)
- **Enrutamiento automático de cables con obstáculos**: el trazado ortogonal
  ahora detecta componentes y otros cables en el camino y los rodea, en vez de
  cruzarlos en línea recta.
- Edición interactiva de cables ampliada: arrastre de segmentos y uniones,
  desplazamiento de los cables junto con la selección múltiple, y
  sincronización de componentes y cables durante el arrastre.
- Pines de los componentes centrados de forma uniforme.
- Corrección: el modo de tema "Sistema" ahora sigue en vivo los cambios de
  tema claro/oscuro de Windows en vez de quedar congelado en el que estaba
  activo al arrancar.
- Gestión de estilos de la aplicación simplificada: se usa directamente el
  estilo nativo/Fusion estándar de Qt.

## v0.1.0-alpha — pre-lanzamiento

Primer pre-lanzamiento público (alpha). Editor y simulador de lógica digital
multiplataforma, con binarios descargables para Windows y Linux.

### Núcleo de simulación
- Simulador dirigido por eventos con lógica de cinco estados (`0/1/Z/X/Error`).
- Detección de oscilaciones y de conflictos entre múltiples salidas.
- Retardos de propagación configurables por compuerta.

### Editor gráfico (Qt6)
- Colocación, cableado ortogonal con ruteo automático, uniones y subcircuitos.
- Deshacer/rehacer, tabla de verdad, registro de formas de onda, minimapa.
- Zoom, selector de tema (sistema/claro/oscuro) e iconos nítidos.
- **Edición de cables por esquinas**: arrastrar una esquina la mueve
  manteniendo los ángulos rectos, sin deformar el trazado.

### Componentes
- Biblioteca integrada de ~40 tipos (compuertas, plexores, aritmética,
  memoria, E/S, cableado).
- **Carga de componentes desde JSON** (Fase 3): tipos nuevos definidos por
  netlist de primitivas, sin recompilar (ver `docs/component-format.md`).

### Persistencia y compatibilidad
- Proyectos en JSON (`.dfproj` / `.dfc`); importador de circuitos de Logisim.
- Huellas de componentes (SHA-256), archivo de bloqueo y reporte de
  compatibilidad al abrir.

### Distribución
- Instalador de Windows (Inno Setup) y AppImage de Linux.
- **Aviso de actualizaciones in-app**: la aplicación comprueba en los releases
  de GitHub si hay una versión más nueva y lo informa.

### Limitaciones conocidas
- Buses de múltiples bits aún no implementados (cada red es de 1 bit).
- La carga de componentes JSON usa pines/comportamiento fijos y dibujo genérico.
- El lenguaje DFML, su compilador (DFMC) y los paquetes externos (DFLIB) aún
  no existen.

# Registro de cambios

Todas las versiones publicables de DigitalForge se listan aquí. El formato
sigue, a grandes rasgos, [Keep a Changelog](https://keepachangelog.com/es/),
y el versionado es [semántico](https://semver.org/lang/es/).

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

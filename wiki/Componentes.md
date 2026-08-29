# Componentes

DigitalForge incluye más de cincuenta tipos de componentes agrupados por
categoría.

## Categorías principales

- **Compuertas:** AND, OR, NOT, NAND, NOR, XOR y XNOR.
- **Tristado:** buffer e inversor con salida de alta impedancia.
- **Plexores:** multiplexores, decodificadores y codificadores de prioridad.
- **Aritmética:** sumador, restador y comparador.
- **Memoria:** latch SR, flip-flops D y JK, y registro.
- **Cableado:** entrada, salida, reloj, constante, túnel, tierra, resistencias
  pull-up/pull-down, transistor y compuerta de transmisión.
- **E/S y depuración:** LED, display hexadecimal, matriz LED, terminal y sonda.
- **Estructura:** subcircuitos.
- **Familia 74LS:** 7400, 7402, 7404, 7408, 7432, 7486, 7474, 7476, 7490,
  74138, 74151, 7483, 7485 y controlador BCD.

## Componentes personalizados en JSON

Se pueden añadir componentes sin recompilar la aplicación mediante archivos
JSON. Cada definición declara:

- identificador y nombre;
- categoría, versión y descripción;
- propiedades;
- pines;
- una netlist de primitivas;
- conexiones entre pines externos y nodos internos.

Los archivos `*.json` se cargan desde la carpeta `components/` situada junto al
ejecutable o desde la ruta indicada por `DIGITALFORGE_COMPONENTS_DIR`. Un
archivo inválido se reporta sin impedir que se carguen los demás.

Consulta la
[especificación completa del formato](https://github.com/yetto-tools/DigitalForge/blob/main/docs/component-format.md)
y los
[ejemplos](https://github.com/yetto-tools/DigitalForge/tree/main/components).

Este formato todavía no es DFML: describe netlists fijas y no dispone de un
compilador ni de paquetes DFLIB.


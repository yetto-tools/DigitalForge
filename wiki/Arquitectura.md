# Arquitectura

## Núcleo

`src/core/` implementa la topología y la simulación sin depender de Qt.
`Circuit` almacena compuertas y redes mediante vectores e índices compactos.
`Simulator` procesa una cola de eventos con marcas de tiempo y mantiene el
estado de ejecución.

## Componentes

`src/components/` separa la definición de un tipo de componente de cada
instancia colocada:

- `ComponentDefinition`: contrato inmutable, propiedades, pines y construcción
  de la simulación.
- `ComponentInstance`: valores concretos, pines derivados y redes enlazadas.
- `ComponentRegistry`: catálogo dinámico por `typeId`.
- `BasicComponentLibrary`: biblioteca integrada.
- `JsonComponentLoader`: componentes declarados mediante JSON.

## Editor

`src/editor/` adapta los componentes al modelo de documentos y al lienzo Qt:

- `CircuitDocument`: circuito editable y pila de deshacer.
- `Project`: manifiesto y conjunto de documentos.
- `CircuitScene` y `CircuitView`: escena gráfica y visualización.
- herramientas de selección, colocación y cableado;
- comandos de deshacer y rehacer.

## Interfaz

`src/ui/` contiene la paleta, el árbol de proyecto, el inspector de propiedades,
el minimapa, las herramientas de simulación, la tabla de verdad y el panel de
formas de onda. `src/app/` integra estos elementos en la ventana principal.

## Formatos

`src/formats/` serializa circuitos y manifiestos, importa Logisim y gestiona
versiones, huellas y archivos de bloqueo.

## Subcircuitos

Los subcircuitos se aplanan antes de simular. Una interfaz mínima permite que
el sistema de componentes consulte documentos externos sin crear una
dependencia circular con el editor. El núcleo no conoce el concepto de
subcircuito.


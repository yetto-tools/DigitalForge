# Formatos y compatibilidad

## Archivos

| Extensión o nombre | Propósito |
|---|---|
| `.dfproj` | Manifiesto de un proyecto con varios documentos |
| `.dfc` | Documento de circuito |
| `digitalforge.lock.json` | Versiones y huellas de los componentes usados |
| `.json` en `components/` | Definición declarativa de un componente |

Los proyectos y circuitos se almacenan en JSON.

## Protección frente a cambios

Cada instancia conserva su `typeId`, versión de definición y huellas de
compatibilidad. Las conexiones también guardan una clave estable de pin. Esto
permite:

- distinguir cambios visuales de cambios de simulación o interfaz;
- reconectar un pin aunque haya cambiado de posición;
- evitar que un pin eliminado se reconecte por error a otro pin;
- comparar el archivo de bloqueo con los componentes instalados.

La carga no elimina un componente por una incompatibilidad. Los cables que no
pueden resolverse se reportan y el resto del trabajo se conserva.

## Limitaciones actuales

- La interfaz todavía no muestra todo el reporte de compatibilidad.
- Los campos JSON desconocidos no críticos se pierden al volver a guardar.
- No existen aún migraciones declarativas, alias de tipos ni propiedades
  obsoletas.
- DFML, DFMC y las bibliotecas DFLIB todavía no están implementados.

El seguimiento técnico detallado está en
[docs/dfml-metadata-status.md](https://github.com/yetto-tools/DigitalForge/blob/main/docs/dfml-metadata-status.md).


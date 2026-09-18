#ifndef SEQCHECKER_BUILDTREE_H
#define SEQCHECKER_BUILDTREE_H

#include <QString>

/**
 * ¿La app corre desde un ARBOL DE BUILD o desde una instalacion?
 *
 * De la respuesta depende donde se buscan `python_runtime/`, `py_scr/`, `thirdparty/` y la
 * config, y donde se escriben logs y temporales: en el arbol de build cuelgan del PADRE del
 * ejecutable (la raiz del repo); en una instalacion, de la carpeta del ejecutable, que es lo
 * unico que el desinstalador se lleva.
 *
 * Es la UNICA funcion que decide esto. AppPathManager y DebugFlags le preguntan a ella; ningun
 * otro archivo repite la heuristica (../LGA_Base_QT_C_Py/docs/Doc_Rutas_Instalacion.md, "Una sola
 * funcion"). Copia de LGA_Base_QT_C_Py/include/lga_base_qt_c_py/BuildTree.h: los arreglos se
 * hacen alla y se copian.
 *
 * 🔴 Antes se decidia con `appDir.contains("build")`: el NOMBRE de la ruta, no el contenido. La
 * instalacion por defecto es `C:\Portable\LGA\<App>`, y un producto que se llame SceneBuilder, o
 * una instalacion en `C:\Trabajo\Rebuild\App` o en `D:\builds\App`, se daba por arbol de build y
 * resolvia todo una carpeta mas arriba, afuera de `{app}`.
 *
 * Windows (criterio canonico, el de `LgaBuildTree` de LGA_MediaTools_v2):
 *  1. `LGA_BUILD_TREE` en el entorno fuerza la respuesta. Solo valores explicitos:
 *     `1`/`true`/`yes`/`on` y `0`/`false`/`no`/`off`. Cualquier otra cosa se ignora con un
 *     aviso, para que `LGA_BUILD_TREE=no` no signifique "si".
 *  2. Si no hay override, hacen falta LAS DOS cosas:
 *     - un `CMakeCache.txt` en la carpeta del ejecutable o hasta DOS niveles arriba (CMake lo
 *       deja en todo arbol de build y jamas viaja a una instalacion), y
 *     - que el padre de esa carpeta tenga `CMakeLists.txt` y `py_scr/`: la forma de la raiz del
 *       repo.
 *     Solo el `CMakeCache.txt` no alcanza: una instalacion colgada abajo de un arbol de build
 *     ajeno pasaria por build. Solo la forma del repo tampoco: el staging `deploy/` (que tiene
 *     `py_scr/` copiado) pasaria por build.
 *  3. Si no, instalacion. Es el fallback barato: escribir de mas adentro de `{app}` deja una
 *     carpeta que el desinstalador se lleva; escribir afuera deja archivos huerfanos.
 *
 * macOS y Linux: se conserva el predicado viejo por nombre, a proposito. En un bundle el
 * ejecutable vive en `build/App.app/Contents/MacOS/`, TRES niveles abajo del `CMakeCache.txt`, y
 * el tope de dos niveles no llega; portarlo sin un Mac para verificarlo no se hace.
 * Pendiente de verificar en un Mac.
 *
 * Funcion MUDA: la llama el constructor de DebugFlags, asi que no puede usar CONDITIONAL_DEBUG
 * (volveria a entrar al singleton mientras se construye). `qWarning` si: el handler de main.cpp
 * escribe a archivo sin pasar por DebugFlags.
 */
namespace LgaBuildTree {

/// `appDir` es la carpeta del ejecutable. El resultado se cachea por ruta.
bool isBuildTree(const QString &appDir);

/// Repite el aviso de un `LGA_BUILD_TREE` invalido (no cambia ninguna respuesta). main() la
/// llama justo despues de instalar su handler de mensajes: la primera consulta sale de la
/// resolucion de la carpeta de logs, ANTES del handler, y ese primer aviso no llega al log.
/// Fuera de Windows no hace nada (ahi no hay override).
void warnIfInvalidOverride();

/// Solo para tests: vacia el cache de respuestas.
void clearCache();

} // namespace LgaBuildTree

#endif // SEQCHECKER_BUILDTREE_H

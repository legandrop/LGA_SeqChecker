#ifndef SEQCHECKER_RUNMODE_H
#define SEQCHECKER_RUNMODE_H

#include <QString>

/**
 * ¿Esta corrida puede PERSISTIR config del usuario?
 *
 * Es la UNICA funcion que lo decide. Todo punto que escribe config (geometria de ventana,
 * AppSettings, SecureConfig y su clave, el registro compartido de LGA) le pregunta a
 * `canPersistConfig()` antes de tocar el disco; ningun otro archivo repite la condicion.
 *
 * 🔴 Por que existe: una corrida sin pantalla (QT_QPA_PLATFORM=offscreen) o en un modo de captura
 * reescribia la config REAL del usuario. Medido en las apps derivadas: `saveWindowSettings()`
 * guardaba la geometria en `%APPDATA%\LGA\<App>\` aun en offscreen, recortada a 800x800 (la
 * pantalla falsa de ese plugin), y la app abria asi la vez siguiente. Mostrar o cerrar la
 * ventana alcanza para dispararlo: no hace falta que nadie toque un control.
 *
 * No persiste cuando:
 *  - la corrida es un modo de QA o de captura (`setCaptureMode(true)`, lo llama main() apenas
 *    reconoce el argumento: `--ui-shot`, `--tooltip-shot`, `--ui-probe`), sea cual sea la plataforma; o
 *  - la plataforma de Qt no tiene pantalla real (`offscreen`, `minimal`).
 * En esos casos los setters siguen funcionando EN MEMORIA: la UI se comporta igual durante la
 * corrida y nada llega al disco.
 *
 * Excepcion: con `QStandardPaths::setTestModeEnabled(true)` si persiste. Ese modo redirige
 * AppDataLocation a una carpeta de pruebas, asi que la config real no se toca y un test puede
 * comprobar que algo se guarda.
 *
 * 🔴 Limite de esa excepcion: solo es segura para lo que se escribe en una ruta de
 * `QStandardPaths`. El modo de test NO redirige la carpeta de instalacion, el registro de
 * Windows (la clave `Run`, asociaciones de archivos) ni nada que viva en otra app. Un punto que
 * escribe ahi le pregunta a `canPersistSystemState()`, que no tiene la excepcion: en un test
 * tambien contesta "no" si la corrida es de captura o sin pantalla.
 *
 * No es silencioso: cuando la corrida no persiste, `logIfNotPersisting()` deja UNA linea en el
 * log con el motivo. Sin esa linea, "la app no guardo mis cambios" no se puede diagnosticar.
 *
 * Los LOGS no son config y no pasan por aca: una corrida sin pantalla que no deja log no se
 * puede diagnosticar.
 *
 * Copia de LGA_Base_QT_C_Py/include/lga_base_qt_c_py/RunMode.h: los arreglos se hacen alla y se
 * copian. Depende solo de QtCore y QtGui.
 */
namespace LgaRunMode {

/// Marca la corrida como modo de QA/captura. Se llama una vez, al principio de main().
void setCaptureMode(bool on);
bool isCaptureMode();

/// La plataforma de Qt no dibuja en una pantalla real. Con la QGuiApplication ya construida
/// responde por `platformName()`; antes, por la variable QT_QPA_PLATFORM.
bool isHeadlessPlatform();

/// LA pregunta. `false` = no escribir config en disco en esta corrida.
bool canPersistConfig();

/// La misma pregunta para lo que NO vive en una ruta de `QStandardPaths` (carpeta de
/// instalacion, registro de Windows, estado de otra app). Sin la excepcion del modo de test.
bool canPersistSystemState();

/// Por que esta corrida no persiste, en una frase para el log. Vacio si persiste.
QString nonPersistReason();

/// Deja UNA linea `qInfo` con el motivo si la corrida no persiste; si persiste no escribe nada.
/// main() la llama una vez, despues de instalar su handler de mensajes. Las llamadas siguientes
/// no repiten la linea.
void logIfNotPersisting();

} // namespace LgaRunMode

#endif // SEQCHECKER_RUNMODE_H

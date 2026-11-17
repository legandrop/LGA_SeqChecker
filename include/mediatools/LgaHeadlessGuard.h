#ifndef SEQCHECKER_HEADLESSGUARD_H
#define SEQCHECKER_HEADLESSGUARD_H

#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <cstdio>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

/**
 * Chequeo del plugin de plataforma ANTES de construir QApplication.
 *
 * 🔴 Por que existe: si a una app Qt se le pide una plataforma (`QT_QPA_PLATFORM=offscreen`, que
 * es como corre toda prueba sin pantalla) y el plugin no esta (`platforms/qoffscreen.dll`), Qt NO
 * falla por consola: abre un cartel MODAL ("no Qt platform plugin could be initialized") en el
 * escritorio y el proceso queda vivo hasta que alguien hace clic. Ese cartel sale de adentro del
 * constructor de QGuiApplication, asi que el unico lugar donde se lo puede evitar es antes.
 *
 * Uso, primera linea de main():
 *
 *     int main(int argc, char *argv[])
 *     {
 *         if (const int guard = LgaHeadlessGuard::check(argc, argv)) {
 *             return guard;            // mensaje por stderr, sin cartel
 *         }
 *         QApplication app(argc, argv);
 *
 * Cuando actua y cuando no:
 *  - Solo si ALGUIEN PIDIO una plataforma: `-platform <nombre>` en los argumentos o la variable
 *    `QT_QPA_PLATFORM`. Ahi, si el plugin no aparece en ninguna de las carpetas donde Qt lo
 *    buscaria, escribe el motivo por stderr y devuelve `kExitCode` (3).
 *  - Sin plataforma pedida (el arranque normal del usuario) NO decide nada y devuelve 0: si a una
 *    instalacion le falta `qwindows.dll`, el cartel de Qt es lo que le tiene que avisar al
 *    usuario, y un chequeo propio que se equivoque dejaria a la app sin arrancar en silencio.
 *  - Con un `qt.conf` junto al exe tampoco decide (ese archivo cambia donde busca Qt y antes de
 *    QCoreApplication no se puede leer igual que Qt): devuelve 0.
 *  - SOLO EN WINDOWS. En macOS y Linux `check()` devuelve siempre 0: ahi no hay cartel modal que
 *    evitar, y en un bundle de macOS los plugins viven en `Contents/PlugIns`, no junto al
 *    ejecutable, asi que la busqueda daria un falso "falta".
 *
 * Busca en el mismo orden que Qt: `-platformpluginpath`, `QT_QPA_PLATFORM_PLUGIN_PATH`,
 * `<carpeta del exe>/platforms`, `<plugins de la instalacion de Qt>/platforms` y cada entrada de
 * `QT_PLUGIN_PATH` + `/platforms`.
 *
 * Header-only y solo QtCore: lo incluye la app y lo incluye un arnes, sin sumar fuentes al build.
 * Se copia tal cual a cada app derivada. No reemplaza al preflight de `tools/qa/run_headless.ps1`
 * (que ademas mira DLL y funciones importadas): lo cubre para cuando el exe se lanza sin runner.
 */
namespace LgaHeadlessGuard {

constexpr int kExitCode = 3;

/// Plataformas pedidas, en orden, sin parametros (`offscreen:size=...` -> `offscreen`). Vacia si
/// nadie pidio ninguna. `-platform` gana sobre la variable, como en Qt.
inline QStringList requestedPlatforms(int argc, char **argv)
{
    QString raw;
    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], "-platform") == 0 || qstrcmp(argv[i], "--platform") == 0) {
            raw = QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    if (raw.isEmpty()) {
        raw = qEnvironmentVariable("QT_QPA_PLATFORM");
    }
    QStringList names;
    const QStringList parts = raw.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString name = part.section(QLatin1Char(':'), 0, 0).trimmed().toLower();
        if (!name.isEmpty() && !names.contains(name)) {
            names.append(name);
        }
    }
    return names;
}

inline QString pluginFileName(const QString &platform)
{
#if defined(Q_OS_WIN)
    return QStringLiteral("q%1.dll").arg(platform);
#elif defined(Q_OS_MAC)
    return QStringLiteral("libq%1.dylib").arg(platform);
#else
    return QStringLiteral("libq%1.so").arg(platform);
#endif
}

/// Carpeta del ejecutable, sin depender de QCoreApplication (todavia no existe).
inline QString executableDir(char **argv)
{
#ifdef Q_OS_WIN
    Q_UNUSED(argv);
    wchar_t buffer[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, DWORD(sizeof(buffer) / sizeof(buffer[0])));
    if (n > 0 && n < DWORD(sizeof(buffer) / sizeof(buffer[0]))) {
        return QFileInfo(QString::fromWCharArray(buffer, int(n))).absolutePath();
    }
    return QString();
#else
    return argv && argv[0] ? QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath() : QString();
#endif
}

/// Las carpetas `platforms` donde Qt buscaria el plugin, en su orden.
inline QStringList platformDirs(int argc, char **argv)
{
    QStringList dirs;
    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], "-platformpluginpath") == 0) {
            dirs.append(QString::fromLocal8Bit(argv[i + 1]));
        }
    }
    const QString direct = qEnvironmentVariable("QT_QPA_PLATFORM_PLUGIN_PATH");
    if (!direct.isEmpty()) {
        dirs.append(direct);
    }
    const QString exeDir = executableDir(argv);
    if (!exeDir.isEmpty()) {
        dirs.append(QDir(exeDir).filePath(QStringLiteral("platforms")));
    }
    const QString qtPlugins = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    if (!qtPlugins.isEmpty()) {
        dirs.append(QDir(qtPlugins).filePath(QStringLiteral("platforms")));
    }
    const QStringList extra = qEnvironmentVariable("QT_PLUGIN_PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const QString &dir : extra) {
        dirs.append(QDir(dir).filePath(QStringLiteral("platforms")));
    }
    return dirs;
}

/// Ruta del plugin de `platform` en la primera carpeta de `dirs` que lo tenga, o vacio.
inline QString findPlatformPlugin(const QString &platform, const QStringList &dirs)
{
    const QString file = pluginFileName(platform);
    for (const QString &dir : dirs) {
        const QString candidate = QDir(dir).filePath(file);
        if (QFileInfo(candidate).isFile()) {
            return QDir::toNativeSeparators(candidate);
        }
    }
    return QString();
}

/// La decision, separada de argv y del entorno para poder probarla: 0 = seguir; `kExitCode` =
/// se pidio una plataforma y su plugin no esta en ninguna de `dirs` (el motivo va a `err`).
inline int checkPlatforms(const QStringList &platforms, const QStringList &dirs, FILE *err = stderr)
{
    if (platforms.isEmpty()) {
        return 0;
    }
    for (const QString &platform : platforms) {
        if (!findPlatformPlugin(platform, dirs).isEmpty()) {
            return 0;   // Qt prueba las plataformas en orden: con una que cargue alcanza
        }
    }
    if (err) {
        std::fprintf(err,
                     "[HeadlessGuard] Se pidio la plataforma de Qt '%s' y falta su plugin (%s). "
                     "No se construye QApplication: Qt abriria un cartel modal. Buscado en: %s\n",
                     qPrintable(platforms.join(QLatin1Char(';'))),
                     qPrintable(pluginFileName(platforms.first())),
                     qPrintable(QDir::toNativeSeparators(dirs.join(QStringLiteral(" | ")))));
        std::fflush(err);
    }
    return kExitCode;
}

/// La llamada de main(): 0 = seguir y construir QApplication; otro valor = salir con ese codigo.
inline int check(int argc, char **argv, FILE *err = stderr)
{
#ifndef Q_OS_WIN
    // Fuera de Windows no decide nada. El cartel modal que este guard evita es de Windows (el
    // MessageBox de init_platform()); en macOS y Linux Qt falla por consola. Y la busqueda de
    // arriba no vale para un bundle de macOS: macdeployqt deja los plugins en
    // `Contents/PlugIns` y el `qt.conf` en `Contents/Resources`, no junto al ejecutable
    // (`Contents/MacOS`), asi que una plataforma pedida que SI esta en el bundle daria un falso
    // "falta" y la app saldria con 3. Las demas funciones quedan para quien las quiera usar.
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    Q_UNUSED(err);
    return 0;
#else
    const QStringList platforms = requestedPlatforms(argc, argv);
    if (platforms.isEmpty()) {
        return 0;
    }
    const QString exeDir = executableDir(argv);
    if (!exeDir.isEmpty() && QFileInfo::exists(QDir(exeDir).filePath(QStringLiteral("qt.conf")))) {
        return 0;
    }
    return checkPlatforms(platforms, platformDirs(argc, argv), err);
#endif
}

} // namespace LgaHeadlessGuard

#endif // SEQCHECKER_HEADLESSGUARD_H

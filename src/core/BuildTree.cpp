#include "mediatools/BuildTree.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>

// Copia de LGA_Base_QT_C_Py/src/utils/BuildTree.cpp (los arreglos se hacen alla y se copian).
// Criterio y comentarios en BuildTree.h. La rama de Windows es la de `LgaBuildTree` de
// LGA_MediaTools_v2 (libs/lgacommon/src/BuildTree.cpp); si se corrige alla, se corrige aca.

namespace {

QMutex &cacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QString, bool> &cache()
{
    static QHash<QString, bool> map;
    return map;
}

#ifdef Q_OS_WIN

// Cuantos niveles se sube buscando el `CMakeCache.txt`. El ejecutable vive en `build/` y, como
// mucho, un nivel mas abajo. Dos es el maximo que hace falta y el minimo que no convierte esto
// en un ascenso hasta la raiz del disco: con un ascenso largo, una instalacion colgada varios
// niveles abajo de CUALQUIER arbol de build ajeno se daba por arbol de build.
constexpr int kMaxLevelsUp = 2;

// Override explicito. Devuelve 1 o 0, o -1 si no hay override valido.
int envOverride()
{
    const QString raw =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("LGA_BUILD_TREE")).trimmed();
    if (raw.isEmpty()) {
        return -1;
    }
    static const QStringList kTrue{QStringLiteral("1"), QStringLiteral("true"),
                                   QStringLiteral("yes"), QStringLiteral("on")};
    static const QStringList kFalse{QStringLiteral("0"), QStringLiteral("false"),
                                    QStringLiteral("no"), QStringLiteral("off")};
    const QString value = raw.toLower();
    if (kTrue.contains(value)) {
        return 1;
    }
    if (kFalse.contains(value)) {
        return 0;
    }
    qWarning().noquote() << QString("[BuildTree] LGA_BUILD_TREE=%1 no es un valor valido "
                                    "(1/0, true/false, yes/no, on/off); se ignora")
                                .arg(raw);
    return -1;
}

// La carpeta del arbol de build que contiene a `appDir`, o vacio si no hay ninguna a mano.
QString findBuildRoot(const QString &appDir)
{
    QDir dir(appDir);
    for (int level = 0; level <= kMaxLevelsUp; ++level) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("CMakeCache.txt")))) {
            return dir.absolutePath();
        }
        if (dir.isRoot() || !dir.cdUp()) {
            return QString();
        }
    }
    return QString();
}

// Forma de la raiz del repo: el `CMakeLists.txt` y la carpeta de scripts conviven ahi. Un
// `deploy/` no tiene `CMakeLists.txt`, y ninguna instalacion tiene las dos cosas.
bool looksLikeSourceRoot(const QDir &dir)
{
    return QFileInfo::exists(dir.filePath(QStringLiteral("CMakeLists.txt")))
        && QFileInfo(dir.filePath(QStringLiteral("py_scr"))).isDir();
}

bool evaluate(const QString &appDir)
{
    const int forced = envOverride();
    if (forced >= 0) {
        return forced == 1;
    }
    // Hacen falta LAS DOS cosas: un `CMakeCache.txt` cerca Y que arriba de el este la raiz
    // del repo.
    const QString buildRoot = findBuildRoot(appDir);
    if (buildRoot.isEmpty()) {
        return false;
    }
    QDir parent(buildRoot);
    return parent.cdUp() && looksLikeSourceRoot(parent);
}

#else

// macOS y Linux: el predicado de siempre, sin cambios (ver BuildTree.h).
bool evaluate(const QString &appDir)
{
    return appDir.contains(QStringLiteral("build"), Qt::CaseInsensitive);
}

#endif

} // namespace

namespace LgaBuildTree {

bool isBuildTree(const QString &appDir)
{
    if (appDir.trimmed().isEmpty()) {
        return false;
    }
    const QString key = QDir::cleanPath(QDir::fromNativeSeparators(appDir));
    {
        QMutexLocker locker(&cacheMutex());
        const auto it = cache().constFind(key);
        if (it != cache().constEnd()) {
            return *it;
        }
    }

    const bool result = evaluate(key);

    QMutexLocker locker(&cacheMutex());
    cache().insert(key, result);
    return result;
}

void warnIfInvalidOverride()
{
#ifdef Q_OS_WIN
    envOverride();   // avisa por qWarning si el valor no es valido
#endif
}

void clearCache()
{
    QMutexLocker locker(&cacheMutex());
    cache().clear();
}

} // namespace LgaBuildTree

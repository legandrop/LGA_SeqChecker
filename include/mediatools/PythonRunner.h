#ifndef PYTHONRUNNER_H
#define PYTHONRUNNER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>

/**
 * @brief Wrapper de QProcess para ejecutar scripts Python.
 *
 * Lanza el python.exe embebido con el script especificado,
 * captura stdout/stderr y emite señales con el resultado.
 */
class PythonRunner : public QObject
{
    Q_OBJECT
public:
    explicit PythonRunner(QObject *parent = nullptr);
    ~PythonRunner() override;

    // Setea la ruta al ejecutable Python (de AppPathManager)
    void setPythonPath(const QString &pythonExe);

    // Ejecuta un script con argumentos dados
    // scriptPath: ruta absoluta al .py
    // args: lista de argumentos (sin el script path)
    void run(const QString &scriptPath, const QStringList &args = {});

    // Cancela el proceso si está corriendo
    void cancel();

    bool isRunning() const;

signals:
    // Emitido cuando hay output en stdout (línea a línea)
    void outputLine(const QString &line);

    // Emitido cuando hay output en stderr
    void errorLine(const QString &line);

    // Emitido cuando el proceso termina
    void finished(int exitCode, const QString &stdoutData, const QString &stderrData);

    // Emitido cuando hay un error al iniciar
    void errorOccurred(const QString &errorMessage);

private slots:
    void onReadyReadStdOut();
    void onReadyReadStdErr();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onErrorOccurred(QProcess::ProcessError error);

private:
    QProcess   *m_process;
    QString     m_pythonExe;
    QString     m_stdoutAccum;
    QString     m_stderrAccum;
};

#endif // PYTHONRUNNER_H

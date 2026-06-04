#include "mediatools/PythonRunner.h"
#include "mediatools/debug_flags.h"

#include <QDebug>

PythonRunner::PythonRunner(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    connect(m_process, &QProcess::readyReadStandardOutput, this, &PythonRunner::onReadyReadStdOut);
    connect(m_process, &QProcess::readyReadStandardError,  this, &PythonRunner::onReadyReadStdErr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PythonRunner::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &PythonRunner::onErrorOccurred);
}

PythonRunner::~PythonRunner()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

void PythonRunner::setPythonPath(const QString &pythonExe)
{
    m_pythonExe = pythonExe;
}

void PythonRunner::run(const QString &scriptPath, const QStringList &args)
{
    if (m_process->state() != QProcess::NotRunning) {
        CONDITIONAL_DEBUG("python_runner", "[PythonRunner::run] Ya hay un proceso en ejecución");
        emit errorOccurred("Ya hay un proceso Python en ejecución");
        return;
    }

    m_stdoutAccum.clear();
    m_stderrAccum.clear();

    QStringList allArgs;
    allArgs << scriptPath;
    allArgs << args;

    CONDITIONAL_DEBUG("python_runner", "[PythonRunner::run] Lanzando:" << m_pythonExe << allArgs.join(" "));

    m_process->setProgram(m_pythonExe);
    m_process->setArguments(allArgs);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->start();

    if (!m_process->waitForStarted(5000)) {
        QString errMsg = QString("No se pudo iniciar Python: %1").arg(m_process->errorString());
        CONDITIONAL_DEBUG("python_runner", "[PythonRunner::run] ERROR:" << errMsg);
        emit errorOccurred(errMsg);
    }
}

void PythonRunner::cancel()
{
    if (m_process->state() != QProcess::NotRunning) {
        CONDITIONAL_DEBUG("python_runner", "[PythonRunner::cancel] Cancelando proceso Python...");
        m_process->kill();
    }
}

bool PythonRunner::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

void PythonRunner::onReadyReadStdOut()
{
    while (m_process->canReadLine()) {
        QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        if (!line.isEmpty()) {
            m_stdoutAccum += line + "\n";
            CONDITIONAL_DEBUG("python_runner_verbose", "[PythonRunner] STDOUT:" << line);
            emit outputLine(line);
        }
    }
}

void PythonRunner::onReadyReadStdErr()
{
    while (m_process->canReadLine()) {
        QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        if (!line.isEmpty()) {
            m_stderrAccum += line + "\n";
            CONDITIONAL_DEBUG("python_runner", "[PythonRunner] STDERR:" << line);
            emit errorLine(line);
        }
    }
}

void PythonRunner::onFinished(int exitCode, QProcess::ExitStatus status)
{
    // Vaciar buffers
    QByteArray remaining = m_process->readAllStandardOutput();
    if (!remaining.isEmpty()) {
        m_stdoutAccum += QString::fromUtf8(remaining);
    }
    QByteArray remainingErr = m_process->readAllStandardError();
    if (!remainingErr.isEmpty()) {
        m_stderrAccum += QString::fromUtf8(remainingErr);
    }

    CONDITIONAL_DEBUG("python_runner", QString("[PythonRunner] Proceso terminado. ExitCode=%1 Status=%2")
                       .arg(exitCode).arg(status));

    emit finished(exitCode, m_stdoutAccum, m_stderrAccum);  // stdoutData, stderrData
}

void PythonRunner::onErrorOccurred(QProcess::ProcessError error)
{
    QString errMsg = QString("Error de QProcess (%1): %2").arg(error).arg(m_process->errorString());
    CONDITIONAL_DEBUG("python_runner", "[PythonRunner] ERROR:" << errMsg);
    emit errorOccurred(errMsg);
}

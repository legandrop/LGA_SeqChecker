#include "seqchecker/mainwindow.h"
#include "mediatools/debug_flags.h"
#include "mediatools/AppPathManager.h"
#include "mediatools/PythonRunner.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QStandardPaths>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDesktopServices>
#include <QColor>

namespace {
// Columnas de la tabla de resultados.
enum Col {
    ColSequence = 0,
    ColFolder,
    ColFrames,
    ColRange,
    ColOk,
    ColSuspect,
    ColCorrupt,
    ColStatus,
    ColCount
};

QColor statusColor(const QString &status)
{
    if (status == "ok")      return QColor(COL_STATUS_OK);
    if (status == "suspect") return QColor(COL_STATUS_SUSPECT);
    if (status == "corrupt" || status == "error") return QColor(COL_STATUS_CORRUPT);
    return QColor(COL_STATUS_PENDING);
}

QString statusLabelText(const QString &status)
{
    if (status == "ok")      return "OK";
    if (status == "suspect") return "Suspect";
    if (status == "corrupt") return "CORRUPT";
    if (status == "error")   return "Error";
    return "Analyzing…";
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("LGA SeqChecker");
    resize(1100, 640);
    setAcceptDrops(true);

    const QString appPath = QCoreApplication::applicationDirPath();
    m_pythonExe  = AppPathManager::getPythonRuntimePath(appPath);
    m_scriptPath = AppPathManager::getPythonScriptPath(appPath, "LGA_SeqChecker.py");
    CONDITIONAL_DEBUG("core", "[MainWindow] python:" << m_pythonExe << "script:" << m_scriptPath);

    m_runner = new PythonRunner(this);
    m_runner->setPythonPath(m_pythonExe);
    connect(m_runner, &PythonRunner::outputLine, this, &MainWindow::onPythonLine);
    connect(m_runner, &PythonRunner::errorLine,  this, &MainWindow::onPythonError);
    connect(m_runner, &PythonRunner::finished,   this, &MainWindow::onPythonFinished);
    connect(m_runner, &PythonRunner::errorOccurred, this, [this](const QString &msg) {
        setStatusText("Error: " + msg);
        m_analysisRunning = false;
    });

    m_geometrySaveTimer = new QTimer(this);
    m_geometrySaveTimer->setSingleShot(true);
    m_geometrySaveTimer->setInterval(400);
    connect(m_geometrySaveTimer, &QTimer::timeout, this, &MainWindow::saveWindowSettings);

    buildUi();
    loadStyleSheet();
    createDropOverlay();
    loadWindowSettings();
}

void MainWindow::buildUi()
{
    QWidget *central = new QWidget(this);
    central->setObjectName("centralRoot");
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    m_statusLabel = new QLabel("Drop a folder anywhere to scan for corrupt EXR sequences.", central);
    m_statusLabel->setObjectName("statusLabel");
    root->addWidget(m_statusLabel);

    m_table = new QTableWidget(0, ColCount, central);
    m_table->setObjectName("seqTable");
    QStringList headers;
    headers << "Sequence" << "Folder" << "Frames" << "Range"
            << "OK" << "Suspect" << "Corrupt" << "Status";
    m_table->setHorizontalHeaderLabels(headers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(false);

    QHeaderView *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(ColSequence, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColFolder, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColFrames, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColRange, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColOk, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColSuspect, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColCorrupt, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColStatus, QHeaderView::Fixed);
    m_table->setColumnWidth(ColFrames, 70);
    m_table->setColumnWidth(ColRange, 140);
    m_table->setColumnWidth(ColOk, 60);
    m_table->setColumnWidth(ColSuspect, 70);
    m_table->setColumnWidth(ColCorrupt, 75);
    m_table->setColumnWidth(ColStatus, 110);

    // Doble-click revela la carpeta de la secuencia.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        QTableWidgetItem *it = m_table->item(row, ColFolder);
        if (!it) return;
        const QString folder = it->data(Qt::UserRole).toString();
        if (!folder.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        }
    });

    root->addWidget(m_table, 1);
    setCentralWidget(central);
}

void MainWindow::loadStyleSheet()
{
    QString qss;
    for (const QString &res : {QStringLiteral(":/styles/tabs.qss"),
                               QStringLiteral(":/styles/dark_theme.qss")}) {
        QFile f(res);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qss += QString::fromUtf8(f.readAll()) + "\n";
            f.close();
        }
    }

    // Estilos propios de SeqChecker (no dependen de tabs de PipeSync).
    qss += QString(
        "QWidget#centralRoot { background-color: %1; }"
        "QLabel#statusLabel { color: %2; font-family: 'Inter'; font-size: 14px; padding: 2px 2px; }"
        "QTableWidget#seqTable {"
        "  background-color: %3; border: none; border-radius: 6px;"
        "  color: %2; font-family: 'Inter'; font-size: 13px;"
        "  gridline-color: transparent;"
        "}"
        "QTableWidget#seqTable::item { padding: 4px 8px; border: none; }"
        "QTableWidget#seqTable::item:selected { background-color: %4; color: #d0d0d0; }"
        "QHeaderView::section {"
        "  background-color: #191919; color: #8f8f8f; border: none;"
        "  border-right: 1px solid %5; padding: 6px 8px; font-weight: 600;"
        "}"
        "QScrollBar:vertical { background: %3; width: 12px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #3a3a3a; border-radius: 6px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ).arg(COL_BG_PRINCIPAL)   // 1
     .arg(COL_TXT_PRINCIPAL)  // 2
     .arg(COL_BG_ITEMS)       // 3
     .arg("#4b4b4b")          // 4
     .arg(COL_BORDER);        // 5

    setStyleSheet(qss);
}

// ----------------------------------------------------------------------------
// Drag & drop overlay (1 zona)
// ----------------------------------------------------------------------------
void MainWindow::createDropOverlay()
{
    if (m_dropOverlay) return;

    m_dropOverlay = new QWidget(this);
    m_dropOverlay->setObjectName("folderDropOverlay");
    m_dropOverlay->setAttribute(Qt::WA_StyledBackground, true);
    m_dropOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_dropOverlay->setStyleSheet(QString(
        "#folderDropOverlay { background-color: %1; border: none; border-radius: %2px; }"
    ).arg(QString::fromUtf8(DROP_OVERLAY_BACKDROP_COLOR)).arg(DROP_OVERLAY_PANE_BORDER_RADIUS));

    auto *overlayLayout = new QVBoxLayout(m_dropOverlay);
    overlayLayout->setContentsMargins(DROP_OVERLAY_OUTER_MARGIN, DROP_OVERLAY_OUTER_MARGIN,
                                      DROP_OVERLAY_OUTER_MARGIN, DROP_OVERLAY_OUTER_MARGIN);

    m_dropOverlayPane = new QWidget(m_dropOverlay);
    m_dropOverlayPane->setObjectName("folderDropPane");
    m_dropOverlayPane->setAttribute(Qt::WA_StyledBackground, true);
    auto *paneLayout = new QVBoxLayout(m_dropOverlayPane);
    paneLayout->setContentsMargins(DROP_OVERLAY_PANE_PADDING, DROP_OVERLAY_PANE_PADDING,
                                   DROP_OVERLAY_PANE_PADDING, DROP_OVERLAY_PANE_PADDING);
    paneLayout->addStretch();
    m_dropOverlayLabel = new QLabel("Drop folder(s) to analyze", m_dropOverlayPane);
    m_dropOverlayLabel->setAlignment(Qt::AlignCenter);
    m_dropOverlayLabel->setWordWrap(true);
    paneLayout->addWidget(m_dropOverlayLabel);
    paneLayout->addStretch();

    overlayLayout->addWidget(m_dropOverlayPane);

    setDropOverlayVisible(false, false);
    m_dropOverlay->setGeometry(rect());
    m_dropOverlay->hide();
}

void MainWindow::setDropOverlayVisible(bool visible, bool active)
{
    if (!m_dropOverlay) return;

    m_dropOverlayPane->setStyleSheet(QString(
        "QWidget#folderDropPane {"
        "  background-color: %1; border: %2px dashed %3; border-radius: %4px;"
        "}"
        "QLabel { border: none; }"
    ).arg(active ? QString::fromUtf8(DROP_OVERLAY_ACTIVE_PANE_BG_COLOR) : QStringLiteral("transparent"))
     .arg(DROP_OVERLAY_PANE_BORDER_WIDTH)
     .arg(QString::fromUtf8(DROP_OVERLAY_PANE_DASH_BORDER_COLOR))
     .arg(DROP_OVERLAY_PANE_BORDER_RADIUS));

    m_dropOverlayLabel->setStyleSheet(QString(
        "font-family: '%1'; font-size: %2px; font-weight: %3; color: %4;"
        "background: transparent; border: none;"
    ).arg(QString::fromUtf8(DROP_OVERLAY_LABEL_FONT_FAMILY))
     .arg(DROP_OVERLAY_LABEL_FONT_PX)
     .arg(DROP_OVERLAY_LABEL_FONT_WEIGHT)
     .arg(active ? QString::fromUtf8(DROP_OVERLAY_LABEL_ACTIVE_COLOR)
                 : QString::fromUtf8(DROP_OVERLAY_LABEL_COLOR)));

    if (visible) {
        m_dropOverlay->setGeometry(rect());
        m_dropOverlay->raise();
        m_dropOverlay->show();
    } else {
        m_dropOverlay->hide();
    }
}

QStringList MainWindow::extractDroppedFolders(const QMimeData *mimeData) const
{
    QStringList folders;
    if (!mimeData || !mimeData->hasUrls()) return folders;
    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile()) continue;
        const QString path = QDir::cleanPath(url.toLocalFile());
        const QFileInfo info(path);
        if (info.exists() && info.isDir() && !folders.contains(path)) {
            folders.append(path);
        }
    }
    return folders;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_analysisRunning) return;
    const QStringList folders = extractDroppedFolders(event->mimeData());
    if (!folders.isEmpty()) {
        setDropOverlayVisible(true, true);
        event->acceptProposedAction();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (m_analysisRunning) return;
    if (!extractDroppedFolders(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *)
{
    setDropOverlayVisible(false, false);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    setDropOverlayVisible(false, false);
    if (m_analysisRunning) return;
    const QStringList folders = extractDroppedFolders(event->mimeData());
    if (folders.isEmpty()) return;
    event->acceptProposedAction();
    startAnalysis(folders);
}

// ----------------------------------------------------------------------------
// Análisis
// ----------------------------------------------------------------------------
void MainWindow::startAnalysis(const QStringList &folders)
{
    if (m_analysisRunning) return;
    if (!QFileInfo::exists(m_pythonExe)) {
        setStatusText("Error: python runtime no encontrado en " + m_pythonExe);
        return;
    }
    if (!QFileInfo::exists(m_scriptPath)) {
        setStatusText("Error: backend no encontrado en " + m_scriptPath);
        return;
    }

    resetResults();
    m_analysisRunning = true;
    setStatusText("Scanning…");

    QStringList args;
    args << "--json-lines";
    for (const QString &f : folders) {
        args << "--folder" << f;
    }
    CONDITIONAL_DEBUG("import", "[startAnalysis] folders:" << folders);
    m_runner->run(m_scriptPath, args);
}

void MainWindow::resetResults()
{
    m_table->setRowCount(0);
    m_seqRowById.clear();
    m_totalFrames = 0;
    m_doneFrames = 0;
    m_corruptTotal = 0;
}

int MainWindow::rowForSeqId(const QString &id, bool createIfMissing)
{
    auto it = m_seqRowById.find(id);
    if (it != m_seqRowById.end()) return it.value();
    if (!createIfMissing) return -1;
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    for (int c = 0; c < ColCount; ++c) {
        m_table->setItem(row, c, new QTableWidgetItem());
    }
    m_seqRowById.insert(id, row);
    return row;
}

void MainWindow::onPythonLine(const QString &line)
{
    if (!line.startsWith("MT_")) {
        CONDITIONAL_DEBUG("python_runner_verbose", "[py]" << line);
        return;
    }
    const int sp = line.indexOf(' ');
    if (sp < 0) return;
    const QString tag = line.left(sp);
    const QByteArray payload = line.mid(sp + 1).toUtf8();
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject o = doc.object();

    if (tag == "MT_SEQ_FOUND") {
        const QString id = o.value("id").toString();
        const int row = rowForSeqId(id, true);
        const QString folder = o.value("folder").toString();
        m_table->item(row, ColSequence)->setText(o.value("name").toString());
        QTableWidgetItem *fit = m_table->item(row, ColFolder);
        fit->setText(folder);
        fit->setData(Qt::UserRole, folder);
        m_table->item(row, ColFrames)->setText(QString::number(o.value("frames").toInt()));
        m_table->item(row, ColRange)->setText(o.value("range").toString());
        QTableWidgetItem *sit = m_table->item(row, ColStatus);
        sit->setText(statusLabelText("pending"));
        sit->setForeground(statusColor("pending"));
        m_totalFrames += o.value("frames").toInt();
    } else if (tag == "MT_PROGRESS") {
        m_doneFrames = o.value("done").toInt();
        if (o.contains("total")) m_totalFrames = o.value("total").toInt();
        setStatusText(QString("Analyzing… %1 / %2 frames — %3 corrupt")
                          .arg(m_doneFrames).arg(m_totalFrames).arg(m_corruptTotal));
    } else if (tag == "MT_SEQ_RESULT") {
        const QString id = o.value("id").toString();
        const int row = rowForSeqId(id, true);
        const int corrupt = o.value("corrupt").toInt();
        m_table->item(row, ColOk)->setText(QString::number(o.value("ok").toInt()));
        m_table->item(row, ColSuspect)->setText(QString::number(o.value("suspect").toInt()));
        m_table->item(row, ColCorrupt)->setText(QString::number(corrupt));
        const QString status = o.value("status").toString();
        QTableWidgetItem *sit = m_table->item(row, ColStatus);
        sit->setText(statusLabelText(status));
        sit->setForeground(statusColor(status));
        m_table->item(row, ColCorrupt)->setForeground(statusColor(corrupt > 0 ? "corrupt" : "ok"));
        // Tooltip con el detalle de frames problemáticos.
        const QString detail = o.value("detail").toString();
        if (!detail.isEmpty()) {
            for (int c = 0; c < ColCount; ++c) m_table->item(row, c)->setToolTip(detail);
        }
        m_corruptTotal += corrupt;
    } else if (tag == "MT_DONE") {
        const int seqs = o.value("sequences").toInt();
        const int corrupt = o.value("corrupt").toInt();
        const int suspect = o.value("suspect").toInt();
        setStatusText(QString("Done — %1 sequences, %2 corrupt frames, %3 suspect.")
                          .arg(seqs).arg(corrupt).arg(suspect));
    }
}

void MainWindow::onPythonError(const QString &line)
{
    CONDITIONAL_DEBUG("python_runner", "[py-stderr]" << line);
}

void MainWindow::onPythonFinished(int exitCode, const QString &, const QString &err)
{
    m_analysisRunning = false;
    if (exitCode != 0) {
        QString tail = err.trimmed();
        if (tail.size() > 300) tail = tail.right(300);
        setStatusText(QString("Analysis failed (exit %1). %2").arg(exitCode).arg(tail));
    }
    CONDITIONAL_DEBUG("core", "[analysis] finished exit" << exitCode);
}

void MainWindow::setStatusText(const QString &text)
{
    if (m_statusLabel) m_statusLabel->setText(text);
}

// ----------------------------------------------------------------------------
// Geometría de ventana
// ----------------------------------------------------------------------------
void MainWindow::scheduleGeometrySave()
{
    if (m_geometrySaveTimer) m_geometrySaveTimer->start();
}

void MainWindow::loadWindowSettings()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!configDir.isEmpty()) QDir().mkpath(configDir);
    const QString configPath = configDir.isEmpty() ? QString() : configDir + "/window_geometry.ini";

    QByteArray geo;
    if (!configPath.isEmpty() && QFile::exists(configPath)) {
        QSettings s(configPath, QSettings::IniFormat);
        geo = s.value("geometry").toByteArray();
    } else {
        QSettings s;
        geo = s.value("geometry").toByteArray();
    }
    if (!geo.isEmpty()) restoreGeometry(geo);
}

void MainWindow::saveWindowSettings()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString configPath = configDir.isEmpty() ? QString() : configDir + "/window_geometry.ini";
    if (!configPath.isEmpty()) {
        QDir().mkpath(configDir);
        QSettings s(configPath, QSettings::IniFormat);
        s.setValue("geometry", saveGeometry());
    } else {
        QSettings s;
        s.setValue("geometry", saveGeometry());
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_dropOverlay && m_dropOverlay->isVisible()) m_dropOverlay->setGeometry(rect());
    scheduleGeometrySave();
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    scheduleGeometrySave();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowSettings();
    if (m_runner && m_runner->isRunning()) m_runner->cancel();
    QMainWindow::closeEvent(event);
}

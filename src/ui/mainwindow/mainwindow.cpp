#include "seqchecker/mainwindow.h"
#include "seqchecker/ArrowComboBox.h"
#include "seqchecker/ShotNameDetector.h"
#include "seqchecker/utils/TableColumnWidthHelper.h"
#include "seqchecker/utils/TableHeaderDividerView.h"
#include "mediatools/debug_flags.h"
#include "mediatools/AppPathManager.h"
#include "mediatools/PythonRunner.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
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
#include <QShowEvent>
#include <QMoveEvent>
#include <QStandardPaths>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDesktopServices>
#include <QColor>
#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QTextStream>
#include <QThread>
#include <QVector>
#include <QSet>
#include <QtMath>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

constexpr int ColRoleShotName = Qt::UserRole + 10;
constexpr int ColRoleShotColor = Qt::UserRole + 11;

struct CpuPreset {
    QString name;
    int workers = 1;
};

static const struct {
    const char* var;
    const char* value;
} COLOR_VARS[] = {
    { "bg_principal", "#161616" },
    { "bg_items", "#1d1d1d" },
    { "bg_tabs", "#101010" },
    { "border_principal", "#303030" },
    { "txt_principal", "#B2B2B2" },
    { "control_section_label_color", "#8f8f8f" },
    { "txt_input", "#7b7b7b" },
    { "txt_input_placeholder", "#555555" },
    { "violeta_oscuro", "#443a91" },
    { "violeta_claro", "#774dcb" },
    { "boton_gris_oscuro", "#2a2a2a" },
    { "boton_gris_oscu_hover", "#3a3a3a" },
    { "table_header_bg", "#191919" },
    { "table_group_bg", "#212121" },
    { "checkbox_bg_on", "#393455" },
    { "checkbox_bg_off", "#2a2832" },
    { "checkbox_bg_on_hover", "#4c4770" },
    { "checkbox_bg_off_hover", "#3a3744" },
    { "ui_small_action_button_text_color", "#9a9a9a" },
    { "ui_small_action_button_text_color_hover", "#d0d0d0" },
    { "ui_small_action_button_font_size", "12px" },
    { "ui_small_action_button_padding_y", "3px" },
    { "ui_small_action_button_padding_x", "10px" },
    { "ui_small_action_button_bg_color", "#2a2a2a" },
    { "ui_small_action_button_bg_color_hover", "#3a3a3a" },
    { "ui_small_action_button_bg_color_disabled", "#232323" },
    { "ui_small_action_button_text_color_disabled", "#666666" },
    { "ui_small_action_button_border_color", "#313131" },
    { "ui_small_action_button_border_radius", "4px" },
    { "ui_table_selection_button_text_color", "#8a8a8a" },
    { "ui_table_selection_button_text_color_hover", "#d0d0d0" },
    { "ui_table_selection_button_font_size", "13px" },
    { "ui_table_selection_button_padding_y", "4px" },
    { "ui_table_selection_button_padding_x", "12px" },
    { "ui_table_selection_button_bg_color", "#1d1d1d" },
    { "ui_table_selection_button_bg_color_hover", "#2a2a2a" },
    { "ui_table_selection_button_bg_color_pressed", "#343434" },
    { "ui_table_selection_button_border_radius", "4px" },
    { "ui_clear_all_tables_button_border_radius", "4px" },
    { "ui_clear_all_tables_button_border_width", "1px" },
    { "ui_clear_all_tables_button_bg_color", "#232323" },
    { "ui_clear_all_tables_button_border_color", "#333333" },
    { "ui_clear_all_tables_button_text_color", "#9a9a9a" },
    { "ui_clear_all_tables_button_bg_color_hover", "#2d2d2d" },
    { "ui_clear_all_tables_button_text_color_hover", "#d0d0d0" },
    { "ui_clear_all_tables_button_bg_color_pressed", "#343434" },
    { "ui_clear_all_tables_button_text_color_pressed", "#d0d0d0" },
    { "ui_clear_all_tables_button_bg_color_disabled", "#1f1f1f" },
    { "ui_clear_all_tables_button_border_color_disabled", "#2b2b2b" },
    { "ui_clear_all_tables_button_text_color_disabled", "#666666" },
    { "ui_tab_header_padding_x", "22px" },
    { "ui_field_padding_y", "1px" },
    { "ui_field_padding_x", "6px" },
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

QVector<CpuPreset> buildCpuPresets()
{
    const int idealThreads = qMax(1, QThread::idealThreadCount());
    const int highWorkers = qMax(1, idealThreads - 1);
    const int mediumWorkers = qMax(1, qRound(static_cast<double>(highWorkers) * 0.66));
    const int lowWorkers = qMax(1, qRound(static_cast<double>(highWorkers) * 0.33));

    const QVector<CpuPreset> raw = {
        {"High", highWorkers},
        {"Medium", mediumWorkers},
        {"Low", lowWorkers},
        {"Minimal", 1},
    };

    QSet<int> usedWorkers;
    QVector<CpuPreset> presets;
    presets.reserve(raw.size());
    for (const CpuPreset &preset : raw) {
        if (usedWorkers.contains(preset.workers)) {
            continue;
        }
        usedWorkers.insert(preset.workers);
        presets.push_back(preset);
    }

    if (presets.isEmpty()) {
        presets.push_back({"High", 1});
    }
    return presets;
}

QLabel *makeRichLabel(const QString &html)
{
    QLabel *label = new QLabel(html);
    label->setTextFormat(Qt::RichText);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setStyleSheet("background: transparent; padding: 0px 2px;");
    return label;
}

QString sequenceDisplayHtml(const QString &sequenceName, const QString &shotName, const QString &shotColor)
{
    const int dot = sequenceName.lastIndexOf('.');
    const QString base = (dot > 0) ? sequenceName.left(dot) : sequenceName;
    const QString ext = (dot > 0) ? sequenceName.mid(dot) : QString();
    const QString extSpan = ext.isEmpty()
        ? QString()
        : QString("<span style='color:#8d8d8d;'>%1</span>").arg(ext.toHtmlEscaped());

    const bool hasShotPrefix =
        !shotName.trimmed().isEmpty()
        && shotName.compare(QString::fromLatin1(ShotNameDetector::UNKNOWN_SHOT), Qt::CaseInsensitive) != 0
        && base.startsWith(shotName);

    if (hasShotPrefix) {
        const QString prefix = shotName.toHtmlEscaped();
        const QString suffix = base.mid(shotName.size()).toHtmlEscaped();
        return QString("<span style='color:%1;'>%2</span><span style='color:#cccccc;'>%3</span>%4")
            .arg(shotColor, prefix, suffix, extSpan);
    }

    return QString("<span style='color:#cccccc;'>%1</span>%2")
        .arg(base.toHtmlEscaped(), extSpan);
}

TableColumnWidthHelper::Config makeSeqColumnConfig(const QString &context)
{
    TableColumnWidthHelper::Config config;
    config.fixedColumns = {ColFrames, ColRange, ColOk, ColSuspect, ColCorrupt, ColStatus};
    config.flexibleColumns = {ColSequence, ColFolder};
    config.priorityColumn = ColFolder;
    config.minimumWidths = {{ColSequence, 180}, {ColFolder, 240}};
    config.defaultWidths = {{ColSequence, 380}, {ColFolder, 520}};
    config.defaultRatios = {{ColSequence, 1.0}, {ColFolder, 1.4}};
    config.contentPaddingPx = 10;
    config.headerPaddingPx = 10;
    config.debugCategory = "queue_table_widths";
    config.debugContext = context;
    return config;
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
    m_cpuControlFile = cpuControlFilePath();
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
    m_statusLabel->setTextFormat(Qt::RichText);
    m_statusLabel->setOpenExternalLinks(false);
    m_statusLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    // El link "N corrupt frames" del status abre el listado global de corruptos.
    connect(m_statusLabel, &QLabel::linkActivated, this, [this](const QString &href) {
        if (href == "corrupt" && !m_allCorruptFiles.isEmpty()) {
            showCorruptDialog("All corrupt frames", m_allCorruptFiles);
        }
    });
    root->addWidget(m_statusLabel);

    m_table = new QTableWidget(0, ColCount, central);
    m_table->setObjectName("seqTable");
    m_table->setHorizontalHeader(new TableHeaderDividerView(Qt::Horizontal, m_table, 12));
    m_table->horizontalHeader()->setStyleSheet(TableHeaderDividerView::dividerStyleSheet());
    QStringList headers;
    headers << "Sequence" << "Folder" << "Frames" << "Range"
            << "OK" << "Suspect" << "Corrupt" << "Status";
    m_table->setHorizontalHeaderLabels(headers);
    for (int i = 0; i < m_table->columnCount(); ++i) {
        if (QTableWidgetItem *hdrItem = m_table->horizontalHeaderItem(i)) {
            hdrItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(false);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideNone);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *hh = static_cast<TableHeaderDividerView *>(m_table->horizontalHeader());
    hh->setNoRightBorderColumns(QSet<int>{ColFrames, ColRange, ColOk, ColSuspect, ColCorrupt, ColStatus});
    hh->setSectionResizeMode(ColSequence, QHeaderView::Interactive);
    hh->setSectionResizeMode(ColFolder, QHeaderView::Interactive);
    hh->setSectionResizeMode(ColFrames, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColRange, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColOk, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColSuspect, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColCorrupt, QHeaderView::Fixed);
    hh->setSectionResizeMode(ColStatus, QHeaderView::Fixed);
    hh->setStretchLastSection(false);
    hh->setMinimumSectionSize(56);
    m_table->setColumnWidth(ColSequence, 380);
    m_table->setColumnWidth(ColFolder, 520);
    m_table->setColumnWidth(ColFrames, 70);
    m_table->setColumnWidth(ColRange, 140);
    m_table->setColumnWidth(ColOk, 60);
    m_table->setColumnWidth(ColSuspect, 70);
    m_table->setColumnWidth(ColCorrupt, 75);
    m_table->setColumnWidth(ColStatus, 110);
    connect(hh, &QHeaderView::sectionResized, this, &MainWindow::onHeaderSectionResized);

    // Click en la celda Corrupt (con >0) abre el listado copiable de esa secuencia.
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (col != ColCorrupt) return;
        QTableWidgetItem *it = m_table->item(row, ColCorrupt);
        if (!it) return;
        const QStringList files = it->data(Qt::UserRole).toStringList();
        if (files.isEmpty()) return;
        const QString name = m_table->item(row, ColSequence)
            ? m_table->item(row, ColSequence)->data(Qt::UserRole).toString()
            : QString();
        showCorruptDialog("Corrupt frames — " + name, files);
    });

    // Doble-click revela la carpeta de la secuencia.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col == ColCorrupt) return; // esa columna usa click simple para el listado
        QTableWidgetItem *it = m_table->item(row, ColFolder);
        if (!it) return;
        const QString folder = it->data(Qt::UserRole).toString();
        if (!folder.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        }
    });

    root->addWidget(m_table, 1);

    QWidget *footer = new QWidget(central);
    footer->setObjectName("seqCheckerFooter");
    footer->setStyleSheet("QWidget#seqCheckerFooter { background: transparent; border: none; }");
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(8);

    m_keepOnTopChk = new QCheckBox("Keep this window on top", footer);
    m_keepOnTopChk->setObjectName("uiOptionCheckbox");
    connect(m_keepOnTopChk, &QCheckBox::stateChanged, this, [this](int state) {
        setKeepOnTopState(state == Qt::Checked);
        saveWindowSettings();
    });
    footerLayout->addWidget(m_keepOnTopChk);
    footerLayout->addStretch(1);

    QLabel *cpuLabel = new QLabel("CPU", footer);
    cpuLabel->setStyleSheet("color:#8f8f8f; font-size:11px; padding-left:6px;");
    footerLayout->addWidget(cpuLabel);

    m_cpuCombo = new ArrowComboBox(footer);
    m_cpuCombo->setObjectName("queueCpuCombo");
    m_cpuCombo->setMinimumWidth(160);
    connect(m_cpuCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!m_cpuCombo || index < 0) return;
        const QString presetName = m_cpuCombo->itemData(index, Qt::UserRole).toString();
        if (presetName.isEmpty()) return;
        m_cpuPresetName = presetName;
        saveWindowSettings();
        // Si hay un analisis en curso, aplicar el nuevo limite de workers en vivo:
        // el backend Python relee el control-file y ajusta la concurrencia a medida
        // que terminan los frames actualmente en proceso.
        if (m_analysisRunning) {
            const int workers = selectedWorkerCount();
            writeCpuControlFile(workers);
            CONDITIONAL_DEBUG("import", "[cpu] cambio en vivo -> workers:" << workers);
        }
    });
    footerLayout->addWidget(m_cpuCombo);

    populateCpuPresetCombo();
    root->addWidget(footer);
    setCentralWidget(central);

    m_pendingSmartColumnSizing = true;
    QTimer::singleShot(0, this, [this]() {
        syncTableColumnsToViewport();
    });
}

void MainWindow::loadStyleSheet()
{
    QString stylesheet;

    for (const QString &res : {QStringLiteral(":/styles/dark_theme.qss"),
                               QStringLiteral(":/styles/tabs.qss")}) {
        QFile file(res);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        stylesheet += QString::fromUtf8(file.readAll()) + "\n";
        file.close();
    }

    QVector<QPair<QString, QString>> replacements;
    replacements.reserve(static_cast<int>(sizeof(COLOR_VARS) / sizeof(COLOR_VARS[0])));
    for (const auto &cv : COLOR_VARS) {
        const QString var = QString::fromUtf8(cv.var);
        const QString value = QString::fromUtf8(cv.value);
        replacements.append({var, value});
        if (qApp) {
            qApp->setProperty(cv.var, value);
        }
    }
    std::sort(replacements.begin(), replacements.end(),
              [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                  return a.first.size() > b.first.size();
              });
    for (const auto &replacement : replacements) {
        stylesheet.replace(replacement.first, replacement.second);
    }

    stylesheet += QString(
        "QWidget#centralRoot { background-color: %1; }"
        "QLabel#statusLabel { color: %2; font-family: 'Inter'; font-size: 14px; padding: 2px 2px; }"
        "QTableWidget#seqTable {"
        "  background-color: %3; border: none; border-radius: 8px;"
        "  color: %2; font-family: 'Inter'; font-size: 13px;"
        "  gridline-color: transparent;"
        "}"
        "QTableWidget#seqTable::item { padding: 4px 8px; border: none; }"
        "QTableWidget#seqTable::item:selected { background-color: %4; color: #d0d0d0; }"
        "QTableWidget#seqTable QLabel { background: transparent; }"
        "QTableWidget#seqTable QScrollBar:vertical { background: %3; width: 12px; margin: 0; }"
        "QTableWidget#seqTable QScrollBar::handle:vertical { background: #3a3a3a; border-radius: 6px; min-height: 24px; }"
        "QTableWidget#seqTable QScrollBar::add-line:vertical, QTableWidget#seqTable QScrollBar::sub-line:vertical { height: 0; }"
    ).arg(COL_BG_PRINCIPAL)
     .arg(COL_TXT_PRINCIPAL)
     .arg(COL_BG_ITEMS)
     .arg("#4b4b4b");

    setStyleSheet(stylesheet);
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
    const int workers = selectedWorkerCount();
    const int maxWorkers = maxWorkerCount();
    // Baseline del control-file: el backend lo relee en vivo cuando se cambia el dropdown.
    writeCpuControlFile(workers);
    m_progressTimer.start();
    setStatusText(QString("Scanning… (%1 workers)").arg(workers));

    QStringList args;
    args << "--json-lines";
    args << "--workers" << QString::number(workers);
    args << "--max-workers" << QString::number(maxWorkers);
    args << "--control-file" << m_cpuControlFile;
    for (const QString &f : folders) {
        args << "--folder" << f;
    }
    CONDITIONAL_DEBUG("import", "[startAnalysis] folders:" << folders << "workers:" << workers);
    m_runner->run(m_scriptPath, args);
}

void MainWindow::resetResults()
{
    m_table->setRowCount(0);
    m_seqRowById.clear();
    m_allCorruptFiles.clear();
    m_queueOrder.clear();
    m_completedSeqIds.clear();
    m_lastShotForColor.clear();
    m_shotColorBlock = -1;
    m_activeQueueIndex = -1;
    m_lastFpsDoneFrames = 0;
    m_lastFpsElapsedMs = 0;
    m_smoothedFps = 0.0;
    m_totalFrames = 0;
    m_doneFrames = 0;
    m_corruptTotal = 0;
    m_pendingSmartColumnSizing = true;
    m_manualColumnWidthOverride = false;
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
    m_pendingSmartColumnSizing = true;
    m_manualColumnWidthOverride = false;
    QTimer::singleShot(0, this, [this]() {
        syncTableColumnsToViewport();
    });
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
        const QString sequenceName = o.value("name").toString();
        QTableWidgetItem *seqItem = m_table->item(row, ColSequence);
        seqItem->setText(QString());
        seqItem->setData(Qt::UserRole, sequenceName);

        ShotNameDetector::ItemInput shotInput;
        shotInput.displayName = sequenceName;
        shotInput.baseName = QFileInfo(sequenceName).completeBaseName();
        shotInput.absolutePath = QDir(folder).filePath(sequenceName);
        shotInput.isSequence = true;
        const ShotNameDetector::Detection shotDet = ShotNameDetector::detectSingle(shotInput, "SeqCheckerTable");

        QString shotName = shotDet.shotName.trimmed();
        if (shotName.isEmpty()) {
            shotName = QString::fromLatin1(ShotNameDetector::UNKNOWN_SHOT);
        }
        if (shotName != m_lastShotForColor) {
            m_lastShotForColor = shotName;
            ++m_shotColorBlock;
        }
        const QString shotColor = ShotNameDetector::shotColorForBlock(m_shotColorBlock);
        seqItem->setData(ColRoleShotName, shotName);
        seqItem->setData(ColRoleShotColor, shotColor);
        m_table->setCellWidget(row, ColSequence, makeRichLabel(sequenceDisplayHtml(sequenceName, shotName, shotColor)));

        QTableWidgetItem *fit = m_table->item(row, ColFolder);
        fit->setText(folder);
        fit->setData(Qt::UserRole, folder);
        fit->setToolTip(folder);
        m_table->item(row, ColFrames)->setText(QString::number(o.value("frames").toInt()));
        m_table->item(row, ColRange)->setText(o.value("range").toString());
        if (!m_queueOrder.contains(id)) {
            m_queueOrder.append(id);
        }
        refreshQueueStatuses();
        m_totalFrames += o.value("frames").toInt();
    } else if (tag == "MT_SEQ_START") {
        const QString id = o.value("id").toString();
        if (!m_queueOrder.contains(id)) {
            m_queueOrder.append(id);
        }
        m_activeQueueIndex = m_queueOrder.indexOf(id);
        refreshQueueStatuses();
    } else if (tag == "MT_PROGRESS") {
        m_doneFrames = o.value("done").toInt();
        if (o.contains("total")) m_totalFrames = o.value("total").toInt();

        const qint64 elapsedMs = m_progressTimer.isValid() ? m_progressTimer.elapsed() : 0;
        const int deltaFrames = m_doneFrames - m_lastFpsDoneFrames;
        const qint64 deltaMs = elapsedMs - m_lastFpsElapsedMs;
        if (deltaFrames > 0 && deltaMs > 0) {
            const double instantFps = (1000.0 * static_cast<double>(deltaFrames)) / static_cast<double>(deltaMs);
            m_smoothedFps = (m_smoothedFps <= 0.0) ? instantFps : (m_smoothedFps * 0.7 + instantFps * 0.3);
        }
        m_lastFpsDoneFrames = m_doneFrames;
        m_lastFpsElapsedMs = elapsedMs;

        setStatusText(QString("Analyzing… %1 / %2 frames — %3 corrupt — %4 fps")
                          .arg(m_doneFrames)
                          .arg(m_totalFrames)
                          .arg(m_corruptTotal)
                          .arg(QString::number(std::max(0.0, m_smoothedFps), 'f', 1)));
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
        m_completedSeqIds.insert(id);
        if (m_activeQueueIndex >= 0 && m_activeQueueIndex < m_queueOrder.size()
            && m_queueOrder.at(m_activeQueueIndex) == id) {
            m_activeQueueIndex = -1;
        }

        // Paths corruptos: se guardan en la celda Corrupt (click simple abre el listado).
        QStringList corruptFiles;
        for (const QJsonValue &v : o.value("corrupt_files").toArray()) {
            corruptFiles << v.toString();
        }
        QTableWidgetItem *cit = m_table->item(row, ColCorrupt);
        cit->setForeground(statusColor(corrupt > 0 ? "corrupt" : "ok"));
        QFont corruptFont = cit->font();
        corruptFont.setUnderline(corrupt > 0);
        cit->setFont(corruptFont);
        if (corrupt > 0) {
            cit->setData(Qt::UserRole, corruptFiles);
            cit->setToolTip("Click to list & copy corrupt frame paths");
            m_allCorruptFiles += corruptFiles;
        } else {
            cit->setData(Qt::UserRole, QStringList());
            cit->setToolTip(QString());
        }

        // Tooltip con el detalle de frames problemáticos en el resto de las celdas.
        const QString detail = o.value("detail").toString();
        if (!detail.isEmpty()) {
            for (int c = 0; c < ColCount; ++c) {
                if (c == ColCorrupt && corrupt > 0) continue;
                if (c == ColFolder) continue; // Mantener el tooltip del path completo.
                QTableWidgetItem *cellItem = m_table->item(row, c);
                if (cellItem) {
                    cellItem->setToolTip(detail);
                }
                if (QWidget *cellWidget = m_table->cellWidget(row, c)) {
                    cellWidget->setToolTip(detail);
                }
            }
        }
        m_corruptTotal += corrupt;
        refreshQueueStatuses();
    } else if (tag == "MT_DONE") {
        m_activeQueueIndex = -1;
        setStatusDone(o.value("sequences").toInt(),
                      o.value("corrupt").toInt(),
                      o.value("suspect").toInt());
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
    if (m_statusLabel) m_statusLabel->setText(text.toHtmlEscaped());
}

void MainWindow::setStatusDone(int sequences, int corrupt, int suspect)
{
    if (!m_statusLabel) return;
    // El conteo de corruptos se muestra como link clickeable que abre el listado global.
    QString corruptPart;
    if (corrupt > 0) {
        corruptPart = QString("<a href=\"corrupt\" style=\"color:%1; font-weight:600;\">%2 corrupt frames</a>")
                          .arg(COL_STATUS_CORRUPT).arg(corrupt);
    } else {
        corruptPart = "0 corrupt frames";
    }
    m_statusLabel->setText(QString("Done — %1 sequences · %2 · %3 suspect.")
                               .arg(sequences).arg(corruptPart).arg(suspect));
}

void MainWindow::showCorruptDialog(const QString &title, const QStringList &files)
{
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    dlg.resize(720, 460);

    auto *lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(10);

    auto *info = new QLabel(QString("%1 corrupt frame(s). Select text and copy, or use \"Copy all\".")
                               .arg(files.size()), &dlg);
    info->setStyleSheet(QString("color:%1; font-family:'Inter'; font-size:13px;").arg(COL_TXT_PRINCIPAL));
    lay->addWidget(info);

    auto *text = new QPlainTextEdit(&dlg);
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setPlainText(files.join("\n"));
    text->setStyleSheet(QString(
        "QPlainTextEdit { background:%1; color:%2; border:1px solid %3; border-radius:4px;"
        " font-family:'Roboto Mono','Consolas',monospace; font-size:12px; padding:6px; }"
    ).arg("#1a1a1a").arg("#9a9a9a").arg(COL_BORDER));
    lay->addWidget(text, 1);

    auto *buttons = new QDialogButtonBox(&dlg);
    auto *copyBtn = buttons->addButton("Copy all", QDialogButtonBox::ActionRole);
    auto *closeBtn = buttons->addButton(QDialogButtonBox::Close);
    lay->addWidget(buttons);

    connect(copyBtn, &QPushButton::clicked, &dlg, [files, info]() {
        QApplication::clipboard()->setText(files.join("\n"));
        info->setText(QString("Copied %1 path(s) to clipboard.").arg(files.size()));
    });
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.setStyleSheet(QString("QDialog { background:%1; }").arg(COL_BG_PRINCIPAL));
    dlg.exec();
}

void MainWindow::refreshQueueStatuses()
{
    if (!m_table) return;

    int queuedAfterActive = 0;
    int queuedNoActive = 0;
    for (int i = 0; i < m_queueOrder.size(); ++i) {
        const QString &seqId = m_queueOrder.at(i);
        if (m_completedSeqIds.contains(seqId)) {
            continue;
        }

        const int row = rowForSeqId(seqId, false);
        if (row < 0) {
            continue;
        }
        QTableWidgetItem *statusItem = m_table->item(row, ColStatus);
        if (!statusItem) {
            continue;
        }

        if (m_activeQueueIndex >= 0 && i == m_activeQueueIndex) {
            statusItem->setText(statusLabelText("pending"));
            statusItem->setForeground(statusColor("pending"));
            continue;
        }

        int queuedPos = 0;
        if (m_activeQueueIndex >= 0) {
            ++queuedAfterActive;
            queuedPos = queuedAfterActive;
        } else {
            ++queuedNoActive;
            queuedPos = queuedNoActive;
        }
        statusItem->setText(QString("Queued #%1").arg(queuedPos));
        statusItem->setForeground(QColor("#5a9ab5"));
    }
}

void MainWindow::populateCpuPresetCombo()
{
    if (!m_cpuCombo) return;

    m_cpuCombo->blockSignals(true);
    m_cpuCombo->clear();
    const QVector<CpuPreset> presets = buildCpuPresets();
    for (const CpuPreset &preset : presets) {
        const QString label = QString("%1 (%2 workers)")
                                  .arg(preset.name)
                                  .arg(preset.workers);
        m_cpuCombo->addItem(label, preset.name);
        m_cpuCombo->setItemData(m_cpuCombo->count() - 1, preset.workers, Qt::UserRole + 1);
    }
    applyCpuPresetSelection(m_cpuPresetName);
    m_cpuCombo->blockSignals(false);
}

void MainWindow::applyCpuPresetSelection(const QString &presetName)
{
    if (!m_cpuCombo) return;

    for (int i = 0; i < m_cpuCombo->count(); ++i) {
        if (m_cpuCombo->itemData(i, Qt::UserRole).toString().compare(presetName, Qt::CaseInsensitive) == 0) {
            m_cpuCombo->setCurrentIndex(i);
            m_cpuPresetName = m_cpuCombo->itemData(i, Qt::UserRole).toString();
            return;
        }
    }
    if (m_cpuCombo->count() > 0) {
        m_cpuCombo->setCurrentIndex(0);
        m_cpuPresetName = m_cpuCombo->itemData(0, Qt::UserRole).toString();
    }
}

int MainWindow::selectedWorkerCount() const
{
    if (!m_cpuCombo || m_cpuCombo->count() == 0) {
        return qMax(1, QThread::idealThreadCount() - 1);
    }
    int index = m_cpuCombo->currentIndex();
    if (index < 0) index = 0;
    const int workers = m_cpuCombo->itemData(index, Qt::UserRole + 1).toInt();
    return qMax(1, workers);
}

int MainWindow::maxWorkerCount() const
{
    // Mayor cantidad de workers entre todos los presets: define el tope del pool
    // Python para que el control-file pueda escalar hacia arriba en vivo.
    int maxW = 1;
    if (m_cpuCombo) {
        for (int i = 0; i < m_cpuCombo->count(); ++i) {
            maxW = qMax(maxW, m_cpuCombo->itemData(i, Qt::UserRole + 1).toInt());
        }
    }
    return qMax(maxW, selectedWorkerCount());
}

QString MainWindow::cpuControlFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    QDir().mkpath(dir);
    return QDir(dir).filePath("cpu_control.txt");
}

void MainWindow::writeCpuControlFile(int workers) const
{
    if (m_cpuControlFile.isEmpty()) return;
    QFile f(m_cpuControlFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        f.write(QByteArray::number(qMax(1, workers)));
        f.close();
    }
}

void MainWindow::setKeepOnTopState(bool enabled)
{
    m_keepOnTop = enabled;
    if (m_keepOnTopChk) {
        m_keepOnTopChk->blockSignals(true);
        m_keepOnTopChk->setChecked(enabled);
        m_keepOnTopChk->blockSignals(false);
    }

#ifdef Q_OS_WIN
    if (isVisible()) {
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        if (hwnd) {
            ::SetWindowPos(
                hwnd,
                enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER
            );
        }
    } else {
        setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    }
#else
    const QRect currentGeometry = geometry();
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    if (wasVisible) {
        show();
        setGeometry(currentGeometry);
    }
#endif
}

bool MainWindow::isDynamicColumn(int logicalIndex) const
{
    return logicalIndex == ColSequence || logicalIndex == ColFolder;
}

void MainWindow::applySmartFlexibleColumns()
{
    if (!m_table || !m_table->viewport() || m_adjustingColumns) {
        return;
    }
    const auto config = makeSeqColumnConfig("applySmartFlexibleColumns");
    m_adjustingColumns = true;
    const bool applied = TableColumnWidthHelper::applyContentAwareWidths(m_table, config);
    m_adjustingColumns = false;
    m_pendingSmartColumnSizing = !applied;
}

void MainWindow::updateProportionalColumns()
{
    if (!m_table || !m_table->viewport() || m_adjustingColumns) {
        return;
    }
    const auto config = makeSeqColumnConfig("updateProportionalColumns");
    m_adjustingColumns = true;
    TableColumnWidthHelper::applyProportionalWidths(m_table, config);
    m_adjustingColumns = false;
}

void MainWindow::syncTableColumnsToViewport()
{
    if (m_pendingSmartColumnSizing) {
        if (!isVisible()) {
            return;
        }
        applySmartFlexibleColumns();
        if (m_pendingSmartColumnSizing) {
            updateProportionalColumns();
        }
        return;
    }
    updateProportionalColumns();
}

void MainWindow::recheckSmartColumnsAfterWindowResize()
{
    if (!m_table || !m_table->viewport() || m_seqRowById.isEmpty() || m_manualColumnWidthOverride) {
        return;
    }
    const auto config = makeSeqColumnConfig("recheckSmartColumnsAfterWindowResize");
    m_adjustingColumns = true;
    const bool applied = TableColumnWidthHelper::applyContentAwareWidthsIfAllFit(m_table, config);
    m_adjustingColumns = false;
    if (applied) {
        m_pendingSmartColumnSizing = false;
    }
}

void MainWindow::onHeaderSectionResized(int logicalIndex, int oldSize, int newSize)
{
    Q_UNUSED(oldSize);
    if (m_adjustingColumns || !isDynamicColumn(logicalIndex)) {
        return;
    }
    m_manualColumnWidthOverride = true;
    const auto config = makeSeqColumnConfig("onHeaderSectionResized");
    m_adjustingColumns = true;
    TableColumnWidthHelper::applyManualResizeRebalance(m_table, config, logicalIndex, newSize);
    m_adjustingColumns = false;
}

// ----------------------------------------------------------------------------
// Geometría de ventana
// ----------------------------------------------------------------------------
void MainWindow::scheduleGeometrySave()
{
    if (m_geometrySaveTimer) m_geometrySaveTimer->start();
}

QString MainWindow::settingsFilePath() const
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (configDir.isEmpty()) return QString();
    QDir().mkpath(configDir);
    return configDir + "/window_geometry.ini";
}

void MainWindow::loadWindowSettings()
{
    const QString configPath = settingsFilePath();
    QByteArray geo;
    if (!configPath.isEmpty() && QFile::exists(configPath)) {
        QSettings s(configPath, QSettings::IniFormat);
        geo = s.value("geometry").toByteArray();
        m_cpuPresetName = s.value("seqchecker/cpu_preset", "High").toString();
        m_keepOnTop = s.value("seqchecker/keep_on_top", false).toBool();
    } else {
        QSettings s;
        geo = s.value("geometry").toByteArray();
        m_cpuPresetName = s.value("seqchecker/cpu_preset", "High").toString();
        m_keepOnTop = s.value("seqchecker/keep_on_top", false).toBool();
    }
    if (!geo.isEmpty()) restoreGeometry(geo);
    applyCpuPresetSelection(m_cpuPresetName);
    setKeepOnTopState(m_keepOnTop);
}

void MainWindow::saveWindowSettings()
{
    const QString configPath = settingsFilePath();

    auto saveSettings = [this](QSettings &s) {
        s.setValue("geometry", saveGeometry());
        s.setValue("seqchecker/cpu_preset", m_cpuPresetName);
        s.setValue("seqchecker/keep_on_top", m_keepOnTop);
    };

    if (!configPath.isEmpty()) {
        QSettings s(configPath, QSettings::IniFormat);
        saveSettings(s);
    } else {
        QSettings s;
        saveSettings(s);
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_dropOverlay && m_dropOverlay->isVisible()) m_dropOverlay->setGeometry(rect());
    syncTableColumnsToViewport();
    QTimer::singleShot(0, this, [this]() {
        recheckSmartColumnsAfterWindowResize();
    });
    scheduleGeometrySave();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        syncTableColumnsToViewport();
    });
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

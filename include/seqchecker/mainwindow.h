#ifndef SEQCHECKER_MAINWINDOW_H
#define SEQCHECKER_MAINWINDOW_H

#include <QMainWindow>
#include <QHash>
#include <QString>
#include <QStringList>

// ----------------------------------------------------------------------------
// Tokens de color base (de UI_STYLE_LGA_APPS.md)
// ----------------------------------------------------------------------------
inline constexpr const char* COL_BG_PRINCIPAL  = "#161616";
inline constexpr const char* COL_BG_ITEMS      = "#1d1d1d";
inline constexpr const char* COL_BORDER        = "#303030";
inline constexpr const char* COL_TXT_PRINCIPAL = "#B2B2B2";
inline constexpr const char* COL_VIOLETA_OSC   = "#443a91";
inline constexpr const char* COL_VIOLETA_CLARO = "#774dcb";

// Colores de estado de la tabla de resultados.
inline constexpr const char* COL_STATUS_OK      = "#51b151"; // verde
inline constexpr const char* COL_STATUS_SUSPECT = "#fdc942"; // amarillo
inline constexpr const char* COL_STATUS_CORRUPT = "#a06060"; // rojo apagado (= error transcode)
inline constexpr const char* COL_STATUS_PENDING = "#777777"; // gris (analizando)

// Tokens visuales centralizados del overlay de drag & drop (de MediaTools).
inline constexpr int DROP_OVERLAY_OUTER_MARGIN = 22;
inline constexpr int DROP_OVERLAY_PANE_PADDING = 14;
inline constexpr int DROP_OVERLAY_PANE_BORDER_WIDTH = 2;
inline constexpr int DROP_OVERLAY_PANE_BORDER_RADIUS = 12;
inline constexpr const char* DROP_OVERLAY_BACKDROP_COLOR = "rgba(16, 16, 16, 236)";
inline constexpr const char* DROP_OVERLAY_PANE_DASH_BORDER_COLOR = "#443a91";
inline constexpr const char* DROP_OVERLAY_ACTIVE_PANE_BG_COLOR = "rgba(56, 45, 118, 40)";
inline constexpr const char* DROP_OVERLAY_LABEL_FONT_FAMILY = "Inter";
inline constexpr int DROP_OVERLAY_LABEL_FONT_PX = 20;
inline constexpr int DROP_OVERLAY_LABEL_FONT_WEIGHT = 500;
inline constexpr const char* DROP_OVERLAY_LABEL_COLOR = "#5f5f5f";
inline constexpr const char* DROP_OVERLAY_LABEL_ACTIVE_COLOR = "#8f8f8f";

class QCloseEvent;
class QMoveEvent;
class QResizeEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QMimeData;
class QWidget;
class QLabel;
class QTableWidget;
class QTimer;
class PythonRunner;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private:
    void buildUi();
    void loadStyleSheet();
    void loadWindowSettings();
    void saveWindowSettings();
    void scheduleGeometrySave();

    // Drag & drop overlay (1 zona).
    void createDropOverlay();
    void setDropOverlayVisible(bool visible, bool active);
    QStringList extractDroppedFolders(const QMimeData *mimeData) const;

    // Análisis.
    void startAnalysis(const QStringList &folders);
    void onPythonLine(const QString &line);
    void onPythonError(const QString &line);
    void onPythonFinished(int exitCode, const QString &out, const QString &err);
    void resetResults();
    int rowForSeqId(const QString &id, bool createIfMissing);
    void setStatusText(const QString &text);
    void setStatusDone(int sequences, int corrupt, int suspect);
    void showCorruptDialog(const QString &title, const QStringList &files);

    QString     m_pythonExe;
    QString     m_scriptPath;
    PythonRunner *m_runner = nullptr;
    bool         m_analysisRunning = false;

    QTableWidget *m_table = nullptr;
    QLabel       *m_statusLabel = nullptr;
    QWidget      *m_dropOverlay = nullptr;
    QWidget      *m_dropOverlayPane = nullptr;
    QLabel       *m_dropOverlayLabel = nullptr;
    QTimer       *m_geometrySaveTimer = nullptr;

    QHash<QString, int> m_seqRowById; // seq id -> fila de la tabla
    QStringList m_allCorruptFiles;    // agregado global de paths corruptos
    int m_totalFrames = 0;
    int m_doneFrames = 0;
    int m_corruptTotal = 0;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
};

#endif // SEQCHECKER_MAINWINDOW_H

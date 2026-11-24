#pragma once

#include "engine/uciengine.h"

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QWidget>

#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QProcess;

// Syzygy endgame tablebases: where they come from (folders, .iso disc images used as
// they are), who uses them (Tablebases panel, engines), and their download.
namespace tablebases {

// Sources chosen by the user: folders and .iso images. A folder also counts
// for its direct subfolders ("Syzygy345", "Syzygy6WDL"…) and for the .iso images it contains.
QStringList sources();
void setSources(const QStringList& list);

struct Summary {
    int files = 0;
    qint64 bytes = 0;
    int wdl[8] = {}, dtz[8] = {}; // tables per number of pieces (3 to 7)
    QStringList folders;          // folders containing tables
    QStringList images;           // .iso images containing some
    QStringList unreadable;       // unreadable images, with the reason
    int largest() const;          // largest number of pieces having at least one result table
    QString text() const;         // "344 fichiers · 18,7 Go · 3 à 5 pièces complètes ; 6 pièces : 27 finales sur 365"
};
constexpr int TablesFor[8] = {0, 0, 0, 5, 30, 110, 365, 1001}; // distinct endgames per number of pieces
Summary scan(const QStringList& sources);

bool imagesSupported(); // the hydra64-tablefs program is there (FUSE)
// Makes the tables available: images presented in a temporary folder, tables opened for
// the Tablebases panel, folder given to engines started afterwards. Returns the effective path
// (folders separated by ":", empty if there is nothing).
QString apply();
QString effectivePath();
void shutdown(); // on closing: the images are no longer presented

// Use by the engines (saved)
bool engineUse(UciEngine::TablebaseRole role);
void setEngineUse(UciEngine::TablebaseRole role, bool on);

QString defaultDownloadDir(); // ~/Documents/hydra64/tablebases

// Downloadable sets, from the lightest to the most complete. Sizes are those of the Lichess
// mirror recorded on 20 November 2026; they are read again from the server before downloading.
struct Set {
    QString label;
    QStringList directories; // subfolders of tables/standard/
    qint64 approxBytes;
};
QList<Set> sets();
// What is left to download of a set in `directory`, according to the tables already there
// (without querying the server): approximate size, and number of missing files.
struct Remaining {
    qint64 bytes = 0;
    int files = 0;      // missing files
    int present = 0;    // files of the set already there
    bool complete() const { return files == 0; }
};
Remaining remaining(const Set& set, const QString& directory);

} // namespace tablebases

// Download of the tables (a single one for the application: it goes on with the Preferences dialog closed).
// Resumption of interrupted files, check of the size and of the BLAKE2b checksum of each file.
class TablebaseDownloader : public QObject {
    Q_OBJECT
public:
    struct File {
        QString name;
        QUrl url;
        qint64 size = -1;   // -1: unknown
        QByteArray blake2b; // hexadecimal; empty: no check
    };
    static TablebaseDownloader& instance();
    explicit TablebaseDownloader(QObject* parent = nullptr);
    ~TablebaseDownloader() override;

    // Reads the server's indexes, then downloads what is missing in `directory`.
    void start(const QStringList& serverDirectories, const QString& directory);
    // Given list of files (tests, or a source other than the mirror).
    void startFiles(const QList<File>& files, const QString& directory);
    void cancel(); // partial files stay, to resume
    bool isRunning() const { return m_running; }
    QString directory() const { return m_directory; }
    qint64 totalBytes() const { return m_total; }
    qint64 doneBytes() const { return m_done; }
    int filesTotal() const { return int(m_files.size()); }
    int filesDone() const { return m_filesDone; }
    QString status() const { return m_status; }
    // Average speed over the last ten seconds (bytes per second, 0: not measured yet) and remaining
    // time estimated from it (seconds, -1: unknown).
    qint64 bytesPerSecond() const;
    qint64 secondsLeft() const;
    static QString durationText(qint64 seconds); // "2 h 05 min", "3 min 20 s", "12 s"
    static QUrl baseUrl();

signals:
    void progress();                          // at most four times per second
    void finished(bool ok, const QString& message);

private:
    struct Job {
        int file = -1;
        QNetworkReply* reply = nullptr;
        std::unique_ptr<QFile> out;
        std::unique_ptr<QCryptographicHash> hash;
        int attempts = 0;
    };
    void fetchIndex();
    void beginFiles();
    void pump();
    void startJob(Job& job, int file);
    void jobFinished(Job* job);
    void fail(const QString& message);
    void setStatus(const QString& s);

    QNetworkAccessManager* m_net;
    QStringList m_serverDirs;
    QList<QNetworkReply*> m_indexReplies;
    QHash<QString, QByteArray> m_sums;
    QList<File> m_files;
    QString m_directory, m_status;
    QList<Job*> m_jobs;
    int m_next = 0, m_filesDone = 0, m_failed = 0;
    qint64 m_total = 0, m_done = 0, m_lastEmit = 0;
    void sample();
    void resetSpeed();
    qint64 m_received = 0;                       // bytes that came from the network (without the files already there or the resumptions)
    QList<QPair<qint64, qint64>> m_samples;      // (time in ms, m_received), one per second
    class QTimer* m_tick = nullptr;
    bool m_running = false;
};

// "Tablebases" page of the Preferences. The sources and the use by the engines are applied by
// commit() (OK button); a download starts right away.
class TablebasePage : public QWidget {
    Q_OBJECT
public:
    explicit TablebasePage(QWidget* parent = nullptr);
    void commit();
    QStringList shownSources() const;
    void addSource(const QString& path); // folder or .iso image
    QString summaryText() const;
    QString downloadStatusText() const;
    QString spaceText() const;
    void setDownloadChoice(int set, const QString& directory);

private:
    void refreshSummary();
    void refreshDownload();
    QListWidget* m_sources;
    QLabel *m_summary, *m_space, *m_downloadStatus;
    QCheckBox *m_analysis, *m_play, *m_tournament, *m_online;
    QComboBox* m_set;
    QLineEdit* m_directory;
    QProgressBar* m_progress;
    QPushButton* m_download;
};

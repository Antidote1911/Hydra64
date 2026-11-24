#include "tablebases.h"
#include "userdirs.h"

#include "chess/tablebase.h"
#include "tablefs/udf.h"
#include "theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <csignal>

namespace tablebases {

namespace {

QProcess* g_helper = nullptr;
QString g_mount, g_effective;

bool isImage(const QString& path) { return path.endsWith(QLatin1String(".iso"), Qt::CaseInsensitive) && QFileInfo(path).isFile(); }
bool isTable(const QString& name) { return name.endsWith(QLatin1String(".rtbw")) || name.endsWith(QLatin1String(".rtbz")); }

// "KRPvKR.rtbw" → 5 pieces
int piecesOf(const QString& name)
{
    int n = 0;
    for (const QChar c : name) {
        if (c == QLatin1Char('.')) break;
        n += c != QLatin1Char('v');
    }
    return n;
}

QString helperPath() { return QApplication::applicationDirPath() + QStringLiteral("/hydra64-tablefs"); }

QString runtimeDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return dir.isEmpty() ? QDir::tempPath() : dir;
}

void note(Summary& s, const QString& name, qint64 size, QSet<QString>& seen)
{
    if (seen.contains(name)) return; // same table in two sources: counted once
    seen.insert(name);
    ++s.files;
    s.bytes += size;
    const int pieces = piecesOf(name);
    if (pieces >= 3 && pieces <= 7) ++(name.endsWith(QLatin1String(".rtbw")) ? s.wdl : s.dtz)[pieces];
}

void scanImage(Summary& s, const QString& path, QSet<QString>& seen)
{
    std::vector<tablefs::ImageFile> files;
    std::string error;
    if (!tablefs::readImage(QFile::encodeName(path).toStdString(), files, &error)) {
        s.unreadable << QStringLiteral("%1 : %2").arg(QFileInfo(path).fileName(), QString::fromStdString(error));
        return;
    }
    const auto tables = tablefs::syzygyTables(files);
    if (tables.empty()) return;
    s.images << path;
    for (const auto& t : tables) note(s, QString::fromStdString(t.name()), qint64(t.size), seen);
}

void scanFolder(Summary& s, const QString& path, QSet<QString>& seen, bool top)
{
    QDir dir(path);
    bool has = false;
    for (const QFileInfo& f : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (f.isDir()) {
            if (top) scanFolder(s, f.absoluteFilePath(), seen, false);
        } else if (isTable(f.fileName())) {
            has = true;
            note(s, f.fileName(), f.size(), seen);
        } else if (top && isImage(f.absoluteFilePath())) {
            scanImage(s, f.absoluteFilePath(), seen);
        }
    }
    if (has) s.folders << dir.absolutePath();
}

// Mount folders left by an instance that stopped abruptly.
void clearStaleMounts()
{
#ifdef Q_OS_WIN
    return; // no mounting of images on Windows (no FUSE)
#else
    QDir dir(runtimeDir());
    for (const QString& name : dir.entryList({QStringLiteral("hydra64-tables-*")}, QDir::Dirs)) {
        const qint64 pid = name.section(QLatin1Char('-'), -1).toLongLong();
        if (pid == QCoreApplication::applicationPid() || (pid > 0 && ::kill(pid_t(pid), 0) == 0)) continue;
        const QString path = dir.absoluteFilePath(name);
        QProcess::execute(QStringLiteral("fusermount3"), {QStringLiteral("-u"), QStringLiteral("-z"), path});
        QDir().rmdir(path);
    }
#endif
}

} // namespace

QStringList sources()
{
    QSettings s;
    if (s.contains("tablebase/sources")) return s.value("tablebase/sources").toStringList();
    const QString old = s.value("tablebase/path").toString(); // former setting: a single folder
    return old.isEmpty() ? QStringList() : old.split(QLatin1Char(':'), Qt::SkipEmptyParts);
}

void setSources(const QStringList& list)
{
    QStringList clean;
    for (const QString& p : list)
        if (!p.trimmed().isEmpty() && !clean.contains(p)) clean << p;
    QSettings().setValue("tablebase/sources", clean);
}

int Summary::largest() const
{
    for (int n = 7; n >= 3; --n)
        if (wdl[n]) return n;
    return 0;
}

QString Summary::text() const
{
    if (!files) return QObject::tr("Aucune table de finales.");
    const QLocale loc;
    QStringList parts;
    int completeUpTo = 2;
    while (completeUpTo < 7 && wdl[completeUpTo + 1] >= TablesFor[completeUpTo + 1]) ++completeUpTo;
    if (completeUpTo >= 3) parts << QObject::tr("3 à %1 pièces : complètes").arg(completeUpTo);
    for (int n = completeUpTo + 1; n <= 7; ++n)
        if (wdl[n]) parts << QObject::tr("%1 pièces : %2 finales sur %3").arg(n).arg(wdl[n]).arg(TablesFor[n]);
    int missingDistances = 0;
    for (int n = 3; n <= 7; ++n) missingDistances += std::max(0, wdl[n] - dtz[n]);
    if (missingDistances) parts << QObject::tr("%1 sans table de distances").arg(missingDistances);
    return QObject::tr("%1 fichiers · %2 · %3").arg(loc.toString(files), loc.formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat), parts.join(QStringLiteral(" ; ")));
}

Summary scan(const QStringList& list)
{
    Summary s;
    QSet<QString> seen;
    for (const QString& source : list) {
        if (isImage(source)) scanImage(s, source, seen);
        else if (QFileInfo(source).isDir()) scanFolder(s, source, seen, true);
    }
    return s;
}

bool imagesSupported() { return QFileInfo(helperPath()).isExecutable(); }

void shutdown()
{
    if (g_helper) {
        g_helper->terminate(); // SIGTERM: the program unmounts its folder
        if (!g_helper->waitForFinished(3000)) g_helper->kill(), g_helper->waitForFinished(1000);
        delete g_helper;
        g_helper = nullptr;
    }
    if (!g_mount.isEmpty()) {
        if (!QDir().rmdir(g_mount)) { // still mounted (forced stop): lazy unmount
            QProcess::execute(QStringLiteral("fusermount3"), {QStringLiteral("-u"), QStringLiteral("-z"), g_mount});
            QDir().rmdir(g_mount);
        }
        g_mount.clear();
    }
}

QString effectivePath() { return g_effective; }

QString apply()
{
    static bool first = true;
    if (first) {
        first = false;
        clearStaleMounts();
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] { shutdown(); });
    }
    const Summary s = scan(sources());
    // The tables must be closed before removing the images folder
    chess::tb::init(std::string());
    shutdown();
    QStringList dirs = s.folders;
    if (!s.images.isEmpty() && imagesSupported()) {
        g_mount = runtimeDir() + QStringLiteral("/hydra64-tables-%1").arg(QCoreApplication::applicationPid());
        QDir().mkpath(g_mount);
        g_helper = new QProcess;
        g_helper->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        g_helper->start(helperPath(), QStringList{g_mount} + s.images);
        bool ready = false;
        for (int i = 0; i < 100 && !ready && g_helper->state() != QProcess::NotRunning; ++i) { // 5 s at most
            QThread::msleep(50);
            g_helper->waitForStarted(1);
            ready = !QDir(g_mount).entryList({QStringLiteral("*.rtbw")}, QDir::Files).isEmpty();
        }
        if (ready) dirs << g_mount;
        else shutdown();
    }
    g_effective = dirs.join(QLatin1Char(':'));
    chess::tb::init(QFile::encodeName(g_effective).toStdString());
    UciEngine::Tablebases t;
    t.path = g_effective;
    t.analysis = engineUse(UciEngine::TablebaseRole::Analysis);
    t.play = engineUse(UciEngine::TablebaseRole::Play);
    t.tournament = engineUse(UciEngine::TablebaseRole::Tournament);
    UciEngine::setTablebases(t);
    return g_effective;
}

static const char* roleKey(UciEngine::TablebaseRole role)
{
    return role == UciEngine::TablebaseRole::Analysis ? "tablebase/engines/analysis" : role == UciEngine::TablebaseRole::Play ? "tablebase/engines/play" : "tablebase/engines/tournament";
}
bool engineUse(UciEngine::TablebaseRole role) { return QSettings().value(roleKey(role), true).toBool(); }
void setEngineUse(UciEngine::TablebaseRole role, bool on) { QSettings().setValue(roleKey(role), on); }

QString defaultDownloadDir() { return userdirs::tablebases(); }

QList<Set> sets()
{
    return {
        {QObject::tr("3, 4 et 5 pièces, résultats et distances"), {QStringLiteral("3-4-5-wdl"), QStringLiteral("3-4-5-dtz")}, 990'000'000LL},
        {QObject::tr("Jusqu'à 6 pièces, résultats seuls"), {QStringLiteral("3-4-5-wdl"), QStringLiteral("3-4-5-dtz"), QStringLiteral("6-wdl")}, 73'820'000'000LL},
        {QObject::tr("Jusqu'à 6 pièces, résultats et distances"), {QStringLiteral("3-4-5-wdl"), QStringLiteral("3-4-5-dtz"), QStringLiteral("6-wdl"), QStringLiteral("6-dtz")}, 161'180'000'000LL},
    };
}

Remaining remaining(const Set& set, const QString& directory)
{
    // Folders of the mirror: number of files and size recorded on 20 November 2026
    struct Part {
        const char* name;
        bool sixPieces;
        const char* extension;
        int files;
        qint64 bytes;
    };
    static const Part parts[] = {{"3-4-5-wdl", false, ".rtbw", 145, 400'000'000LL}, {"3-4-5-dtz", false, ".rtbz", 145, 590'000'000LL},
                                 {"6-wdl", true, ".rtbw", 365, 72'830'000'000LL}, {"6-dtz", true, ".rtbz", 365, 87'360'000'000LL}};
    Remaining r;
    const QFileInfoList present = QDir(directory).entryInfoList({QStringLiteral("*.rtbw"), QStringLiteral("*.rtbz")}, QDir::Files);
    for (const Part& part : parts) {
        if (!set.directories.contains(QLatin1String(part.name))) continue;
        int files = 0;
        qint64 bytes = 0;
        for (const QFileInfo& f : present) {
            const int pieces = piecesOf(f.fileName());
            if (!f.fileName().endsWith(QLatin1String(part.extension)) || (part.sixPieces ? pieces != 6 : (pieces < 3 || pieces > 5))) continue;
            ++files;
            bytes += f.size();
        }
        files = std::min(files, part.files);
        r.present += files;
        r.files += part.files - files;
        if (files < part.files) r.bytes += std::max<qint64>(part.bytes - bytes, qint64(part.files - files) * 1000); // at least a little per missing file
    }
    return r;
}

} // namespace tablebases

// ---------------------------------------------------------------- download

TablebaseDownloader& TablebaseDownloader::instance()
{
    static TablebaseDownloader* d = new TablebaseDownloader(qApp);
    return *d;
}

QUrl TablebaseDownloader::baseUrl() { return QUrl(QStringLiteral("https://tablebase.lichess.ovh/tables/standard/")); }

TablebaseDownloader::TablebaseDownloader(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_tick(new QTimer(this))
{
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, [this] {
        if (!m_running) return m_tick->stop();
        sample();
        emit progress(); // speed and remaining time move even when nothing arrives
    });
}

void TablebaseDownloader::resetSpeed()
{
    m_received = 0;
    m_samples.clear();
    m_tick->start();
}

void TablebaseDownloader::sample()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_samples.append({now, m_received});
    while (m_samples.size() > 2 && now - m_samples.first().first > 10000) m_samples.removeFirst();
}

qint64 TablebaseDownloader::bytesPerSecond() const
{
    if (!m_running || m_samples.size() < 2) return 0;
    const qint64 ms = m_samples.last().first - m_samples.first().first;
    return ms < 1500 ? 0 : (m_samples.last().second - m_samples.first().second) * 1000 / ms;
}

qint64 TablebaseDownloader::secondsLeft() const
{
    const qint64 rate = bytesPerSecond();
    if (rate <= 0 || m_total <= 0) return -1;
    return std::max<qint64>(0, m_total - m_done) / rate;
}

QString TablebaseDownloader::durationText(qint64 seconds)
{
    if (seconds < 0) return {};
    if (seconds >= 3600) return tr("%1 h %2 min").arg(seconds / 3600).arg(seconds % 3600 / 60, 2, 10, QLatin1Char('0'));
    if (seconds >= 60) return tr("%1 min %2 s").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return tr("%1 s").arg(seconds);
}

TablebaseDownloader::~TablebaseDownloader() { cancel(); }

void TablebaseDownloader::setStatus(const QString& s)
{
    m_status = s;
    emit progress();
}

void TablebaseDownloader::start(const QStringList& serverDirectories, const QString& directory)
{
    if (m_running) return;
    m_running = true;
    m_directory = directory;
    m_serverDirs = serverDirectories;
    resetSpeed();
    m_files.clear();
    m_sums.clear();
    m_total = m_done = 0;
    m_filesDone = m_failed = 0;
    QDir().mkpath(directory);
    fetchIndex();
}

void TablebaseDownloader::startFiles(const QList<File>& files, const QString& directory)
{
    if (m_running) return;
    m_running = true;
    m_directory = directory;
    m_files = files;
    resetSpeed();
    m_total = m_done = 0;
    m_filesDone = m_failed = 0;
    QDir().mkpath(directory);
    beginFiles();
}

static QNetworkRequest request(const QUrl& url)
{
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Hydra64"));
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return r;
}

// Server index (one page per folder) and list of checksums ("b2", BLAKE2b).
void TablebaseDownloader::fetchIndex()
{
    setStatus(tr("Lecture de la liste des tables sur le serveur…"));
    QStringList pages = m_serverDirs;
    pages << QStringLiteral("b2");
    for (const QString& page : pages) {
        const bool sums = page == QLatin1String("b2");
        QNetworkReply* reply = m_net->get(request(baseUrl().resolved(QUrl(sums ? page : page + QLatin1Char('/')))));
        m_indexReplies << reply;
        connect(reply, &QNetworkReply::finished, this, [this, reply, page, sums] {
            if (!m_indexReplies.removeOne(reply)) return; // cancelled
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError && !sums) return fail(tr("Liste des tables illisible (%1) : %2").arg(page, reply->errorString()));
            const QString text = QString::fromUtf8(reply->readAll());
            if (sums) {
                static const QRegularExpression line(QStringLiteral("^([0-9a-f]{128})\\s+(\\S+\\.rtb[wz])$"), QRegularExpression::MultilineOption);
                for (auto it = line.globalMatch(text); it.hasNext();) {
                    const auto m = it.next();
                    m_sums.insert(m.captured(2), m.captured(1).toLatin1());
                }
            } else {
                static const QRegularExpression row(QStringLiteral("<a href=\"([A-Za-z]+\\.rtb[wz])\">[^<]*</a>\\s+\\S+\\s+\\S+\\s+(\\d+)"));
                int found = 0;
                for (auto it = row.globalMatch(text); it.hasNext(); ++found) {
                    const auto m = it.next();
                    File f;
                    f.name = m.captured(1);
                    f.url = reply->url().resolved(QUrl(f.name));
                    f.size = m.captured(2).toLongLong();
                    m_files << f;
                }
                if (!found) return fail(tr("Aucune table dans le dossier « %1 » du serveur").arg(page));
            }
            if (m_indexReplies.isEmpty() && m_running) {
                for (File& f : m_files) f.blake2b = m_sums.value(f.name);
                std::sort(m_files.begin(), m_files.end(), [](const File& a, const File& b) { return a.size < b.size; }); // the small ones first: useful quickly
                beginFiles();
            }
        });
    }
}

void TablebaseDownloader::beginFiles()
{
    m_total = 0;
    for (const File& f : m_files) m_total += std::max<qint64>(0, f.size);
    m_next = 0;
    pump();
}

void TablebaseDownloader::pump()
{
    if (!m_running) return;
    constexpr int Parallel = 2; // two files at a time: the mirror is a volunteer service
    while (m_jobs.size() < Parallel && m_next < m_files.size()) {
        const int index = m_next++;
        const File& f = m_files[index];
        const QFileInfo existing(m_directory + QLatin1Char('/') + f.name);
        if (existing.exists() && (f.size < 0 || existing.size() == f.size)) { // already there
            m_done += std::max<qint64>(0, f.size);
            ++m_filesDone;
            continue;
        }
        auto* job = new Job;
        m_jobs << job;
        startJob(*job, index);
    }
    if (m_jobs.isEmpty() && m_next >= m_files.size()) {
        m_running = false;
        const bool ok = m_failed == 0;
        setStatus(ok ? tr("Terminé : %1 fichiers.").arg(m_filesDone) : tr("Terminé avec %1 fichier(s) en échec ; relancez pour les reprendre.").arg(m_failed));
        emit finished(ok, m_status);
        return;
    }
    emit progress();
}

void TablebaseDownloader::startJob(Job& job, int index)
{
    const File& f = m_files[index];
    job.file = index;
    ++job.attempts;
    const QString part = m_directory + QLatin1Char('/') + f.name + QStringLiteral(".part");
    job.out = std::make_unique<QFile>(part);
    job.hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Blake2b_512);
    qint64 have = 0;
    if (job.out->exists() && (f.size < 0 || job.out->size() < f.size) && job.out->open(QIODevice::ReadOnly)) {
        // Resumption: the checksum starts again from the bytes already received
        QByteArray chunk;
        while (!(chunk = job.out->read(1 << 20)).isEmpty()) job.hash->addData(chunk), have += chunk.size();
        job.out->close();
    } else {
        job.out->remove();
    }
    if (!job.out->open(QIODevice::WriteOnly | QIODevice::Append)) {
        fail(tr("Impossible d'écrire dans %1 : %2").arg(QDir::toNativeSeparators(m_directory), job.out->errorString()));
        return;
    }
    QNetworkRequest r = request(f.url);
    if (have > 0) r.setRawHeader("Range", QByteArrayLiteral("bytes=") + QByteArray::number(have) + '-');
    m_done += have;
    job.reply = m_net->get(r);
    Job* j = &job;
    connect(job.reply, &QNetworkReply::readyRead, this, [this, j, have] {
        if (!j->reply) return;
        // Resumption requested but the server returns the whole file: we start from scratch
        if (have > 0 && j->out->pos() == have && j->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 206) {
            j->out->resize(0);
            j->out->seek(0);
            j->hash->reset();
            m_done -= have;
        }
        const QByteArray data = j->reply->readAll();
        if (j->out->write(data) != data.size()) {
            fail(tr("Disque plein ou illisible : %1").arg(j->out->errorString()));
            return;
        }
        j->hash->addData(data);
        m_done += data.size();
        m_received += data.size();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastEmit >= 250) {
            m_lastEmit = now;
            m_status = tr("%1 sur %2 fichiers").arg(m_filesDone).arg(m_files.size());
            emit progress();
        }
    });
    connect(job.reply, &QNetworkReply::finished, this, [this, j] { jobFinished(j); });
}

void TablebaseDownloader::jobFinished(Job* job)
{
    if (!m_jobs.contains(job) || !job->reply) return;
    QNetworkReply* reply = job->reply;
    job->reply = nullptr;
    reply->deleteLater();
    const File f = m_files[job->file];
    const qint64 written = job->out->size();
    job->out->close();
    const bool netOk = reply->error() == QNetworkReply::NoError;
    const bool sizeOk = f.size < 0 || written == f.size;
    const bool sumOk = f.blake2b.isEmpty() || job->hash->result().toHex() == f.blake2b;
    const QString part = job->out->fileName();
    bool done = false;
    if (netOk && sizeOk && sumOk) {
        const QString target = m_directory + QLatin1Char('/') + f.name;
        QFile::remove(target);
        done = QFile::rename(part, target);
    } else if (netOk && sizeOk && !sumOk) {
        QFile::remove(part); // complete but wrong file: nothing to resume
        m_done -= written;
    }
    if (done) {
        ++m_filesDone;
    } else if (job->attempts < 3 && m_running) {
        if (QFileInfo::exists(part)) m_done -= QFileInfo(part).size(); // recounted on resumption
        startJob(*job, job->file);
        return;
    } else {
        ++m_failed;
    }
    m_jobs.removeOne(job);
    delete job;
    pump();
}

void TablebaseDownloader::fail(const QString& message)
{
    if (!m_running) return;
    cancel();
    m_status = message;
    emit progress();
    emit finished(false, message);
}

void TablebaseDownloader::cancel()
{
    const bool was = m_running;
    m_running = false;
    for (QNetworkReply* r : std::as_const(m_indexReplies)) r->abort(), r->deleteLater();
    m_indexReplies.clear();
    for (Job* job : std::as_const(m_jobs)) {
        if (job->reply) {
            QNetworkReply* r = job->reply;
            job->reply = nullptr;
            r->abort();
            r->deleteLater();
        }
        if (job->out) job->out->close();
        delete job;
    }
    m_jobs.clear();
    if (was) setStatus(tr("Interrompu : les fichiers commencés seront repris."));
}

// ---------------------------------------------------------------- Preferences page

static QLabel* faint(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName("Faint");
    l->setWordWrap(true);
    return l;
}

static QLabel* heading(const QString& text)
{
    auto* l = new QLabel(text.toUpper());
    l->setObjectName("Faint");
    QFont f = l->font();
    f.setPixelSize(10);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    l->setFont(f);
    return l;
}

TablebasePage::TablebasePage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(8);

    // Sources
    layout->addWidget(heading(tr("Tables de finales (Syzygy)")));
    layout->addWidget(faint(tr("Dossiers de fichiers .rtbw / .rtbz, ou images de disque .iso (DVD « Endgame Turbo » de ChessBase, par exemple) : "
                               "une image est lue telle quelle, sans rien copier. Un dossier qui contient des images les reconnaît.")));
    m_sources = new QListWidget;
    m_sources->setObjectName("TablebaseSources");
    m_sources->setMaximumHeight(110);
    for (const QString& s : tablebases::sources()) m_sources->addItem(s);
    auto* addFolder = new QPushButton(theme::icon("open"), tr("Ajouter un dossier…"));
    auto* addImage = new QPushButton(tr("Ajouter une image ISO…"));
    auto* remove = new QPushButton(theme::icon("trash"), tr("Retirer"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(addFolder);
    buttons->addWidget(addImage);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    m_summary = new QLabel;
    m_summary->setObjectName("TablebaseSummary");
    m_summary->setWordWrap(true);
    layout->addWidget(m_sources);
    layout->addLayout(buttons);
    layout->addWidget(m_summary);
    connect(addFolder, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Dossier de tables de finales, ou d'images ISO"), userdirs::startDir(userdirs::Kind::Tablebase));
        if (d.isEmpty()) return;
        userdirs::remember(userdirs::Kind::Tablebase, d);
        addSource(d);
    });
    connect(addImage, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Images de disque contenant des tables de finales"), userdirs::startDir(userdirs::Kind::Tablebase), tr("Images de disque (*.iso)"));
        if (!files.isEmpty()) userdirs::remember(userdirs::Kind::Tablebase, files.first());
        for (const QString& f : files) addSource(f);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        qDeleteAll(m_sources->selectedItems());
        refreshSummary();
    });

    // Engines
    layout->addSpacing(12);
    layout->addWidget(heading(tr("Utilisation par les moteurs")));
    m_analysis = new QCheckBox(tr("En analyse"));
    m_analysis->setToolTip(tr("Moteurs spectateurs, annotation, analyse approfondie, évaluation automatique"));
    m_play = new QCheckBox(tr("Quand vous jouez contre un moteur à pleine force"));
    m_tournament = new QCheckBox(tr("Dans les tournois de moteurs"));
    m_analysis->setChecked(tablebases::engineUse(UciEngine::TablebaseRole::Analysis));
    m_play->setChecked(tablebases::engineUse(UciEngine::TablebaseRole::Play));
    m_tournament->setChecked(tablebases::engineUse(UciEngine::TablebaseRole::Tournament));
    layout->addWidget(m_analysis);
    layout->addWidget(m_play);
    layout->addWidget(m_tournament);
    layout->addWidget(faint(tr("Le dossier est transmis aux moteurs qui ont l'option « SyzygyPath », dès leur prochaine recherche. Un adversaire de niveau limité "
                               "ne reçoit jamais les tables. Un « SyzygyPath » réglé à la main dans la configuration d'un moteur passe avant.")));

    // Online service
    layout->addSpacing(12);
    layout->addWidget(heading(tr("Service en ligne")));
    m_online = new QCheckBox(tr("Interroger aussi le service en ligne de Lichess"));
    m_online->setChecked(QSettings().value("tablebase/online", false).toBool());
    layout->addWidget(m_online);
    layout->addWidget(faint(tr("La position affichée (FEN) est envoyée à tablebase.lichess.ovh quand elle a 7 pièces au plus et que les tables locales ne la couvrent pas. "
                               "Rien d'autre n'est envoyé. Ce réglage ne concerne que le panneau Finales, pas les moteurs.")));

    // Download
    layout->addSpacing(12);
    layout->addWidget(heading(tr("Télécharger des tables")));
    m_set = new QComboBox;
    m_set->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); // the list must not widen the page
    m_set->setMinimumContentsLength(24);
    const QLocale loc;
    for (const tablebases::Set& s : tablebases::sets()) m_set->addItem(tr("%1 — environ %2").arg(s.label, loc.formattedDataSize(s.approxBytes, 0, QLocale::DataSizeSIFormat)));
    m_directory = new QLineEdit(QSettings().value("tablebase/downloadDir", tablebases::defaultDownloadDir()).toString());
    auto* browse = new QPushButton(theme::icon("open"), QString());
    auto* dirRow = new QHBoxLayout;
    dirRow->addWidget(m_directory, 1);
    dirRow->addWidget(browse);
    m_space = new QLabel;
    m_space->setWordWrap(true);
    m_download = new QPushButton;
    m_progress = new QProgressBar;
    m_progress->setRange(0, 1000);
    m_download->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_downloadStatus = faint(QString());
    auto* form = new QFormLayout;
    form->addRow(tr("Jeu"), m_set);
    form->addRow(tr("Dossier"), dirRow);
    layout->addLayout(form);
    layout->addWidget(m_space);
    auto* dlRow = new QHBoxLayout;
    dlRow->addWidget(m_download);
    dlRow->addWidget(m_progress, 1);
    dlRow->addStretch(0);
    layout->addLayout(dlRow);
    layout->addWidget(m_downloadStatus);
    layout->addWidget(faint(tr("« Résultats seuls » suffit à un moteur pendant sa recherche ; les distances servent au panneau Finales et au moteur pour conclure sans tourner en rond.")));
    layout->addWidget(faint(tr("Source : miroir public de Lichess (tablebase.lichess.ovh). Les tables déjà présentes dans le dossier ne sont pas retéléchargées, un fichier "
                               "interrompu est repris, et chaque fichier est contrôlé (taille et empreinte). Le téléchargement continue si vous fermez cette fenêtre, "
                               "et s'arrête à la fermeture de Hydra64. Un disque rapide (SSD) est conseillé pour les tables à 6 pièces.")));
    layout->addStretch(1);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Dossier où télécharger les tables"), m_directory->text());
        if (!d.isEmpty()) m_directory->setText(d);
    });
    connect(m_set, &QComboBox::currentIndexChanged, this, [this] { refreshDownload(); });
    connect(m_directory, &QLineEdit::textChanged, this, [this] { refreshDownload(); });
    TablebaseDownloader& dl = TablebaseDownloader::instance();
    connect(&dl, &TablebaseDownloader::progress, this, [this] { refreshDownload(); });
    connect(&dl, &TablebaseDownloader::finished, this, [this](bool, const QString&) {
        // Downloaded tables become a source
        const QString dir = TablebaseDownloader::instance().directory();
        if (!shownSources().contains(dir)) m_sources->addItem(dir);
        refreshSummary();
        refreshDownload();
    });
    connect(m_download, &QPushButton::clicked, this, [this] {
        TablebaseDownloader& d = TablebaseDownloader::instance();
        if (d.isRunning()) {
            d.cancel();
            refreshDownload();
            return;
        }
        const tablebases::Set set = tablebases::sets().value(m_set->currentIndex());
        const QString dir = m_directory->text().trimmed();
        if (dir.isEmpty()) return;
        QDir().mkpath(dir);
        const QStorageInfo storage(dir);
        const QLocale loc;
        const qint64 needed = tablebases::remaining(set, dir).bytes; // what is missing, not the whole set
        if (storage.isValid() && storage.bytesAvailable() < needed + (1LL << 30)) {
            QMessageBox::warning(this, tr("Tables de finales"), tr("Place insuffisante : il reste environ %1 à télécharger, et %2 de libre sur ce disque.")
                                                                    .arg(loc.formattedDataSize(needed, 0, QLocale::DataSizeSIFormat), loc.formattedDataSize(storage.bytesAvailable(), 1, QLocale::DataSizeSIFormat)));
            return;
        }
        if (needed > 5'000'000'000LL
            && QMessageBox::question(this, tr("Tables de finales"), tr("Télécharger environ %1 dans %2 ?\n\nCela peut prendre plusieurs heures. Vous pouvez interrompre et reprendre plus tard.")
                                                                        .arg(loc.formattedDataSize(needed, 0, QLocale::DataSizeSIFormat), QDir::toNativeSeparators(dir)))
                   != QMessageBox::Yes)
            return;
        QSettings().setValue("tablebase/downloadDir", dir);
        d.start(set.directories, dir);
        refreshDownload();
    });
    refreshSummary();
    refreshDownload();
}

QStringList TablebasePage::shownSources() const
{
    QStringList list;
    for (int i = 0; i < m_sources->count(); ++i) list << m_sources->item(i)->text();
    return list;
}

void TablebasePage::addSource(const QString& path)
{
    if (path.isEmpty() || shownSources().contains(path)) return;
    QSettings().setValue("lastDir", QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath());
    m_sources->addItem(path);
    refreshSummary();
}

QString TablebasePage::summaryText() const { return m_summary->text(); }
QString TablebasePage::downloadStatusText() const { return m_downloadStatus->text(); }
QString TablebasePage::spaceText() const { return m_space->text(); }
void TablebasePage::setDownloadChoice(int set, const QString& directory)
{
    m_set->setCurrentIndex(set);
    m_directory->setText(directory);
    refreshDownload();
}

void TablebasePage::refreshSummary()
{
    const tablebases::Summary s = tablebases::scan(shownSources());
    QString text = s.text();
    if (!s.images.isEmpty()) {
        text += QLatin1Char('\n') + (tablebases::imagesSupported() ? tr("%n image(s) ISO lue(s) directement.", nullptr, int(s.images.size()))
                                                                    : tr("Les images ISO ne sont pas utilisables : le programme hydra64-tablefs (FUSE) manque."));
    }
    for (const QString& u : s.unreadable) text += QLatin1Char('\n') + tr("Image illisible — %1").arg(u);
    m_summary->setText(text);
}

void TablebasePage::refreshDownload()
{
    TablebaseDownloader& d = TablebaseDownloader::instance();
    const QLocale loc;
    const bool running = d.isRunning();
    m_download->setText(running ? tr("Interrompre") : tr("Télécharger"));
    m_download->setEnabled(true);
    m_set->setEnabled(!running);
    m_directory->setEnabled(!running);
    m_progress->setVisible(running || d.totalBytes() > 0);
    if (d.totalBytes() > 0) {
        m_progress->setValue(int(d.doneBytes() * 1000 / d.totalBytes()));
        QString text = tr("%1 — %2 sur %3").arg(d.status(), loc.formattedDataSize(d.doneBytes(), 1, QLocale::DataSizeSIFormat), loc.formattedDataSize(d.totalBytes(), 1, QLocale::DataSizeSIFormat));
        if (running) {
            const qint64 rate = d.bytesPerSecond(), left = d.secondsLeft();
            if (rate > 0) text += tr(" · %1/s").arg(loc.formattedDataSize(rate, 1, QLocale::DataSizeSIFormat));
            if (left >= 0) text += tr(" · reste environ %1").arg(TablebaseDownloader::durationText(left));
            else if (d.filesTotal() > 0) text += tr(" · débit en cours de mesure…");
        }
        m_downloadStatus->setText(text);
    } else {
        m_downloadStatus->setText(d.status());
    }
    // Room needed and free room
    const tablebases::Set set = tablebases::sets().value(m_set->currentIndex());
    const tablebases::Remaining left = tablebases::remaining(set, m_directory->text().trimmed());
    QString dir = m_directory->text().trimmed();
    while (!dir.isEmpty() && !QFileInfo::exists(dir)) dir = QFileInfo(dir).absolutePath() == dir ? QString() : QFileInfo(dir).absolutePath(); // the folder may not exist yet
    const QStorageInfo storage(dir.isEmpty() ? QDir::homePath() : dir);
    const bool enough = !storage.isValid() || storage.bytesAvailable() >= left.bytes + (1LL << 30);
    const QString free = storage.isValid() ? loc.formattedDataSize(storage.bytesAvailable(), 1, QLocale::DataSizeSIFormat) : tr("inconnue");
    if (left.complete()) {
        m_space->setText(tr("Ce jeu est déjà complet dans ce dossier : rien à télécharger."));
    } else if (left.present > 0) {
        m_space->setText(tr("Déjà dans ce dossier : %1 fichiers de ce jeu. Reste à télécharger : environ %2 (%3 fichiers). Place libre sur ce disque : %4.%5")
                             .arg(left.present).arg(loc.formattedDataSize(left.bytes, 0, QLocale::DataSizeSIFormat)).arg(left.files).arg(free, enough ? QString() : tr(" Insuffisant.")));
    } else {
        m_space->setText(tr("Place nécessaire : environ %1. Place libre sur ce disque : %2.%3").arg(loc.formattedDataSize(left.bytes, 0, QLocale::DataSizeSIFormat), free, enough ? QString() : tr(" Insuffisant.")));
    }
    if (!running) m_download->setEnabled(!left.complete());
    m_space->setStyleSheet(enough ? QString() : QStringLiteral("color:#e08a00;"));
}

void TablebasePage::commit()
{
    QSettings().setValue("tablebase/online", m_online->isChecked());
    tablebases::setEngineUse(UciEngine::TablebaseRole::Analysis, m_analysis->isChecked());
    tablebases::setEngineUse(UciEngine::TablebaseRole::Play, m_play->isChecked());
    tablebases::setEngineUse(UciEngine::TablebaseRole::Tournament, m_tournament->isChecked());
    tablebases::setSources(shownSources());
    tablebases::apply();
}

#pragma once

#include "chess/game.h"
#include "chess/treebook.h"
#include "deep/deepplanner.h"
#include "engine/uciengine.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

class QTimer;

// Deep analysis (in the manner of Aquarium's IDeA): projects saved in
// ~/Documents/hydra64/analyses (.hy64deep), engine pool (several instances), execution
// of the tasks of each active project according to its CPU share, automatic saving.
class DeepAnalysis : public QObject {
    Q_OBJECT
public:
    // Pool engine: instances (one task each), threads and memory per instance.
    struct EngineSlot {
        QString engineId;
        int count = 1;
        bool enabled = true;
        int threads = 1;
        int hashMb = 64;
    };
    enum class Status { Inactive, Active, Share }; // Share: CPU share (`share` %)
    enum class Activity { Idle, Analysing, Generating, Minimizing, Waiting };

    struct ProjectInfo {
        QString path;
        QString name;
        Status status = Status::Active;
        int share = 100;
        QDateTime created, modified;
        QString master; // master tree (another project)
        qint64 totalTasks = 0, totalCpuMs = 0;
        qint64 sessionTasks = 0, sessionCpuMs = 0;
        Activity activity = Activity::Idle;
        qint64 stageStartMs = 0; // start of the step (session clock)
        int stageDone = 0;       // tasks finished in the step
        bool dirty = false;
    };

    // Task running on an instance
    struct Running {
        int project = -1;
        deep::Task task;
        EngineLine line;   // last main line received
        qint64 elapsedMs = 0;
        QString engineName;
        int slot = -1;
    };

    static QString directory();

    explicit DeepAnalysis(QObject* parent = nullptr);
    ~DeepAnalysis() override;

    // Projects
    void loadAll(); // all the .hy64deep of the folder
    int count() const { return int(m_projects.size()); }
    deep::Project& project(int i) { return *m_projects[size_t(i)].project; }
    const deep::Project& project(int i) const { return *m_projects[size_t(i)].project; }
    ProjectInfo& info(int i) { return m_projects[size_t(i)].info; }
    const ProjectInfo& info(int i) const { return m_projects[size_t(i)].info; }
    int indexOf(const QString& path) const;
    int defaultProject() const { return m_default; }
    void setDefaultProject(int i);
    // New project (file created); `master`: results imported from this master tree.
    int createProject(const QString& name, const chess::Position& root, const deep::Options& options, const QString& master, QString* error);
    bool deleteProject(int i, QString* error);
    bool saveProject(int i, QString* error = nullptr);
    void saveAll();
    void setStatus(int i, Status status, int share = 100);
    void markDirty(int i);
    // Results of the project added to its master tree (file updated).
    bool mergeIntoMaster(int i, QString* error);

    // Engines (remembered settings; applied at the next start)
    QList<EngineSlot> engines() const;
    void setEngines(const QList<EngineSlot>& engineSlots);
    static QList<EngineSlot> defaultEngines();

    bool start(QString* error);
    void stop();
    bool isRunning() const { return m_running; }
    qint64 clockMs() const { return m_clock.elapsed(); } // session clock (start of the steps)
    QList<Running> running() const;
    int runningCount(int project) const;

    // Tasks
    uint64_t addTask(int project, deep::Task t);
    // Path to `node` of the game from a root of the project (disabled root added
    // at the position if none fits). False if the node is invalid.
    bool pathTo(int project, const chess::Game& game, const chess::Node* node, int& root, std::vector<chess::Move>& path);
    void finishTask(uint64_t id);       // running task: stopped, its result is kept
    void deleteTask(int project, uint64_t id);
    void makeInfinite(int project, uint64_t id);
    void minimaxNow(int project);
    // Line of a spectator engine (analysis of `node`): sent to the default project if it asks for it.
    void spectatorLine(const chess::Game& game, const chess::Node* node, const EngineLine& line);

    // Exports
    bool exportPgn(int project, const QString& path, QString* error) const;
    bool exportBook(int project, const QString& path, chess::treebook::TreeFormat format, QString* error) const;

signals:
    void treeChanged(int project);
    void queueChanged(int project);
    void projectsChanged();
    void runningChanged(bool running);
    void taskProgress(uint64_t id); // line received for a running task

private:
    struct Entry {
        std::unique_ptr<deep::Project> project;
        ProjectInfo info;
    };
    struct Worker {
        UciEngine* engine = nullptr;
        int slot = -1;
        uint8_t engineIndex = 0;
        bool ready = false;
        bool busy = false;
        bool discard = false;   // task deleted: result ignored
        bool infinite = false;  // no limit: the user ends it
        bool stopping = false;
        int project = -1;
        deep::Task task;
        QString fen;
        EngineLine line;
        QElapsedTimer timer;
        int depthAtTarget = -1; // "wait for the next depth"
        QString name;
    };

    void readMeta(Entry& e);
    void writeMeta(Entry& e);
    void schedule();
    bool assign(Worker& w);
    void onInfo(Worker& w, const EngineLine& line);
    void onBestMove(Worker& w, const QString& uci, const QString& fen);
    void checkLimits();
    deep::Quality qualityFor(const Worker& w) const;
    void shutdownWorkers();

    std::vector<Entry> m_projects;
    std::vector<std::unique_ptr<Worker>> m_workers;
    int m_default = -1;
    bool m_running = false;
    QElapsedTimer m_clock;
    QTimer* m_limits;   // check of the tasks' time and depth
    QTimer* m_autosave; // every minute
    QString m_lastSpectator; // last line sent (position + depth)
};

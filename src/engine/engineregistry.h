#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

// Option declared by a UCI engine ("option name … type …").
struct UciOption {
    QString name;
    QString type; // check, spin, combo, string, button
    QString defaultValue;
    qint64 min = 0;
    qint64 max = 0;
    QStringList choices; // combo

    static bool parse(const QString& line, UciOption& out);
};

// A registered engine: executable, declared options and values changed by the user.
struct EngineConfig {
    QString id;
    QString name;        // displayed name, editable
    QString path;
    QString idName;      // "id name" returned by the engine
    QString author;      // "id author"
    QList<UciOption> options;
    QMap<QString, QString> values; // only the values that differ from the default
    bool verified = false;

    bool isValid() const { return !path.isEmpty(); }
    const UciOption* option(const QString& name) const;
    QString value(const QString& name) const; // effective value (changed or default)
};

// List of engines, saved in <AppData>/engines.json.
class EngineRegistry : public QObject {
    Q_OBJECT
public:
    static EngineRegistry& instance();

    const QList<EngineConfig>& engines() const { return m_engines; }
    EngineConfig engine(const QString& id) const;
    // Fallback engine, when the remembered engine is unavailable: the built-in Stockfish,
    // otherwise the first of the list.
    EngineConfig fallbackEngine() const;

    void add(const EngineConfig& cfg);
    void update(const EngineConfig& cfg);
    void remove(const QString& id);

    // Starts the engine, reads its identity and options, then quits it (blocking, ≤ 5 s).
    static bool probe(const QString& path, EngineConfig& cfg, QString* error);
    // Creates a complete configuration for an executable, with sensible starting settings.
    static bool create(const QString& path, EngineConfig& cfg, QString* error);

    QString filePath() const;

    // Built-in Stockfish (built next to the executable).
    static QString builtinStockfishPath();
    // Program shipped next to the executable ("stockfish", "shuffle"), with ".exe" on Windows.
    static QString besideApplication(const QString& relative);
    // One of the two built-in engines, Stockfish and Shuffle: always in the list, cannot be removed,
    // executable fixed. They stay next to the program — never in the user's uci_engines folder, where
    // they could be deleted by mistake.
    static bool isBuiltin(const EngineConfig& cfg);
    // "stockfish" or "shuffle" for a built-in engine; empty for an engine added by the user.
    static QString bundledKind(const EngineConfig& cfg);
    static QString bundledPath(const QString& kind); // executable of a shipped engine, next to the program

signals:
    void changed();

private:
    EngineRegistry();
    void load();
    void save() const;
    void autoDetect();
    void registerBundled(); // the built-in engines, Stockfish and Shuffle: always listed

    QList<EngineConfig> m_engines;
};

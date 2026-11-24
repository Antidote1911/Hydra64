#pragma once

#include "engine/engineregistry.h"

#include <QMap>
#include <QObject>
#include <QString>

// Settings of an engine specific to a tab (Opponent of the Play page, Spectator 1, 2…).
// The engine manager keeps the settings of the engine itself; each tab inherits them and can
// change some for itself alone (Threads, Hash…), with no effect on the other tabs using
// the same engine. Saved per tab and per engine: changing engine in a tab then coming
// back to it finds its settings again.
namespace tabengine {

inline constexpr const char* PlayTab = "play";         // opponent engine of the Play page
inline constexpr const char* AnalysisTab = "analysis"; // first engine of the Analysis page

// Values changed in this tab for this engine (option name → value).
QMap<QString, QString> overrides(const QString& tab, const QString& engineId);
// Changes an option for this tab; going back to the engine's value removes the change.
void setValue(const QString& tab, const EngineConfig& engine, const QString& option, const QString& value);
void clear(const QString& tab, const QString& engineId); // the tab takes the engine's settings again
void forget(const QString& tab);                         // tab deleted: all its settings
// Configuration of the engine as the tab uses it: engine settings + those of the tab.
EngineConfig effective(const QString& tab, EngineConfig engine);

class Notifier : public QObject {
    Q_OBJECT
public:
    static Notifier& instance();
signals:
    void changed(const QString& tab); // the settings of this tab have changed
};

} // namespace tabengine

#pragma once

#include "engine/engineregistry.h"

#include <QDialog>
#include <QMap>

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace engineui {
// Editor of a UCI option (number, checkbox, list, text, path); `changed` receives the new value.
QWidget* optionEditor(const UciOption& o, const QString& value, QWidget* dialogParent, std::function<void(const QString&)> changed);
}

// Engine management: add, remove, check
// and set each one's UCI options (the engine's "personality").
class EngineManagerDialog : public QDialog {
    Q_OBJECT
public:
    // `selectId`: engine shown on opening (otherwise the first).
    explicit EngineManagerDialog(QWidget* parent = nullptr, const QString& selectId = {});

private:
    void rebuildList(const QString& selectId = {});
    void showEngine(const QString& id);
    static QString aboutText(const EngineConfig& engine); // description of a shipped engine (empty for the others)
    void rebuildOptions();
    void applyFilter();
    void setValue(const QString& option, const QString& value);
    void commit(); // saves m_current into the registry

    void addEngine();
    void removeEngine();
    void verifyEngine();
    void changePath();
    void resetOptions();
    void setStatus(bool ok, const QString& text);

    EngineConfig m_current;

    QListWidget* m_list;
    QPushButton* m_remove;
    QPushButton* m_browse;
    QStackedWidget* m_stack;
    QLineEdit* m_name;
    QLabel* m_identity;
    QLabel* m_about; // engine shipped with Hydra64: what it is and what sets it apart
    QLineEdit* m_path;
    QLabel* m_status;
    QLineEdit* m_filter;
    QTableWidget* m_options;
    class QLabel* m_strengthNote;
};

// Settings of an engine for a single tab (Opponent, Spectator 1…), opened by the tab's CPU
// button. Each option starts from the engine's value (Engine management); changing it here only
// applies to this tab, the other tabs using the same engine do not move.
class EngineTabDialog : public QDialog {
    Q_OBJECT
public:
    // `tab`: key of the tab (tabengine); `tabTitle`: its displayed name; `engineId`: its engine.
    EngineTabDialog(const QString& tab, const QString& tabTitle, const QString& engineId, QWidget* parent = nullptr);
    // Options the game in progress sets itself (Opponent tab): shown with the game's
    // value, greyed out (null value: option with no effect during the game).
    void setGameOptions(const QMap<QString, QString>& values);
    bool manageEnginesRequested() const { return m_manage; } // "Engine management…" button

private:
    void rebuild();
    QString m_tab;
    EngineConfig m_engine; // engine settings (reference)
    QMap<QString, QString> m_gameOptions;
    QTableWidget* m_options;
    QPushButton* m_reset;
    bool m_manage = false;
};

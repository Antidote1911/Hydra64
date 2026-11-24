#pragma once

#include "engine/engineregistry.h"

#include <QIcon>
#include <QString>
#include <QWidget>

#include <functional>

class QMenu;
class QLabel;
class QToolButton;

// Common elements of the engine tabs (Fritz-style "modules"): Play page and Analysis page.
namespace module {

QToolButton* button(const QString& icon, const QString& text, const QString& tip);
QToolButton* nameButton(const QString& tip);  // engine name, in bold
QLabel* stat();                                // statistics cell (eval, depth, speed)
// State of an engine on its tab's icon: green, it is ready; red, it is working.
QIcon statusIcon(bool running);
QString cpuText(const EngineConfig& engine);   // "16 CPUs" (Threads option)
// Can the engine display several lines (UCI MultiPV option)? Unknown options
// (engine outside the registry): we assume so.
bool supportsMultiPv(const EngineConfig& engine);
// Fills `menu` with the configured engines, each time it opens; a choice calls `pick(id)`.
void engineChoiceMenu(QMenu* menu, std::function<void(const QString& id)> pick);
// Number of lines displayed (1 to 10): "−" and "+" buttons on either side of the number.
class VariantsStepper : public QWidget {
    Q_OBJECT
public:
    static constexpr int Min = 1, Max = 10;
    explicit VariantsStepper(QWidget* parent = nullptr);
    int value() const { return m_value; }
    void setValue(int n); // without emitting changed()

signals:
    void changed(int n); // change by the user

private:
    void step(int delta);
    void refresh();
    int m_value = 1;
    QToolButton* m_minus;
    QToolButton* m_plus;
    QLabel* m_count;
};

} // namespace module

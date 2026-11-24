#include "modulewidgets.h"
#include "theme.h"

#include <QHBoxLayout>
#include <QCoreApplication>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QMenu>
#include <QThread>
#include <QToolButton>

namespace module {

void engineChoiceMenu(QMenu* menu, std::function<void(const QString& id)> pick)
{
    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, pick] {
        menu->clear();
        for (const EngineConfig& e : EngineRegistry::instance().engines()) {
            const QString id = e.id;
            QObject::connect(menu->addAction(e.name), &QAction::triggered, menu, [pick, id] { pick(id); });
        }
        if (menu->isEmpty())
            menu->addAction(QCoreApplication::translate("module", "Aucun moteur configuré"))->setEnabled(false);
    });
}

QToolButton* button(const QString& icon, const QString& text, const QString& tip)
{
    auto* b = new QToolButton;
    b->setObjectName("ModuleButton");
    if (!icon.isEmpty()) b->setIcon(theme::icon(icon));
    b->setText(text);
    b->setIconSize(QSize(16, 16));
    b->setToolButtonStyle(icon.isEmpty() ? Qt::ToolButtonTextOnly : text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QToolButton* nameButton(const QString& tip)
{
    QToolButton* b = button({}, {}, tip);
    b->setObjectName("ModuleName");
    return b;
}

QLabel* stat()
{
    auto* l = new QLabel;
    l->setObjectName("ModuleStat");
    l->setAlignment(Qt::AlignCenter);
    l->setMinimumWidth(70);
    return l;
}

QIcon statusIcon(bool running) { return theme::icon(QStringLiteral("engine"), running ? theme::Bad : theme::Good); }

VariantsStepper::VariantsStepper(QWidget* parent)
    : QWidget(parent)
{
    m_minus = button({}, QStringLiteral("−"), QCoreApplication::translate("module", "Une variante de moins"));
    m_plus = button({}, QStringLiteral("+"), QCoreApplication::translate("module", "Une variante de plus"));
    m_count = new QLabel;
    m_count->setAlignment(Qt::AlignCenter);
    m_count->setMinimumWidth(20);
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(2);
    l->addWidget(m_minus);
    l->addWidget(m_count);
    l->addWidget(m_plus);
    setToolTip(QCoreApplication::translate("module", "Nombre de variantes affichées par le moteur"));
    connect(m_minus, &QToolButton::clicked, this, [this] { step(-1); });
    connect(m_plus, &QToolButton::clicked, this, [this] { step(+1); });
    refresh();
}

void VariantsStepper::setValue(int n)
{
    m_value = std::clamp(n, Min, Max);
    refresh();
}

void VariantsStepper::step(int delta)
{
    const int n = std::clamp(m_value + delta, Min, Max);
    if (n == m_value) return;
    m_value = n;
    refresh();
    emit changed(n);
}

void VariantsStepper::refresh()
{
    m_count->setText(QString::number(m_value));
    m_minus->setEnabled(m_value > Min);
    m_plus->setEnabled(m_value < Max);
}

QString cpuText(const EngineConfig& e)
{
    int threads = e.value(QStringLiteral("Threads")).toInt();
    if (threads <= 0) threads = e.isValid() && e.id.isEmpty() ? std::max(1, QThread::idealThreadCount() / 2) : 1;
    return threads > 1 ? QCoreApplication::translate("module", "%1 CPUs").arg(threads) : QCoreApplication::translate("module", "%1 CPU").arg(threads);
}

bool supportsMultiPv(const EngineConfig& e)
{
    if (e.options.isEmpty()) return true;
    for (const UciOption& o : e.options)
        if (o.name.compare(QLatin1String("MultiPV"), Qt::CaseInsensitive) == 0) return true;
    return false;
}

} // namespace module

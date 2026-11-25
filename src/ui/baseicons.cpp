#include "baseicons.h"

#include "theme.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace baseicons {

namespace {
const char* SettingsKey = "gameList/baseIcons";

// Databases are recognised by their full path (the same file reopened keeps its icon).
QString keyOf(const QString& path) { return QFileInfo(path).absoluteFilePath(); }

// Hydra64's own drawing, next to Lucide's: the head of the logo (resources/hydra64-small.svg) as a
// line drawing. In the "Collections" theme, beside the plain database.
const char* HydraBody = R"(<path d="M 12 2.5 L 13.7 8.6 L 17.4 4.4 L 16.9 10.2 L 21.5 8.8 L 18 13.4 L 15.9 16.2 L 15.3 20 L 13.5 21.6 L 12 22 L 10.5 21.6 L 8.7 20 L 8.1 16.2 L 6 13.4 L 2.5 8.8 L 7.1 10.2 L 6.6 4.4 L 10.3 8.6 Z"/><path d="M 13.5 13.7 L 15.9 12.4"/><path d="M 10.5 13.7 L 8.1 12.4"/>)";
} // namespace

const std::vector<Entry>& entries()
{
    static const std::vector<Entry> all = {
        {"hydra64", 4, HydraBody},
#include "baseicons.inc"
    };
    return all;
}

QStringList themes()
{
    return {QCoreApplication::translate("baseicons", "Échecs"),      QCoreApplication::translate("baseicons", "Étude"),
            QCoreApplication::translate("baseicons", "Joueurs"),     QCoreApplication::translate("baseicons", "Tournois"),
            QCoreApplication::translate("baseicons", "Collections"), QCoreApplication::translate("baseicons", "Symboles")};
}

QIcon icon(const QString& id)
{
    static QHash<QString, QIcon> cache;
    if (auto it = cache.find(id); it != cache.end()) return *it;
    const Entry* found = nullptr;
    const Entry* fallback = nullptr;
    for (const Entry& e : entries()) {
        if (id == QLatin1String(e.id)) found = &e;
        if (QLatin1String(DefaultId) == QLatin1String(e.id)) fallback = &e;
    }
    if (!found) found = fallback;
    const QIcon ic = found ? theme::drawnIcon(QString::fromLatin1(found->body)) : theme::icon("database");
    cache.insert(id, ic);
    return ic;
}

QString chosen(const QString& path)
{
    const QString id = QSettings().value(SettingsKey).toMap().value(keyOf(path)).toString();
    return id.isEmpty() ? QString::fromLatin1(DefaultId) : id;
}

void setChosen(const QString& path, const QString& id)
{
    QSettings s;
    QVariantMap map = s.value(SettingsKey).toMap();
    if (id.isEmpty() || id == QLatin1String(DefaultId)) map.remove(keyOf(path));
    else map.insert(keyOf(path), id);
    s.setValue(SettingsKey, map);
}

bool choose(QWidget* parent, const QString& baseName, QString* id)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("baseicons", "Icône de « %1 »").arg(baseName));
    dialog.resize(640, 440);

    // Themes on the left, icons of the theme on the right.
    auto* themeList = new QListWidget;
    themeList->setObjectName("DatabaseList");
    themeList->setFixedWidth(150);
    themeList->addItems(themes());
    auto* grid = new QListWidget;
    grid->setObjectName("IconGrid");
    grid->setViewMode(QListView::IconMode);
    grid->setMovement(QListView::Static);
    grid->setResizeMode(QListView::Adjust);
    grid->setIconSize(QSize(32, 32));
    grid->setGridSize(QSize(58, 58));
    grid->setUniformItemSizes(true);
    grid->setSelectionMode(QAbstractItemView::SingleSelection);

    auto fill = [grid, id](int theme) {
        grid->clear();
        for (const Entry& e : entries()) {
            if (e.theme != theme) continue;
            const QString name = QString::fromLatin1(e.id);
            auto* item = new QListWidgetItem(icon(name), QString());
            item->setData(Qt::UserRole, name);
            item->setSizeHint(QSize(52, 52));
            grid->addItem(item);
            if (name == *id) grid->setCurrentItem(item);
        }
    };
    QObject::connect(themeList, &QListWidget::currentRowChanged, grid, fill);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("baseicons", "Choisir"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("baseicons", "Annuler"));
    QPushButton* reset = buttons->addButton(QCoreApplication::translate("baseicons", "Icône par défaut"), QDialogButtonBox::ResetRole);
    bool useDefault = false;
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] {
        useDefault = true;
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(grid, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    auto* row = new QHBoxLayout;
    row->addWidget(themeList);
    row->addWidget(grid, 1);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(row, 1);
    layout->addWidget(buttons);

    // Opens on the theme of the current icon.
    int start = 0;
    for (const Entry& e : entries())
        if (*id == QLatin1String(e.id)) start = e.theme;
    themeList->setCurrentRow(start);

    if (dialog.exec() != QDialog::Accepted) return false;
    if (useDefault) {
        *id = QString::fromLatin1(DefaultId);
        return true;
    }
    QListWidgetItem* picked = grid->currentItem();
    if (!picked) return false; // nothing selected in the displayed theme: the icon stays
    *id = picked->data(Qt::UserRole).toString();
    return true;
}

} // namespace baseicons

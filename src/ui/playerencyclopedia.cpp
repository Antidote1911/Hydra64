#include "playerencyclopedia.h"

#include <QSettings>

#include <memory>

namespace playerEncyclopedia {

namespace {
const QString Key = QStringLiteral("playerbase/dir");
std::unique_ptr<hy64db::PlayerBase>& instance()
{
    static std::unique_ptr<hy64db::PlayerBase> base;
    return base;
}
QString& openedDir()
{
    static QString dir;
    return dir;
}
} // namespace

QString directory() { return QSettings().value(Key).toString(); }

bool setDirectory(const QString& dir, QString* error)
{
    if (dir.isEmpty()) {
        QSettings().remove(Key);
        instance().reset();
        openedDir().clear();
        return true;
    }
    auto base = std::make_unique<hy64db::PlayerBase>();
    std::string err;
    if (!base->open(dir.toStdString(), &err)) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    QSettings().setValue(Key, dir);
    instance() = std::move(base);
    openedDir() = dir;
    return true;
}

const hy64db::PlayerBase* get()
{
    const QString dir = directory();
    if (dir.isEmpty()) return nullptr;
    if (!instance() || openedDir() != dir) { // first use: opening (≈ 0.2 s for 525,000 records)
        auto base = std::make_unique<hy64db::PlayerBase>();
        openedDir() = dir;
        instance().reset();
        if (base->open(dir.toStdString())) instance() = std::move(base);
    }
    return instance() && instance()->isOpen() ? instance().get() : nullptr;
}

} // namespace playerEncyclopedia

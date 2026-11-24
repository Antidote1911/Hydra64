#include "conversion.h"
#include "db/database.h"
#include "db/gametree.h"
#include <QFile>

#include <QDir>

#include "chess/treebook.h"
#include "chessbase/chessbase.h"
#include "db/convert.h"
#include "db/chessassistant.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>

#include <memory>

namespace conversion {

namespace {

QString suffix(const QString& path) { return QFileInfo(path).suffix().toLower(); }
QString tr(const char* text) { return QCoreApplication::translate("Conversion", text); }

// Files of a ChessBase database beside the .cbh (games, annotations, players, tournaments, keys…)
bool isChessBaseCompanion(const QString& s)
{
    static const char* const exts[] = {"cba", "cbb", "cbc", "cbe", "cbg", "cbgi", "cbj", "cbl", "cbm", "cbp", "cbs", "cbt", "cbtt",
                                       "cib", "cib2", "cit", "cit2", "cko", "cpo", "ck1", "ck2", "ck3", "ckn", "cp1", "cp2", "cp3", "cpn"};
    for (const char* e : exts)
        if (s == QLatin1String(e)) return true;
    return false;
}

} // namespace

QString databaseFile(const QString& path)
{
    const QFileInfo fi(path);
    // Chess Assistant database: any of its files designates the .cdp
    if (fi.suffix().toLower() != QLatin1String("cdp") && hy64db::ChessAssistantSource::isDatabaseFile(path.toStdString())) {
        for (const char* ext : {".cdp", ".CDP"}) {
            const QString cdp = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QLatin1String(ext);
            if (QFileInfo::exists(cdp)) return cdp;
        }
    }
    if (!isChessBaseCompanion(fi.suffix().toLower())) return path;
    for (const char* ext : {".cbh", ".CBH", ".2cbh"}) {
        const QString cbh = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QLatin1String(ext);
        if (QFileInfo::exists(cbh)) return cbh;
    }
    return path;
}

Kind kindOf(const QString& file)
{
    const QString path = databaseFile(file);
    const QString s = suffix(path);
    if (s == QLatin1String("pgn") || s == QLatin1String("hy64i") || s == QLatin1String("hy64") || s == QLatin1String("cbh") || s == QLatin1String("2cbh")
        || s == QLatin1String("cbv") || s == QLatin1String("2cbv") || s == QLatin1String("epd") || s == QLatin1String("cdp"))
        return Kind::Database;
    if (chess::treebook::treeSourceOf(path.toStdString()) != chess::treebook::TreeSource::Unknown) return Kind::Tree;
    return Kind::Unknown;
}

bool isArchive(const QString& path) { return suffix(path) == QLatin1String("cbv") || suffix(path) == QLatin1String("2cbv"); }

bool isProprietary(const QString& path)
{
    const QString s = suffix(path);
    if (isChessBaseCompanion(s)) return true;
    if (hy64db::ChessAssistantSource::isDatabaseFile(path.toStdString())) return true;
    for (const char* ext : {"cbh", "2cbh", "cbv", "2cbv", "ctg", "cto", "ctb", "hsh", "elm", "cdp"})
        if (s == QLatin1String(ext)) return true;
    return false;
}

QString formatName(const QString& file)
{
    const QString path = databaseFile(file);
    const QString s = suffix(path);
    if (s == QLatin1String("pgn")) return tr("Base PGN");
    if (s == QLatin1String("hy64i")) return tr("Base Hydra64");
    if (s == QLatin1String("hy64")) return tr("Base Hydra64 en un fichier");
    if (s == QLatin1String("epd")) return tr("Positions EPD");
    if (s == QLatin1String("cbh")) return tr("Base ChessBase");
    if (s == QLatin1String("2cbh")) return tr("Base ChessBase 18");
    if (s == QLatin1String("cbv")) return tr("Archive ChessBase");
    if (s == QLatin1String("2cbv")) return tr("Archive ChessBase 18");
    if (s == QLatin1String("cdp")) return tr("Base Chess Assistant / Aquarium");
    switch (chess::treebook::treeSourceOf(path.toStdString())) {
    case chess::treebook::TreeSource::ChessBase: return tr("Livre d'ouvertures ChessBase");
    case chess::treebook::TreeSource::Aquarium: return tr("Arbre ChessOK / Aquarium");
    case chess::treebook::TreeSource::AquariumExport: return tr("Export d'arbre Aquarium (texte)");
    case chess::treebook::TreeSource::Polyglot: return tr("Livre Polyglot");
    case chess::treebook::TreeSource::Hy64Book: return tr("Arbre Hydra64");
    default: return tr("Format non reconnu");
    }
}

QList<Target> targetsFor(Kind kind)
{
    switch (kind) {
    // single file offered first; last, the opening tree of the games
    case Kind::Database: return {Target::Hydra64Single, Target::Hydra64Database, Target::Pgn, Target::Hydra64Tree, Target::Polyglot};
    case Kind::Tree: return {Target::Hydra64Tree, Target::Polyglot};
    default: return {};
    }
}

QString targetName(Target t)
{
    switch (t) {
    case Target::Hydra64Database: return tr("Base Hydra64 en quatre fichiers (.hy64i)");
    case Target::Hydra64Single: return tr("Base Hydra64 en un fichier (.hy64)");
    case Target::Pgn: return tr("Base PGN (.pgn)");
    case Target::Hydra64Tree: return tr("Arbre Hydra64 (.hy64book)");
    case Target::Polyglot: return tr("Livre Polyglot (.bin)");
    }
    return {};
}

QString targetExtension(Target t)
{
    switch (t) {
    case Target::Hydra64Database: return QStringLiteral(".hy64i");
    case Target::Hydra64Single: return QStringLiteral(".hy64");
    case Target::Pgn: return QStringLiteral(".pgn");
    case Target::Hydra64Tree: return QStringLiteral(".hy64book");
    case Target::Polyglot: return QStringLiteral(".bin");
    }
    return {};
}

Target targetForExtension(const QString& path, Kind kind, bool* ok)
{
    *ok = true;
    for (Target t : targetsFor(kind))
        if (path.endsWith(targetExtension(t), Qt::CaseInsensitive)) return t;
    *ok = false;
    return Target::Hydra64Database;
}

QString sourceFilter()
{
    return tr("Bases et arbres (*.pgn *.hy64i *.hy64 *.epd *.cbh *.2cbh *.cbv *.2cbv *.cdp *.ctg *.cto *.ctb *.hsh *.elm *.bin *.hy64book *.txt);;"
              "Bases de parties (*.pgn *.hy64i *.hy64 *.epd *.cbh *.2cbh *.cbv *.2cbv *.cdp);;"
              "Fichiers d'une base Chess Assistant (*.cdp *.d00 *.d02 *.emb);;"
              "Fichiers d'une base ChessBase (*.cbh *.cbg *.cba *.cbj *.cbp *.cbt *.cbs);;"
              "Arbres d'ouverture (*.ctg *.hsh *.elm *.bin *.hy64book *.txt);;Tous les fichiers (*)");
}

QString Report::summary() const
{
    const QLocale loc;
    QString s = kind == Kind::Tree ? tr("%1 coups écrits (%2 positions) en %3 s.").arg(loc.toString(items), loc.toString(positions)).arg(seconds, 0, 'f', 1)
                                   : tr("%1 parties écrites en %2 s.").arg(loc.toString(items)).arg(seconds, 0, 'f', 1);
    if (failed) {
        s += QLatin1Char('\n') + tr("%1 partie(s) illisible(s), ignorée(s) :").arg(loc.toString(failed));
        for (const QString& f : failures) s += QStringLiteral("\n• ") + f;
    }
    if (withoutAnnotations || withoutMoves) {
        s += QLatin1Char('\n') + tr("Parties abîmées dans la base d'origine, gardées à leur place : %1 sans leurs annotations (coups conservés), %2 sans leurs coups (en-têtes conservés).")
                                     .arg(loc.toString(withoutAnnotations), loc.toString(withoutMoves));
        for (const QString& f : partial) s += QStringLiteral("\n• ") + f;
    }
    return s;
}

bool run(const Request& request, Report* report, QString* error, const std::function<bool(int)>& progress)
{
    Request r = request;
    r.source = databaseFile(r.source);
    QElapsedTimer timer;
    timer.start();
    Report rep;
    rep.kind = kindOf(r.source);
    std::string err;
    auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };
    if (rep.kind == Kind::Unknown) return fail(tr("format de départ non reconnu"));
    if (isArchive(r.source)) return fail(tr("archive ChessBase : à extraire d'abord"));
    bool targetOk = false;
    for (Target t : targetsFor(rep.kind)) targetOk = targetOk || t == r.target;
    if (!targetOk) return fail(tr("format d'arrivée incompatible avec la source"));
    // The destination folder is created if needed ("~/Documents/hydra64/openingbooks" does not exist
    // before the first conversion of a tree).
    const QString destinationDir = QFileInfo(r.destination).absolutePath();
    if (!QDir().mkpath(destinationDir)) return fail(tr("impossible de créer le dossier %1").arg(QDir::toNativeSeparators(destinationDir)));

    if (rep.kind == Kind::Tree) {
        chess::treebook::ConvertResult res;
        const bool ok = chess::treebook::convertTree(r.source.toStdString(), r.destination.toStdString(),
                                                     r.target == Target::Polyglot ? chess::treebook::TreeFormat::Polyglot : chess::treebook::TreeFormat::Hy64Book,
                                                     &res, &err, [&](uint64_t d, uint64_t t) {
                                                         return !progress || progress(t ? int(std::min<uint64_t>(999, d * 1000 / t)) : 0);
                                                     });
        if (!ok && err == "write") // write failure with no further detail
            return fail(tr("écriture impossible dans %1 (disque plein ou droits insuffisants ?)").arg(QDir::toNativeSeparators(destinationDir)));
        if (!ok) return fail(err == "cancelled" ? QString() : QString::fromStdString(err));
        rep.items = res.moves;
        rep.positions = res.positions;
    } else if (isTree(r.target)) {
        // Opening tree of the games. Another format than Hydra64: converted first into a
        // temporary database next to the destination (the tree is built from the stored games).
        const QString s = suffix(r.source);
        QString base = r.source;
        const bool temporary = s != QLatin1String("hy64i") && s != QLatin1String("hy64");
        const auto removeTemporary = [&] {
            if (!temporary) return;
            const std::string b = hy64db::basePathOf(base.toStdString());
            for (const std::string& part : {hy64db::indexPath(b), hy64db::namesPath(b), hy64db::gamesPath(b), hy64db::positionsPath(b)}) QFile::remove(QString::fromStdString(part));
        };
        if (temporary) {
            base = r.destination + QStringLiteral(".tmp.hy64i");
            Request first = r;
            first.destination = base;
            first.target = Target::Hydra64Database;
            first.positionDepth = 0;
            QString firstError;
            if (!run(first, nullptr, &firstError, [&](int p) { return !progress || progress(p * 3 / 10); })) {
                removeTemporary();
                return fail(firstError);
            }
        }
        hy64db::GameTreeResult res;
        bool ok = false;
        {
            hy64db::Database db;
            hy64db::GameTreeOptions opt;
            opt.maxPlies = r.treePlies;
            opt.minGames = uint32_t(std::max(1, r.treeMinGames));
            ok = db.open(base.toStdString(), &err)
                 && hy64db::buildGameTree(db, r.destination.toStdString(), r.target == Target::Polyglot ? chess::treebook::TreeFormat::Polyglot : chess::treebook::TreeFormat::Hy64Book,
                                         opt, &res, &err, [&](uint64_t d, uint64_t t) {
                                             const int p = t ? int(std::min<uint64_t>(999, d * 1000 / t)) : 0;
                                             return !progress || progress(temporary ? 300 + p * 7 / 10 : p);
                                         });
        }
        removeTemporary();
        if (!ok && err == "write") return fail(tr("écriture impossible dans %1 (disque plein ou droits insuffisants ?)").arg(QDir::toNativeSeparators(destinationDir)));
        if (!ok && err == "empty") return fail(tr("aucune partie commençant à la position initiale : pas d'arbre à écrire"));
        if (!ok) return fail(err == "cancelled" ? QString() : QString::fromStdString(err));
        rep.kind = Kind::Tree; // what was written
        rep.items = res.moves;
        rep.positions = res.positions;
    } else if ((suffix(r.source) == QLatin1String("hy64i") && r.target == Target::Hydra64Single)
               || (suffix(r.source) == QLatin1String("hy64") && r.target == Target::Hydra64Database)) {
        // Same database in another form: a plain copy of the files, without rereading the games.
        const auto bytes = [&](uint64_t d, uint64_t t) { return !progress || progress(t ? int(std::min<uint64_t>(999, d * 1000 / t)) : 0); };
        const bool ok = r.target == Target::Hydra64Single ? hy64db::packDatabase(r.source.toStdString(), r.destination.toStdString(), &err, bytes)
                                                             : hy64db::unpackDatabase(r.source.toStdString(), r.destination.toStdString(), &err, bytes);
        if (!ok) return fail(err == "cancelled" ? QString() : QString::fromStdString(err));
        hy64db::Database check;
        if (!check.open(r.destination.toStdString(), &err)) return fail(QString::fromStdString(err));
        rep.items = check.count();
    } else {
        // Game source
        std::unique_ptr<hy64db::GameSource> src;
        const QString s = suffix(r.source);
        if (s == QLatin1String("cbh") || s == QLatin1String("2cbh")) {
            auto cb = std::make_unique<ChessBaseGameSource>();
            QString cbErr;
            if (!cb->open(r.source, &cbErr)) return fail(cbErr);
            src = std::move(cb);
        } else if (s == QLatin1String("cdp")) {
            auto ca = std::make_unique<hy64db::ChessAssistantSource>();
            if (!ca->open(r.source.toStdString(), &err)) return fail(QString::fromStdString(err));
            src = std::move(ca);
        } else if (s == QLatin1String("hy64i") || s == QLatin1String("hy64")) {
            auto db = std::make_unique<hy64db::DatabaseSource>();
            if (!db->open(r.source.toStdString(), &err)) return fail(QString::fromStdString(err));
            src = std::move(db);
        } else if (s == QLatin1String("epd")) {
            auto epd = std::make_unique<hy64db::EpdSource>();
            if (!epd->open(r.source.toStdString(), &err)) return fail(QString::fromStdString(err));
            src = std::move(epd);
        } else {
            auto pgn = std::make_unique<hy64db::PgnSource>();
            if (!pgn->open(r.source.toStdString(), &err)) return fail(QString::fromStdString(err));
            src = std::move(pgn);
        }
        hy64db::ConvertResult res;
        const auto step = [&](hy64db::ConvertStep st, uint64_t d, uint64_t t) {
            const double f = t ? double(d) / double(t) : 1.0;
            const int pm = r.target == Target::Pgn || r.positionDepth <= 0 ? int(999 * f) : st == hy64db::StepGames ? int(700 * f) : int(700 + 299 * f);
            return !progress || progress(pm);
        };
        bool ok;
        if (r.target == Target::Pgn) {
            ok = hy64db::exportPgn(*src, r.destination.toStdString(), &res, &err, step);
        } else if (r.target == Target::Hydra64Single) {
            // Temporary ordinary database next to the destination, then packed into one file.
            const QString temp = r.destination + QStringLiteral(".tmp.hy64i");
            hy64db::ConvertOptions opt;
            opt.positionDepth = std::max(0, r.positionDepth);
            ok = hy64db::convert(*src, temp.toStdString(), opt, &res, &err, step) && hy64db::packDatabase(temp.toStdString(), r.destination.toStdString(), &err);
            const std::string base = hy64db::basePathOf(temp.toStdString());
            for (const std::string& part : {hy64db::indexPath(base), hy64db::namesPath(base), hy64db::gamesPath(base), hy64db::positionsPath(base)}) QFile::remove(QString::fromStdString(part));
        } else {
            hy64db::ConvertOptions opt;
            opt.positionDepth = std::max(0, r.positionDepth);
            ok = hy64db::convert(*src, r.destination.toStdString(), opt, &res, &err, step);
        }
        if (!ok) return fail(QString::fromStdString(err));
        rep.items = res.games;
        rep.failed = res.failed;
        for (const std::string& f : res.failures) rep.failures << QString::fromStdString(f);
        const hy64db::GameSource::Partial partial = src->partial();
        rep.withoutAnnotations = partial.withoutAnnotations;
        rep.withoutMoves = partial.withoutMoves;
        for (const std::string& f : partial.first) rep.partial << QString::fromStdString(f);
    }
    rep.seconds = double(timer.elapsed()) / 1000.0;
    if (report) *report = rep;
    return true;
}

} // namespace conversion

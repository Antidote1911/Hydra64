// hydra64-convert: conversion of game databases and opening trees to Hydra64
// or open formats (same code as File → Convert…). The target format is deduced from
// the extension of the destination: .hy64i, .pgn (databases); .hy64book, .bin (trees).
//   hydra64-convert <source> <destination> [--positions D]
//   hydra64-convert <base> <tree.hy64book or .bin> [--plies N] [--min-games N]
//   hydra64-convert --rebuild-positions <base.hy64i> [--positions D]
//   hydra64-convert --verify <source> <base.hy64i or .hy64>
//   hydra64-convert --survey-index <base.hy64> <surveys folder>
//   hydra64-convert --beauty <base.hy64i or .hy64> [--stats]
//   hydra64-convert --check <base.hy64i or .hy64>
#include "chessbase/chessbase.h"
#include "ui/conversion.h"
#include "ui/surveyindex.h"
#include "ui/userdirs.h"
#include "chess/pgn.h"
#include "db/beauty.h"
#include "db/chessassistant.h"
#include "db/convert.h"
#include "db/database.h"
#include "db/positionindex.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdio>
#include <memory>

namespace {

int usage()
{
    std::fprintf(stderr,
                 "Usage :\n"
                 "  hydra64-convert <source> <destination> [--positions D]\n"
                 "  hydra64-convert <base> <arbre.hy64book ou .bin> [--plies N] [--min-games N]   (arbre d'ouverture des parties : N demi-coups par partie au plus, défaut 50 ; chaque partie suivie jusqu'à sa dernière position connue de N parties, défaut 2, 1 = tout)\n"
                 "  hydra64-convert --rebuild-positions <base.hy64i> [--positions D]\n"
                 "  hydra64-convert --verify <source> <base.hy64i ou .hy64>   (compare toutes les parties, une à une)\n"
                 "  hydra64-convert --survey-index <base.hy64> <dossier des synthèses>   (index des synthèses d'ouverture, écrit à côté de la base)\n"
                 "  hydra64-convert --beauty <base.hy64i ou .hy64> [--stats]   (beauté des parties, de 0 à 3 étoiles, inscrite dans la base ; --stats : compte seulement, sans rien écrire)\n"
                 "  hydra64-convert --check <base.hy64i ou .hy64>   (test d'intégrité : relit toutes les parties, n'écrit rien)\n"
                 "  hydra64-convert --rename-legacy <dossier>   (fichiers de l'époque Basilisk64 : .b64 → .hy64, .b64book → .hy64book… ; le contenu ne change pas)\n"
                 "Sources : bases .pgn .hy64i .hy64 .epd .cbh .2cbh .cbv .2cbv .cdp ; arbres .ctg .hsh .elm .bin .hy64book, export Aquarium .txt\n"
                 "Destination : .hy64i, .hy64 (un seul fichier) ou .pgn pour une base, .hy64book ou .bin (Polyglot) pour un arbre.\n"
                 "D : profondeur de l'index des positions en demi-coups (défaut %d, 0 = sans index).\n",
                 hy64db::DefaultPositionDepth);
    return 2;
}

// Progress on one line (at most one update per tenth of a second).
struct Progress {
    std::chrono::steady_clock::time_point last{};
    bool operator()(const char* what, uint64_t done, uint64_t total)
    {
        const auto now = std::chrono::steady_clock::now();
        if (done < total && now - last < std::chrono::milliseconds(100)) return true;
        last = now;
        std::fprintf(stderr, "\r%s : %llu / %llu (%.0f %%)   ", what, (unsigned long long)done, (unsigned long long)total,
                     total ? 100.0 * double(done) / double(total) : 100.0);
        if (done >= total) std::fprintf(stderr, "\n");
        return true;
    }
};

// Verification of a conversion: each game of the source is reread in full (moves, variations,
// comments, headers) through the "reading" path — not the conversion one — and compared with the game
// of the same rank in the Hydra64 database, both written as PGN. Games the source cannot read
// are counted separately: the conversion skipped them, and the following ranks are shifted accordingly.
int verify(const QString& sourcePath, const QString& basePath, unsigned threads, Progress& progress)
{
    std::string err;
    std::unique_ptr<hy64db::GameSource> src;
    const QString s = QFileInfo(sourcePath).suffix().toLower();
    if (s == QLatin1String("cbh") || s == QLatin1String("2cbh")) {
        auto cb = std::make_unique<ChessBaseGameSource>();
        QString cbErr;
        if (!cb->open(sourcePath, &cbErr)) return std::fprintf(stderr, "Erreur : %s\n", qPrintable(cbErr)), 1;
        src = std::move(cb);
    } else if (s == QLatin1String("cdp")) {
        auto ca = std::make_unique<hy64db::ChessAssistantSource>();
        if (!ca->open(sourcePath.toStdString(), &err)) return std::fprintf(stderr, "Erreur : %s\n", err.c_str()), 1;
        src = std::move(ca);
    } else if (s == QLatin1String("pgn")) {
        auto pgn = std::make_unique<hy64db::PgnSource>();
        if (!pgn->open(sourcePath.toStdString(), &err)) return std::fprintf(stderr, "Erreur : %s\n", err.c_str()), 1;
        src = std::move(pgn);
    } else {
        return std::fprintf(stderr, "Erreur : vérification possible depuis .cbh, .2cbh, .cdp ou .pgn\n"), 2;
    }
    hy64db::Database db;
    if (!db.open(basePath.toStdString(), &err)) return std::fprintf(stderr, "Erreur : %s\n", err.c_str()), 1;

    const uint64_t total = src->count();
    const unsigned workers = threads ? threads : std::max(1u, std::thread::hardware_concurrency());
    const auto parallel = [&](const char* what, const std::function<void(uint64_t)>& one) {
        std::atomic<uint64_t> next{0}, done{0};
        std::mutex show;
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < workers; ++w)
            pool.emplace_back([&] {
                for (;;) {
                    const uint64_t from = next.fetch_add(2048);
                    if (from >= total) return;
                    const uint64_t to = std::min(total, from + 2048);
                    for (uint64_t i = from; i < to; ++i) one(i);
                    const uint64_t d = done.fetch_add(to - from) + (to - from);
                    if (show.try_lock()) {
                        progress(what, d, total);
                        show.unlock();
                    }
                }
            });
        for (std::thread& th : pool) th.join();
        progress(what, total, total);
    };

    // 1. The source: fingerprint of the PGN of each readable game
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<uint64_t> print(total, 0);
    std::vector<uint8_t> readable(total, 0);
    std::mutex refusedLock;
    std::vector<std::pair<uint64_t, std::string>> refusedList; // the first ones, with their reason
    std::vector<std::pair<uint64_t, std::string>> withoutMoves, withoutNotes;
    parallel("Source", [&](uint64_t i) {
        chess::Game g;
        std::string why;
        if (!src->read(i, g, &why)) {
            std::lock_guard<std::mutex> lock(refusedLock);
            if (refusedList.size() < 200) refusedList.emplace_back(i, why);
            return;
        }
        readable[i] = 1;
        print[i] = std::hash<std::string>{}(chess::pgn::writeGame(g));
        // Games only partly recovered (see ChessBaseGameSource): counted and listed
        const std::string& head = g.root()->comment;
        const bool noMoves = head.rfind("Coups illisibles dans la base ChessBase", 0) == 0, noNotes = head.rfind("Annotations illisibles dans la base ChessBase", 0) == 0;
        if (noMoves || noNotes) {
            std::lock_guard<std::mutex> lock(refusedLock);
            (noMoves ? withoutMoves : withoutNotes).emplace_back(i, head);
        }
    });
    // 2. Rank of each readable game in the converted database
    std::vector<uint32_t> rank(total, 0);
    uint64_t readableCount = 0;
    for (uint64_t i = 0; i < total; ++i) {
        rank[i] = uint32_t(readableCount);
        readableCount += readable[i];
    }
    const uint64_t refused = total - readableCount;
    if (readableCount != db.count()) {
        std::fprintf(stderr, "ÉCART : la source a %llu parties lisibles (%llu illisibles), la base convertie en a %llu.\n", (unsigned long long)readableCount,
                     (unsigned long long)refused, (unsigned long long)db.count());
        return 1;
    }
    // 3. The converted database: same fingerprint at the same rank?
    std::atomic<uint64_t> different{0};
    std::mutex report;
    std::vector<uint64_t> firstDifferent;
    parallel("Base convertie", [&](uint64_t i) {
        if (!readable[i]) return;
        chess::Game g;
        if (db.game(rank[i], g) && std::hash<std::string>{}(chess::pgn::writeGame(g)) == print[i]) return;
        ++different;
        std::lock_guard<std::mutex> lock(report);
        if (firstDifferent.size() < 20) firstDifferent.push_back(i);
    });
    std::sort(firstDifferent.begin(), firstDifferent.end());
    std::sort(refusedList.begin(), refusedList.end());
    for (const auto& [i, why] : refusedList) std::fprintf(stderr, "  illisible dans la source : partie n° %llu — %s\n", (unsigned long long)(i + 1), why.c_str());
    for (uint64_t i : firstDifferent) { // where the two texts diverge
        chess::Game a, b;
        src->read(i, a, nullptr);
        db.game(rank[i], b);
        const std::string ta = chess::pgn::writeGame(a), tb = chess::pgn::writeGame(b);
        size_t at = 0;
        while (at < ta.size() && at < tb.size() && ta[at] == tb[at]) ++at;
        const size_t from = at > 60 ? at - 60 : 0;
        std::fprintf(stderr, "  partie n° %llu de la source (rang %u de la base), %s – %s :\n    source : …%s\n    base   : …%s\n", (unsigned long long)(i + 1), rank[i] + 1,
                     a.header("White").c_str(), a.header("Black").c_str(), ta.substr(from, 160).c_str(), tb.substr(from, 160).c_str());
    }
    std::sort(withoutMoves.begin(), withoutMoves.end());
    std::sort(withoutNotes.begin(), withoutNotes.end());
    for (const auto& [i, why] : withoutNotes) std::fprintf(stderr, "  sans ses annotations : partie n° %llu — %s\n", (unsigned long long)(i + 1), why.c_str());
    for (const auto& [i, why] : withoutMoves) std::fprintf(stderr, "  sans ses coups : partie n° %llu — %s\n", (unsigned long long)(i + 1), why.c_str());
    std::fprintf(stderr, "Récupérées en partie (abîmées dans la source) : %zu sans leurs annotations, %zu sans leurs coups.\n", withoutNotes.size(), withoutMoves.size());
    std::fprintf(stderr, "%llu parties dans la source : %llu identiques dans la base convertie, %llu différentes, %llu illisibles dans la source (ignorées par la conversion) — %.0f s.\n",
                 (unsigned long long)total, (unsigned long long)(readableCount - different.load()), (unsigned long long)different.load(), (unsigned long long)refused,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return different.load() ? 1 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    QStringList positional;
    int depth = hy64db::DefaultPositionDepth;
    int treePlies = 50, treeMinGames = 2;
    unsigned threads = 0;
    bool rebuild = false, verifyMode = false, surveyMode = false, beautyMode = false, statsOnly = false, legacyMode = false, checkMode = false;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--positions") && i + 1 < args.size()) depth = args[++i].toInt();
        else if (args[i] == QLatin1String("--plies") && i + 1 < args.size()) treePlies = args[++i].toInt();
        else if (args[i] == QLatin1String("--min-games") && i + 1 < args.size()) treeMinGames = args[++i].toInt();
        else if (args[i] == QLatin1String("--threads") && i + 1 < args.size()) threads = args[++i].toUInt();
        else if (args[i] == QLatin1String("--rebuild-positions")) rebuild = true;
        else if (args[i] == QLatin1String("--verify")) verifyMode = true;
        else if (args[i] == QLatin1String("--survey-index")) surveyMode = true;
        else if (args[i] == QLatin1String("--beauty")) beautyMode = true;
        else if (args[i] == QLatin1String("--check")) checkMode = true;
        else if (args[i] == QLatin1String("--rename-legacy")) legacyMode = true;
        else if (args[i] == QLatin1String("--stats")) statsOnly = true;
        else if (args[i].startsWith(QLatin1String("--"))) return usage();
        else positional << args[i];
    }
    Progress progress;
    std::string error;

    if (legacyMode) { // files of the Basilisk64 days: new extensions, same content
        if (positional.size() != 1) return usage();
        const int n = userdirs::renameLegacyFiles(positional[0]);
        std::fprintf(stderr, "%d fichier(s) renommé(s) dans %s\n", n, qPrintable(positional[0]));
        return 0;
    }

    if (surveyMode) {
        if (positional.size() != 2) return usage();
        QString err;
        const int n = surveyindex::build(positional[0], positional[1], &err, [&](int d, int t) { progress("Synthèses", uint64_t(d), uint64_t(t)); });
        if (n < 0) return std::fprintf(stderr, "\nErreur : %s\n", qPrintable(err)), 1;
        std::fprintf(stderr, "%d synthèses indexées dans %s\n", n, qPrintable(surveyindex::pathFor(positional[0])));
        return 0;
    }

    if (checkMode) { // integrity test: every game read again, nothing written
        if (positional.size() != 1) return usage();
        hy64db::Database db;
        if (!db.open(positional[0].toStdString(), &error)) return std::fprintf(stderr, "Erreur : %s\n", error.c_str()), 1;
        const auto t0 = std::chrono::steady_clock::now();
        const hy64db::IntegrityReport rep = db.checkIntegrity([&](uint64_t d, uint64_t t) {
            progress("Test d'intégrité", d, t);
            return true;
        });
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "\n%u parties relues en %.1f s (dont %u supprimées) : %u problème(s)\n", rep.games, seconds, rep.deleted, rep.problems());
        if (rep.problems())
            std::fprintf(stderr, "  blocs hors du fichier : %u, noms inconnus : %u, parties illisibles : %u, longueurs différentes : %u\n", rep.badLocation, rep.badNames,
                         rep.unreadable, rep.badLength);
        for (const auto& p : rep.first) std::fprintf(stderr, "  partie n° %u : %s\n", p.id + 1, p.what.c_str());
        return rep.ok() ? 0 : 1;
    }

    if (beautyMode) {
        if (positional.size() != 1) return usage();
        hy64db::Database db;
        if (!db.open(positional[0].toStdString(), &error)) return std::fprintf(stderr, "Erreur : %s\n", error.c_str()), 1;
        const auto t0 = std::chrono::steady_clock::now();
        uint64_t marks[4] = {0, 0, 0, 0};
        if (statsOnly) { // what the current rules would give, without touching the database
            std::vector<uint8_t> blob;
            hy64db::BeautyRater rater;
            for (uint32_t id = 0; id < db.count(); ++id) {
                const hy64db::Record r = db.record(id);
                if (r.deleted() || !db.blob(id, blob)) continue;
                rater.start();
                hy64db::replayMainlineMoves(blob.data(), blob.size(), [&](const chess::Position& before, chess::Move m, int) {
                    rater.move(before, m);
                    return true;
                });
                ++marks[rater.medals(hy64db::Result(r.result))];
                if ((id & 0xFFFF) == 0) progress("Beauté", id, db.count());
            }
        } else {
            const int64_t changed = db.rateBeauty(&error, [&](uint64_t d, uint64_t t) { return progress("Beauté", d, t); }, true);
            if (changed < 0) return std::fprintf(stderr, "\nErreur : %s\n", error.c_str()), 1;
            for (uint32_t id = 0; id < db.count(); ++id) {
                const hy64db::Record r = db.record(id);
                if (!r.deleted() && hy64db::beautyOf(r.flags) >= 0) ++marks[hy64db::beautyOf(r.flags)];
            }
            std::fprintf(stderr, "\n%lld parties mises à jour.", (long long)changed);
        }
        const uint64_t total = marks[0] + marks[1] + marks[2] + marks[3];
        std::fprintf(stderr, "\nBeauté de %llu parties en %.1f s : ★ %llu (%.2f %%), ★★ %llu (%.2f %%), ★★★ %llu (%.2f %%)\n", (unsigned long long)total,
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), (unsigned long long)marks[1],
                     total ? 100.0 * marks[1] / total : 0, (unsigned long long)marks[2], total ? 100.0 * marks[2] / total : 0,
                     (unsigned long long)marks[3], total ? 100.0 * marks[3] / total : 0);
        return 0;
    }

    if (verifyMode) {
        if (positional.size() != 2) return usage();
        return verify(conversion::databaseFile(positional[0]), positional[1], threads, progress);
    }

    if (rebuild) {
        if (positional.size() != 1) return usage();
        hy64db::Database db;
        if (!db.open(positional[0].toStdString(), &error)) {
            std::fprintf(stderr, "Erreur : %s\n", error.c_str());
            return 1;
        }
        const auto t0 = std::chrono::steady_clock::now();
        if (!hy64db::buildPositionIndex(db, depth, &error, [&](uint64_t d, uint64_t t) { return progress("Positions", d, t); })) {
            std::fprintf(stderr, "\nErreur : %s\n", error.c_str());
            return 1;
        }
        std::fprintf(stderr, "Index des positions refait en %.1f s (%llu positions).\n",
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                     (unsigned long long)(db.positions() ? db.positions()->positions() : 0));
        return 0;
    }

    if (positional.size() != 2) return usage();
    QString source = conversion::databaseFile(positional[0]); // .cbg, .cba… → the database's .cbh
    const QString destination = positional[1];
    if (conversion::isArchive(source)) { // ChessBase archive: extracted next to the destination
        const QString dir = QFileInfo(destination).absolutePath() + QStringLiteral("/") + QFileInfo(source).completeBaseName()
                          + QStringLiteral(" (ChessBase)");
        std::fprintf(stderr, "Extraction de l'archive dans %s…\n", qPrintable(dir));
        QString err;
        if (!ChessBaseSource::extractArchive(source, dir, &err)) {
            std::fprintf(stderr, "Erreur : %s\n", qPrintable(err));
            return 1;
        }
        const QFileInfoList found = QDir(dir).entryInfoList({QStringLiteral("*.cbh"), QStringLiteral("*.2cbh")}, QDir::Files);
        if (found.isEmpty()) {
            std::fprintf(stderr, "Erreur : aucune base dans l'archive\n");
            return 1;
        }
        source = found.first().absoluteFilePath();
    }
    const conversion::Kind kind = conversion::kindOf(source);
    if (kind == conversion::Kind::Unknown) {
        std::fprintf(stderr, "Erreur : format de départ non reconnu (%s)\n", qPrintable(source));
        return 1;
    }
    bool ok = false;
    conversion::Request request;
    request.source = source;
    request.destination = destination;
    request.target = conversion::targetForExtension(destination, kind, &ok);
    request.positionDepth = depth;
    request.treePlies = treePlies;
    request.treeMinGames = treeMinGames;
    if (!ok) {
        QStringList exts;
        for (conversion::Target t : conversion::targetsFor(kind)) exts << conversion::targetExtension(t);
        std::fprintf(stderr, "Erreur : destination à nommer en %s pour une source « %s »\n", qPrintable(exts.join(QStringLiteral(" ou "))),
                     qPrintable(conversion::formatName(source)));
        return 2;
    }
    (void)threads;
    std::fprintf(stderr, "%s → %s\n", qPrintable(conversion::formatName(source)), qPrintable(conversion::targetName(request.target)));
    conversion::Report report;
    QString failure;
    const bool done = conversion::run(request, &report, &failure, [&](int permille) { return progress("Conversion", uint64_t(permille), 1000); });
    if (!done) {
        std::fprintf(stderr, "\nErreur : %s\n", failure.isEmpty() ? "conversion annulée" : qPrintable(failure));
        return 1;
    }
    progress("Conversion", 1000, 1000);
    std::fprintf(stderr, "%s\n", qPrintable(report.summary()));
    return 0;
}

#include "chess/pgn.h"
#include "ui/userdirs.h"
#include "ui/enginegame.h"
#include "ui/enginemanager.h"
#include "ui/gameinfodialog.h"
#include "ui/positioneditor.h"
#include "ui/enginepanel.h"
#include "ui/boardwidget.h"
#include "ui/gamesession.h"
#include "ui/mainwindow.h"
#include "ui/preferences.h"
#include "ui/theme.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QWidget>
#include <QWindow>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QTimer>
#include <QTranslator>

#include <cstdio>
#if defined(__GLIBC__)
#include <execinfo.h>
#endif

namespace {
QtMessageHandler previousHandler = nullptr;

QString describe(const QWidget* w)
{
    if (!w) return QStringLiteral("-");
    QStringList chain;
    for (const QWidget* p = w; p; p = p->parentWidget())
        chain << QStringLiteral("%1(%2)").arg(QString::fromLatin1(p->metaObject()->className()), p->objectName());
    return chain.join(QStringLiteral(" < "));
}

// Diagnostics: "… must be a top level window." (QWindow::setTransientParent) is logged with
// the call stack and the active widgets in <config>/Hydra64/diagnostic.log, to find its cause.
void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (previousHandler) previousHandler(type, context, msg);
    else std::fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, msg)));
    if (!msg.contains(QLatin1String("must be a top level window"))) return;
    QString report = QStringLiteral("=== %1\n%2\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate), msg);
    report += QStringLiteral("fenêtre active : %1\npopup : %2\nmodale : %3\nfocus : %4\n")
                  .arg(describe(QApplication::activeWindow()), describe(QApplication::activePopupWidget()), describe(QApplication::activeModalWidget()),
                       describe(QApplication::focusWidget()));
    for (QWindow* w : QGuiApplication::allWindows())
        if (w->parent()) report += QStringLiteral("fenêtre native enfant : %1 (parent %2)\n").arg(w->objectName(), w->parent()->objectName());
#if defined(__GLIBC__)
    void* frames[48];
    const int n = backtrace(frames, 48);
    if (char** symbols = backtrace_symbols(frames, n)) {
        for (int i = 0; i < n; ++i) report += QString::fromLocal8Bit(symbols[i]) + QLatin1Char('\n');
        std::free(symbols);
    }
#endif
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    QFile f(dir + QStringLiteral("/diagnostic.log"));
    if (f.open(QIODevice::Append)) f.write(report.toUtf8());
}
} // namespace

int main(int argc, char** argv)
{
    previousHandler = qInstallMessageHandler(messageHandler);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Hydra64"));
    QApplication::setOrganizationName(QStringLiteral("Hydra64"));
    QApplication::setApplicationDisplayName(QStringLiteral("Hydra64"));
    QApplication::setApplicationVersion(QStringLiteral(HYDRA64_VERSION)); // project() of CMakeLists.txt
    QApplication::setWindowIcon(theme::logoIcon());
    theme::apply(app);

    // Interface language: Hydra64's texts (written in French) + Qt's standard dialogs.
    const QString lang = prefs::language();
    QLocale::setDefault(QLocale(lang == QLatin1String("en") ? QLocale::English : QLocale::French));
    auto* qtTranslator = new QTranslator(&app);
    if (qtTranslator->load(QStringLiteral("qtbase_") + lang, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(qtTranslator);
    if (lang != QLatin1String("fr")) {
        auto* appTranslator = new QTranslator(&app);
        if (appTranslator->load(QStringLiteral(":/i18n/hydra64_") + lang)) QApplication::installTranslator(appTranslator);
    } else {
        // Panel menus (ADS, written in English)
        auto* adsTranslator = new QTranslator(&app);
        if (adsTranslator->load(QStringLiteral(":/i18n/ads_fr"))) QApplication::installTranslator(adsTranslator);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Hydra64 — interface d'échecs : bases, analyse, jeu, tournois de moteurs"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("base"), QStringLiteral("Base PGN à ouvrir"));
    QCommandLineOption screenshot(QStringLiteral("screenshot"), QStringLiteral("Capture la fenêtre dans <fichier> puis quitte."), QStringLiteral("fichier"));
    QCommandLineOption page(QStringLiteral("page"), QStringLiteral("Page affichée : bases, analyse, jouer, tournoi."), QStringLiteral("page"));
    QCommandLineOption game(QStringLiteral("game"), QStringLiteral("Charge la partie n° <n> de la base."), QStringLiteral("n"));
    QCommandLineOption ply(QStringLiteral("ply"), QStringLiteral("Avance de <n> demi-coups."), QStringLiteral("n"));
    QCommandLineOption engine(QStringLiteral("engine"), QStringLiteral("Moteur UCI à utiliser."), QStringLiteral("chemin"));
    QCommandLineOption analyse(QStringLiteral("analyse"), QStringLiteral("Lance l'analyse au démarrage."));
    QCommandLineOption fen(QStringLiteral("fen"), QStringLiteral("Commence à la position <fen>."), QStringLiteral("fen"));
    QCommandLineOption play(QStringLiteral("play"), QStringLiteral("Joue contre le moteur avec <camp> : blancs, noirs, hasard."), QStringLiteral("camp"));
    QCommandLineOption elo(QStringLiteral("elo"), QStringLiteral("Force du moteur (1320–3190, ou max)."), QStringLiteral("elo"));
    QCommandLineOption moveTime(QStringLiteral("movetime"), QStringLiteral("Temps de réflexion du moteur par coup, en ms."), QStringLiteral("ms"));
    QCommandLineOption engines(QStringLiteral("engines"), QStringLiteral("Ouvre la gestion des moteurs."));
    QCommandLineOption moves(QStringLiteral("moves"), QStringLiteral("Joue des coups UCI sur l'échiquier comme à la souris (ex. e2e4,g1f3)."), QStringLiteral("coups"));
    QCommandLineOption delay(QStringLiteral("delay"), QStringLiteral("Délai avant la capture, en ms."), QStringLiteral("ms"));
    QCommandLineOption editor(QStringLiteral("edit"), QStringLiteral("Ouvre l'éditeur de position."));
    QCommandLineOption save(QStringLiteral("save"), QStringLiteral("Avant la capture, enregistre <n> fois la partie dans la base ouverte."), QStringLiteral("n"));
    QCommandLineOption info(QStringLiteral("info"), QStringLiteral("Ouvre les informations de la partie."));
    QCommandLineOption tc(QStringLiteral("tc"), QStringLiteral("Cadence pour --play : minutes+incrément (ex. 3+2)."), QStringLiteral("cadence"));
    QCommandLineOption prefsOpt(QStringLiteral("prefs"), QStringLiteral("Ouvre les préférences."));
    QCommandLineOption panelOpt(QStringLiteral("panel"), QStringLiteral("Ouvre le panneau <nom> (clock, graph, notation…)."), QStringLiteral("nom"));
    QCommandLineOption aboutOpt(QStringLiteral("about"), QStringLiteral("Ouvre « À propos de Hydra64 »."));
    parser.addOptions({screenshot, page, game, ply, engine, analyse, fen, play, elo, moveTime, engines, moves, delay, editor, save, info, prefsOpt, tc, aboutOpt, panelOpt});
    parser.process(app);

    // User folder: databases/ (bundled databases copied if missing), openingbooks/, tablebases/
    userdirs::install();
    MainWindow w;
    if (parser.isSet(screenshot)) w.resize(1600, 960);
    if (!parser.positionalArguments().isEmpty()) w.openDatabase(parser.positionalArguments().first());
    if (parser.isSet(game)) w.loadGameFromDatabase(parser.value(game).toInt() - 1); // Databases page
    if (parser.isSet(page)) {
        const QString p = parser.value(page);
        const MainWindow::Page target = p.startsWith('b') || p.startsWith('d') ? MainWindow::PageDatabase
                                        : (p.startsWith('j') || p.startsWith('e')) ? MainWindow::PageBoard
                                        : p.startsWith('t') ? MainWindow::PageTournament
                                                            : MainWindow::PageAnalysis;
        // --game N --page analyse: the opened game is sent to analysis.
        if (target == MainWindow::PageAnalysis && parser.isSet(game)) w.sendToAnalysis(false);
        else w.setPage(target);
    }
    if (parser.isSet(ply)) {
        auto* session = w.findChild<GameSession*>();
        for (int i = 0; session && i < parser.value(ply).toInt(); ++i) session->forward();
    }
    if (parser.isSet(engine)) w.enginePanel()->setEnginePath(parser.value(engine));
    if (parser.isSet(fen)) {
        chess::Position pos;
        if (pos.setFen(parser.value(fen).toStdString())) w.findChild<GameSession*>()->newGame(pos);
        else qWarning("FEN invalide : %s", qPrintable(parser.value(fen)));
    }
    w.show();
    if (parser.isSet(play)) {
        PlaySettings ps;
        const QString side = parser.value(play);
        ps.side = side.startsWith('n') ? PlaySettings::PlayBlack : side.startsWith('h') ? PlaySettings::PlayRandom : PlaySettings::PlayWhite;
        ps.fullStrength = parser.value(elo) == QLatin1String("max");
        if (parser.isSet(elo) && !ps.fullStrength) ps.elo = std::clamp(parser.value(elo).toInt(), 1320, 3190);
        if (parser.isSet(moveTime)) ps.moveTimeMs = std::max(50, parser.value(moveTime).toInt());
        ps.fromCurrentPosition = parser.isSet(fen);
        if (parser.isSet(tc)) {
            const QStringList t = parser.value(tc).split('+');
            ps.baseMs = qint64(t.value(0).toDouble() * 60000);
            ps.incMs = qint64(t.value(1).toDouble() * 1000);
        }
        w.startEngineGame(ps);
    }
    if (parser.isSet(analyse)) w.enginePanel()->setAnalysing(true);

    if (parser.isSet(moves)) {
        // Each move is sent when possible (the engine may be playing).
        auto* queue = new QStringList(parser.value(moves).split(',', Qt::SkipEmptyParts));
        auto* timer = new QTimer(&w);
        QObject::connect(timer, &QObject::destroyed, [queue] { delete queue; });
        QObject::connect(timer, &QTimer::timeout, &w, [&w, queue, timer] {
            auto* board = w.findChild<BoardWidget*>();
            auto* session = w.findChild<GameSession*>();
            if (queue->isEmpty() || !board || !session) { // done, or window being closed
                timer->stop();
                return;
            }
            const chess::Move m = session->current()->pos.parseUci(queue->first().toStdString());
            if (m.isNull()) return; // not playable yet (engine's turn)
            queue->removeFirst();
            emit board->moveRequested(m);
        });
        timer->start(150);
    }

    QDialog* manager = nullptr;
    if (parser.isSet(panelOpt)) w.showPanel(parser.value(panelOpt));
    if (parser.isSet(aboutOpt)) {
        QTimer::singleShot(0, &w, [&w] { w.showAbout(); });
    }
    if (parser.isSet(prefsOpt)) {
        manager = new PreferencesDialog(&w);
        manager->setAttribute(Qt::WA_DeleteOnClose);
        manager->show();
    } else if (parser.isSet(info)) {
        manager = new GameInfoDialog(w.findChild<GameSession*>()->game().headers(), &w);
        manager->setAttribute(Qt::WA_DeleteOnClose);
        manager->show();
    } else if (parser.isSet(editor)) {
        manager = new PositionEditorDialog(w.findChild<GameSession*>()->current()->pos, false, &w);
        manager->setAttribute(Qt::WA_DeleteOnClose);
        manager->show();
    } else if (parser.isSet(engines)) {
        manager = new EngineManagerDialog(&w);
        manager->setAttribute(Qt::WA_DeleteOnClose);
        manager->show();
    }

    if (parser.isSet(screenshot)) {
        const QString file = parser.value(screenshot);
        const int wait = parser.isSet(delay) ? parser.value(delay).toInt() : (parser.isSet(analyse) || parser.isSet(play) || parser.isSet(moves) ? 2000 : 600);
        const int saves = parser.isSet(save) ? parser.value(save).toInt() : 0;
        QTimer::singleShot(wait, &w, [&w, file, manager, saves] {
            for (int i = 0; i < saves; ++i) w.saveToDatabase();
            QWidget* modal = QApplication::activeModalWidget();
            (manager ? manager->grab() : modal ? modal->grab() : w.grab()).save(file);
            if (modal && !manager) modal->close();
            w.close(); // goes through closeEvent: the layout is saved
        });
    }
    const int rc = app.exec();
    if (w.resetRequested()) { // Help → Reset: fresh installation, then restart
        MainWindow::wipeUserData();
        QProcess::startDetached(QApplication::applicationFilePath(), {});
    }
    return rc;
}

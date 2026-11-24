#include "enginegame.h"
#include "tabengine.h"
#include "chess/pgn.h"
#include "openings.h"
#include <QTimer>
#include "engine/uciengine.h"
#include "display.h"
#include "gamesession.h"
#include "modulewidgets.h"

#include <QDate>
#include <QFileInfo>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSettings>
#include <QThread>

using namespace chess;

EngineGame::EngineGame(GameSession* session, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_engine(new UciEngine(this))
    , m_clock(new ChessClock(this))
{
    connect(m_clock, &ChessClock::tick, this, &EngineGame::clockTick);
    connect(m_clock, &ChessClock::flagFell, this, &EngineGame::onFlag);
    connect(m_engine, &UciEngine::bestMoveReceived, this, &EngineGame::onBestMove);
    m_useBook = QSettings().value(QStringLiteral("play/useBook"), true).toBool();
    connect(m_engine, &UciEngine::infoReceived, this, &EngineGame::engineInfo);
    connect(m_engine, &UciEngine::infoReceived, this, [this](const EngineLine& l) {
        if (m_settings.beginnerLevel > 0 && !l.pv.isEmpty() && !m_pondering) m_lines[l.multipv] = l;
    });
    m_multiPv = std::clamp(QSettings().value(QStringLiteral("playVariants"), DefaultMultiPv).toInt(), 1, MaxMultiPv);
    // Options changed in the engine manager (e.g. Threads through the CPU button):
    // applied right away to the engine of the game in progress.
    connect(&EngineRegistry::instance(), &EngineRegistry::changed, this, &EngineGame::applyRegistryChanges);
    // Settings of the Opponent tab changed: sent to the engine of the game in progress, like the engine's own
    connect(&tabengine::Notifier::instance(), &tabengine::Notifier::changed, this, [this](const QString& tab) {
        if (tab == QLatin1String(tabengine::PlayTab)) applyRegistryChanges();
    });
    connect(m_engine, &UciEngine::started, this, [this] {
        m_engineReady = true;
        updateHeaders();
        // The engine took time to start (NNUE network…): that is not thinking time.
        if (m_active && !m_suspended && !m_halted && !m_paused && isTimed() && !m_clock->isRunning() && isAtGameEnd()) m_clock->start(lastNode()->pos.sideToMove());
    });
    connect(m_engine, &UciEngine::terminated, this, [this] {
        if (m_active) finish("*", tr("Le moteur s'est arrêté"));
    });
    // treeChanged comes before currentChanged when a move creates a new node.
    connect(session, &GameSession::treeChanged, this, [this] {
        // An added move makes the tree grow; a shrinking tree is a deletion, not a move played
        // (without this distinction, deleting the last move passed for a move and the engine replied at once)
        size_t size = 0;
        std::vector<const Node*> stack{m_session->game().root()};
        while (!stack.empty()) {
            const Node* n = stack.back();
            stack.pop_back();
            ++size;
            for (const auto& c : n->children) stack.push_back(c.get());
        }
        if (size < m_treeSize) m_deleted = true, m_newMove = false;
        else m_newMove = true, m_deleted = m_deleted && size == m_treeSize;
        if (m_starting) m_deleted = false; // new game: the old tree disappears, that is not a deletion
        m_treeSize = size;
    });
    connect(session, &GameSession::currentChanged, this, &EngineGame::onPositionChanged);
    // Another game loaded or started ends the game in progress.
    // (not during the pause: it is then the Analysis page's game that is replaced)
    connect(session, &GameSession::gameReplaced, this, [this] {
        if (!m_starting && !m_suspended) {
            m_flagEnded = false; // the game that ended on time is no longer on screen
            stop();
        }
    });
}

QString EngineGame::engineLabel() const
{
    QString name = m_config.name.isEmpty() ? m_engine->name() : m_config.name;
    if (name.isEmpty()) name = QFileInfo(m_engine->path()).baseName();
    const bool hasElo = m_config.option(QStringLiteral("UCI_Elo")) || m_config.options.isEmpty(); // unknown options: we assume
    if (m_settings.beginnerLevel > 0) return tr("%1 (débutant %2)").arg(name).arg(m_settings.beginnerLevel);
    return m_settings.fullStrength || !hasElo ? name : tr("%1 (Elo %2)").arg(name).arg(m_settings.elo);
}

bool EngineGame::launchEngine(const EngineConfig& registryEngine, const PlaySettings& settings, QString* error)
{
    // Engine settings + those of the Opponent tab (Threads, Hash… specific to the Play page)
    const EngineConfig engine = tabengine::effective(QLatin1String(tabengine::PlayTab), registryEngine);
    stop();
    m_flagEnded = false;
    if (!engine.isValid()) {
        if (error) *error = tr("Aucun moteur configuré (menu Moteur → Gestion des moteurs…).");
        return false;
    }
    // Always restarted: the options may have changed since the last game.
    m_engine->quit();
    m_engineReady = false;
    m_deleted = false;
    m_awaiting = false;
    // Endgame tablebases: only for an opponent at full strength (a limited level that
    // played endgames perfectly would make no sense)
    m_engine->setTablebaseRole(UciEngine::TablebaseRole::Play, settings.fullStrength && settings.beginnerLevel == 0 && settings.tablebases);
    if (!m_engine->start(engine.path)) {
        if (error) *error = tr("Impossible de lancer le moteur :\n%1").arg(engine.path);
        return false;
    }
    m_config = engine;
    if (engine.id.isEmpty()) {
        m_engine->setOption(QStringLiteral("Threads"), QString::number(std::max(1, QThread::idealThreadCount() / 2)));
        m_engine->setOption(QStringLiteral("Hash"), QStringLiteral("256"));
    }
    for (auto it = engine.values.cbegin(); it != engine.values.cend(); ++it) m_engine->setOption(it.key(), it.value());
    m_settings = settings;
    m_engine->setOption(QStringLiteral("UCI_LimitStrength"), settings.fullStrength && settings.beginnerLevel == 0 ? QStringLiteral("false") : QStringLiteral("true"));
    sendSkillLevel();
    if (!settings.fullStrength || settings.beginnerLevel > 0)
        m_engine->setOption(QStringLiteral("UCI_Elo"), QString::number(settings.beginnerLevel > 0 ? 1320 : settings.elo));
    if (module::supportsMultiPv(engine))
        m_engine->setOption(QStringLiteral("MultiPV"), QString::number(settings.beginnerLevel > 0 ? std::max(m_multiPv, 6) : m_multiPv));
    if (engine.option(QStringLiteral("Ponder"))) m_engine->setOption(QStringLiteral("Ponder"), settings.ponder ? QStringLiteral("true") : QStringLiteral("false"));
    m_pondering = false;
    m_engine->newGame();
    m_halted = false;
    m_paused = false;
    return true;
}

void EngineGame::setupClock(qint64 whiteMs, qint64 blackMs)
{
    if (!m_settings.isTimed()) {
        m_clock->setup(0, 0, 0, 0);
        return;
    }
    const qint64 engineInc = m_settings.engineSeparate ? m_settings.engineIncMs : m_settings.incMs;
    m_clock->setup(whiteMs, m_human == White ? m_settings.incMs : engineInc, blackMs, m_human == Black ? m_settings.incMs : engineInc);
}

bool EngineGame::start(const EngineConfig& engine, const PlaySettings& settings, QString* error)
{
    if (!launchEngine(engine, settings, error)) return false;

    switch (settings.side) {
    case PlaySettings::PlayWhite: m_human = White; break;
    case PlaySettings::PlayBlack: m_human = Black; break;
    case PlaySettings::PlayRandom: m_human = QRandomGenerator::global()->bounded(2) ? White : Black; break;
    }

    m_starting = true;
    const Position start = settings.fromCurrentPosition ? m_session->current()->pos : Position::startPos();
    m_session->newGame(start);
    m_starting = false;

    if (!settings.openingMoves.isEmpty()) { // imposed opening: its moves go into the game
        Game line;
        if (pgn::parseGame(settings.openingMoves.toStdString() + " *", line) && line.root()->pos.fen() == m_session->current()->pos.fen()) {
            m_starting = true;
            for (const Node* n : line.mainline()) m_session->playMove(n->move);
            m_starting = false;
        }
        if (const eco::Opening* o = openingsBook().lookup(m_session->current()->pos)) {
            m_session->setHeader("ECO", o->eco);
            m_session->setHeader("Opening", o->name);
        }
    }
    m_session->setHeader("Event", tr("Partie contre le moteur").toStdString());
    m_session->setHeader("Date", QDate::currentDate().toString(QStringLiteral("yyyy.MM.dd")).toStdString());
    m_session->setHeader("Result", "*");
    if (settings.isTimed()) {
        const qint64 engineBase = settings.engineSeparate ? settings.engineBaseMs : settings.baseMs;
        const qint64 wBase = m_human == White ? settings.baseMs : engineBase;
        const qint64 bBase = m_human == Black ? settings.baseMs : engineBase;
        setupClock(wBase, bBase);
        // PGN time control "seconds+increment" (cannot be expressed if it differs between the sides)
        const std::string tc = wBase == bBase && m_clock->increment(White) == m_clock->increment(Black)
                                   ? std::to_string(wBase / 1000) + "+" + std::to_string(m_clock->increment(White) / 1000)
                                   : "?";
        m_session->setHeader("TimeControl", tc);
    } else {
        setupClock(0, 0);
    }
    m_active = true;
    updateHeaders();
    // The clock starts when the engine is ready (started signal).
    emit stateChanged();
    onPositionChanged(); // the engine starts if it has White
    return true;
}

bool EngineGame::resume(const EngineConfig& engine, const PlaySettings& settings, Color human, qint64 whiteMs, qint64 blackMs, QString* error)
{
    if (!launchEngine(engine, settings, error)) return false;
    m_human = human;
    setupClock(whiteMs, blackMs);
    m_active = true;
    m_suspended = true; // actual resumption when the Play page is shown (setSuspended(false))
    m_newMove = false;
    emit stateChanged();
    return true;
}

QJsonObject EngineGame::saveState() const
{
    if (!m_active) return {};
    const PlaySettings& s = m_settings;
    return QJsonObject{
        {"engineId", m_config.id},           {"enginePath", m_config.path},     {"human", m_human == White ? "w" : "b"},
        {"elo", s.elo},                      {"fullStrength", s.fullStrength},  {"moveTimeMs", s.moveTimeMs},
        {"tablebases", s.tablebases},        {"beginnerLevel", s.beginnerLevel},
        {"depth", s.depth},                  {"coach", s.coach},                {"coachThreshold", s.coachThreshold},
        {"ponder", s.ponder},
        {"playerName", s.playerName},        {"baseMs", s.baseMs},              {"incMs", s.incMs},
        {"engineSeparate", s.engineSeparate}, {"engineBaseMs", s.engineBaseMs}, {"engineIncMs", s.engineIncMs},
        {"whiteMs", m_clock->remaining(White)}, {"blackMs", m_clock->remaining(Black)},
        {"flagless", m_clock->isFlagless(White) ? "w" : m_clock->isFlagless(Black) ? "b" : ""},
    };
}

bool EngineGame::restoreState(const QJsonObject& o, QString* error)
{
    EngineConfig engine = EngineRegistry::instance().engine(o.value("engineId").toString());
    if (!engine.isValid()) { // engine outside the registry (command line) or removed since
        engine.path = o.value("enginePath").toString();
        engine.name = QFileInfo(engine.path).baseName();
        if (engine.path.isEmpty() || !QFileInfo::exists(engine.path)) {
            if (error) *error = tr("Le moteur de la partie en cours n'est plus disponible.");
            return false;
        }
    }
    PlaySettings s;
    s.elo = o.value("elo").toInt(1800);
    s.fullStrength = o.value("fullStrength").toBool();
    s.tablebases = o.value("tablebases").toBool(true);
    s.beginnerLevel = std::clamp(o.value("beginnerLevel").toInt(), 0, 3);
    if (s.beginnerLevel > 0) s.fullStrength = false;
    s.moveTimeMs = o.value("moveTimeMs").toInt(1000);
    s.depth = o.value("depth").toInt();
    s.coach = o.value("coach").toBool();
    s.coachThreshold = o.value("coachThreshold").toDouble(0.2);
    s.ponder = o.value("ponder").toBool();
    s.playerName = o.value("playerName").toString();
    s.baseMs = o.value("baseMs").toInteger();
    s.incMs = o.value("incMs").toInteger();
    s.engineSeparate = o.value("engineSeparate").toBool();
    s.engineBaseMs = o.value("engineBaseMs").toInteger();
    s.engineIncMs = o.value("engineIncMs").toInteger();
    s.engineId = engine.id;
    const Color human = o.value("human").toString() == QLatin1String("b") ? Black : White;
    if (!resume(engine, s, human, o.value("whiteMs").toInteger(), o.value("blackMs").toInteger(), error)) return false;
    const QString flagless = o.value("flagless").toString(); // game continued after a flag fall
    if (!flagless.isEmpty()) m_clock->setFlagless(flagless == QLatin1String("b") ? Black : White, true);
    return true;
}

void EngineGame::updateHeaders()
{
    if (!m_active) return;
    const std::string you = (m_settings.playerName.trimmed().isEmpty() ? tr("Vous") : m_settings.playerName.trimmed()).toStdString();
    const std::string engine = engineLabel().toStdString();
    m_session->setHeader("White", m_human == White ? you : engine);
    m_session->setHeader("Black", m_human == Black ? you : engine);
}

void EngineGame::stopPondering()
{
    if (!m_pondering) return;
    m_pondering = false;
    m_engine->stop();
}

QString EngineGame::searchArgs() const
{
    // The engine plays the side whose flag fell (game continued): no time left to manage
    if (isTimed() && m_clock->isFlagless(~m_human)) return QStringLiteral("movetime %1").arg(std::max(100, m_settings.moveTimeMs));
    if (isTimed()) // the engine manages its time by the clock
        return QStringLiteral("wtime %1 btime %2 winc %3 binc %4")
            .arg(m_clock->remaining(chess::White))
            .arg(m_clock->remaining(chess::Black))
            .arg(m_clock->increment(chess::White))
            .arg(m_clock->increment(chess::Black));
    return m_settings.depth > 0 ? QStringLiteral("depth %1").arg(m_settings.depth) : QStringLiteral("movetime %1").arg(m_settings.moveTimeMs);
}

void EngineGame::stop()
{
    m_clock->stop();
    stopPondering();
    if (!m_active) return;
    m_active = false;
    m_halted = false;
    m_engine->stop();
    setThinking(false);
    emit stateChanged();
}

void EngineGame::setSuspended(bool suspended)
{
    if (suspended == m_suspended) return;
    m_suspended = suspended;
    if (suspended) {
        stopPondering();
        if (m_thinking) {
            m_engine->stop();
            setThinking(false);
        }
        m_clock->stop();
        emit stateChanged();
    } else {
        m_newMove = false; // swapping games is not a move played
        m_deleted = false; // nor a deletion
        // Coming back to the page (or opening the application) is not resuming play: never an engine
        // thinking nor a clock running by itself. Engine to move: it stays stopped until Play (Space).
        // Player to move: the game restarts with their move (their clock then starts).
        if (m_active) {
            m_paused = true;
            m_halted = m_halted || lastNode()->pos.sideToMove() != m_human;
        }
        onPositionChanged();
    }
}

void EngineGame::resign()
{
    if (!m_active) return;
    finish(m_human == White ? "0-1" : "1-0", tr("Abandon"));
}

void EngineGame::takeBack()
{
    if (!m_active) return;
    Node* n = m_session->current();
    // Go back to the last position where it is the player's turn, undoing at least one of their moves.
    if (n->pos.sideToMove() == m_human) n = n->parent ? n->parent->parent : nullptr;
    else n = n->parent;
    if (!n) return;
    stopPondering();
    m_engine->stop();
    setThinking(false);
    if (isTimed()) m_clock->setRunning(m_human); // the player moves again: their time is the one running
    m_session->goTo(n);
}

void EngineGame::setThinking(bool on)
{
    if (m_thinking == on) return;
    m_thinking = on;
    emit stateChanged();
}

void EngineGame::playHumanMove(Move m)
{
    Node* cur = m_session->current();
    // After a deletion, playing a move of the engine's side swaps the sides: it will reply with the other one
    if (m_awaiting && isAtGameEnd() && cur->pos.sideToMove() != m_human) {
        m_human = cur->pos.sideToMove();
        m_awaiting = false;
        updateHeaders();
    }
    for (const auto& child : cur->children)
        if (child->move == m && !child->children.empty()) {
            m_session->deleteNode(child.get()); // old continuation dropped
            break;
        }
    m_session->playMove(m);
}

Node* EngineGame::lastNode() const
{
    Node* n = m_session->game().root();
    while (n->mainChild()) n = n->mainChild();
    return n;
}

bool EngineGame::isAtGameEnd() const
{
    const Node* cur = m_session->current();
    return cur->children.empty() && cur->isMainline();
}

void EngineGame::onPositionChanged()
{
    if (!m_active || m_suspended || m_coachPending) return;
    Node* cur = m_session->current();

    // A move has just been played from a past position (implicit take-back): it creates a
    // branch, which is nevertheless the game actually played → it is promoted to the main line.
    // Merely browsing an old variation does not promote it.
    const bool newMove = m_newMove && cur->children.empty() && cur->parent;
    if (newMove)
        while (!cur->isMainline()) m_session->promoteVariation(cur);
    m_newMove = false;
    // Move deleted by hand, and it is the engine's turn: it does not start again by itself
    const bool deleted = m_deleted;
    m_deleted = false;
    if (newMove) m_awaiting = false, m_paused = false;
    else if (deleted && !m_halted && lastNode()->pos.sideToMove() != m_human) m_awaiting = true;

    // Engine stopped (Stop button): moves are free, clock stopped, no reply.
    if (m_halted) {
        emit stateChanged();
        return;
    }

    if (m_paused) { // resumed at launch, nothing played yet: everything waits
        emit stateChanged();
        return;
    }

    // Clock: the side that just moved gets its increment; its time is recorded on the move.
    if (newMove && isTimed()) {
        const Color mover = ~cur->pos.sideToMove();
        if (!m_clock->isRunning()) m_clock->start(mover);
        m_clock->moveMade(mover);
        cur->clockMs = int(m_clock->remaining(mover));
    }

    // The engine only plays at the last move of the game: going back to look at
    // the game does not make it move again (and interrupts its thinking in progress).
    if (!isAtGameEnd()) {
        stopPondering();
        if (m_thinking) {
            m_engine->stop();
            setThinking(false);
        }
        // Engine paused by the player's browsing: its clock stops too.
        if (isTimed() && lastNode()->pos.sideToMove() != m_human) m_clock->stop();
        emit stateChanged();
        return;
    }
    if (m_awaiting) {
        if (cur->pos.sideToMove() != m_human) { // it waits for Play, or a move of its side (sides swapped)
            stopPondering();
            if (m_thinking) {
                m_engine->stop();
                setThinking(false);
            }
            if (isTimed()) m_clock->stop();
            emit stateChanged();
            return;
        }
        m_awaiting = false; // it is the player's turn: the game goes on normally
    }
    if (isTimed() && m_engineReady && !m_clock->isRunning()) m_clock->start(cur->pos.sideToMove());

    GameEnd end;
    if (detectGameEnd(cur, end)) {
        finish(end.result, display::endReason(end.kind));
        return;
    }
    if (cur->pos.sideToMove() != m_human && m_settings.coach && m_coachHook && cur->parent && m_coachChecked != cur->pos.fen()) {
        // Coach: the player's move is checked before the engine's reply (clocks stopped).
        m_coachPending = true;
        if (isTimed()) m_clock->stop();
        const std::string fen = cur->pos.fen();
        m_coachHook(cur->parent->pos, cur->move, [this, fen](bool keep) {
            m_coachPending = false;
            if (!m_active) return;
            Node* now = m_session->current();
            if (now->pos.fen() != fen) return; // the game changed in the meantime
            if (!keep) {
                m_session->goTo(now->parent);
                m_session->deleteNode(now);
                if (isTimed()) m_clock->setRunning(m_human);
                emit stateChanged();
                return;
            }
            m_coachChecked = fen;
            onPositionChanged();
        });
        emit stateChanged();
        return;
    }
    if (cur->pos.sideToMove() != m_human && m_pondering) {
        // Pondering: the expected move was played → the search goes on (ponderhit);
        // otherwise it is dropped and a new one starts below.
        m_pondering = false;
        if (cur->pos.fen() == m_ponderFen) {
            ++m_ponderHits;
            setThinking(true);
            m_searchPos = cur->pos;
            emit searchStarted();
            m_engine->ponderHit();
            emit stateChanged();
            return;
        }
        m_engine->stop();
    }
    if (cur->pos.sideToMove() != m_human) {
        // Book move: played after a short moment (the player's move stays visible).
        if (m_useBook && m_bookProvider) {
            const Move bm = m_bookProvider(cur->pos);
            if (!bm.isNull() && cur->pos.isLegal(bm)) {
                if (m_thinking) {
                    m_engine->stop();
                    setThinking(false);
                }
                m_bookPending = true;
                const std::string fen = cur->pos.fen();
                QTimer::singleShot(400, this, [this, fen, bm] {
                    m_bookPending = false;
                    const Node* now = m_session->current();
                    if (!m_active || m_suspended || m_halted || !isAtGameEnd() || now->pos.fen() != fen || now->pos.sideToMove() == m_human) {
                        emit stateChanged();
                        return;
                    }
                    emit bookMovePlayed(bm);
                    m_session->playMove(bm);
                });
                emit stateChanged();
                return;
            }
        }
        setThinking(true);
        const QString go = searchArgs();
        m_searchPos = cur->pos;
        m_lines.clear();
        emit searchStarted();
        m_engine->search(QString::fromStdString(cur->pos.fen()), go);
    } else if (m_thinking) {
        m_engine->stop();
        setThinking(false);
    }
    emit stateChanged();
}

void EngineGame::onBestMove(const QString& uci, const QString& fen)
{
    if (!m_active || m_suspended || m_halted || m_paused) return;
    const Position& pos = m_session->current()->pos;
    if (QString::fromStdString(pos.fen()) != fen || pos.sideToMove() == m_human) return; // stale move
    Move m = pos.parseUci(uci.toStdString());
    setThinking(false);
    if (m.isNull()) return;
    if (m_settings.beginnerLevel > 0 && m_lines.size() > 1) { // beginner level: sometimes a weaker move
        const QList<EngineLine> lines = m_lines.values();
        const int k = pickBeginnerLine(lines, m_settings.beginnerLevel, QRandomGenerator::global()->generateDouble());
        const Move alt = k >= 0 ? pos.parseUci(lines[k].pv.first().toStdString()) : Move{};
        if (!alt.isNull() && pos.isLegal(alt)) m = alt;
    }
    const QString ponder = m_engine->ponderMove();
    m_session->playMove(m);
    // Pondering on the player's expected reply
    if (m_settings.ponder && m_active && !m_halted && !m_suspended && !ponder.isEmpty()) {
        const Position& now = m_session->current()->pos;
        const Move expected = now.parseUci(ponder.toStdString());
        if (!expected.isNull() && now.isLegal(expected) && now.sideToMove() == m_human) {
            const Position next = now.after(expected);
            if (!next.legalMoves().empty()) {
                m_ponderFen = next.fen();
                m_pondering = true;
                m_engine->ponder(QString::fromStdString(m_ponderFen), searchArgs());
            }
        }
    }
}

int EngineGame::pickBeginnerLine(const QList<EngineLine>& lines, int level, double roll)
{
    if (lines.isEmpty()) return -1;
    auto value = [](const EngineLine& l) { return l.mate ? (l.score > 0 ? 20000 - l.score : -20000 - l.score) : l.score; };
    int best = value(lines.first());
    for (const EngineLine& l : lines) best = std::max(best, value(l));
    // Tolerated loss and "softness" of the draw depending on the level (1: the weakest)
    static const int maxLoss[4] = {0, 450, 250, 120};
    static const double scale[4] = {1, 140, 80, 40};
    const int lv = std::clamp(level, 1, 3);
    QList<double> weights;
    double total = 0;
    for (const EngineLine& l : lines) {
        const int loss = best - value(l);
        const double w = l.pv.isEmpty() || loss > maxLoss[lv] ? 0 : 1.0 / (1.0 + loss / scale[lv]);
        weights << w;
        total += w;
    }
    if (total <= 0) return 0;
    double r = roll * total;
    for (int i = 0; i < weights.size(); ++i) {
        if (r < weights[i]) return i;
        r -= weights[i];
    }
    return int(weights.size()) - 1;
}

void EngineGame::onFlag(Color side)
{
    if (!m_active) return;
    m_flagEnded = true;
    m_flagSide = side;
    // Time ran out: lost, unless the opponent cannot mate (lone king, or king + one minor piece).
    const Position& pos = lastNode()->pos;
    int material = 0;
    bool heavy = false;
    for (int s = 0; s < 64; ++s) {
        const Piece p = pos.at(Square(s));
        if (p == NoPiece || colorOf(p) == side || typeOf(p) == King) continue;
        ++material;
        if (typeOf(p) == Pawn || typeOf(p) == Rook || typeOf(p) == Queen) heavy = true;
    }
    if (!heavy && material <= 1) finish("1/2-1/2", tr("Temps écoulé — matériel insuffisant pour mater"));
    else finish(side == White ? "0-1" : "1-0", tr("Temps écoulé"));
}

void EngineGame::finish(const std::string& result, const QString& reason)
{
    m_clock->stop();
    m_active = false;
    m_engine->stop();
    setThinking(false);
    m_session->setHeader("Result", result);
    m_session->setHeader("Termination", reason.toStdString());
    emit stateChanged();
    emit finished(QString::fromStdString(result), reason);
}

bool EngineGame::continueGame(QString* error)
{
    if (!canContinue()) return false;
    const Color flagged = m_flagSide;
    if (!m_engine->isRunning()) { // engine stopped since: restarted with the same settings, clocks kept
        const qint64 whiteMs = m_clock->remaining(White), blackMs = m_clock->remaining(Black);
        const EngineConfig registry = EngineRegistry::instance().engine(m_config.id);
        if (!launchEngine(registry.isValid() ? registry : m_config, m_settings, error)) return false;
        setupClock(whiteMs, blackMs);
    }
    m_flagEnded = false;
    m_clock->setFlagless(flagged, true);
    m_active = true;
    m_halted = false;
    m_paused = false;
    m_newMove = false;
    m_deleted = false;
    // Engine to move: it waits for Play, or a move of its side (sides swapped)
    m_awaiting = lastNode()->pos.sideToMove() != m_human;
    m_session->setHeader("Result", "*");
    m_session->setHeader("Termination", "");
    updateHeaders();
    emit stateChanged();
    return true;
}

bool EngineGame::changeEngine(const EngineConfig& registryEngine, QString* error)
{
    const EngineConfig engine = tabengine::effective(QLatin1String(tabengine::PlayTab), registryEngine);
    if (!m_active) return false;
    if (!engine.isValid()) {
        if (error) *error = tr("Moteur introuvable.");
        return false;
    }
    // Same game, same clock, same side: only the opponent changes. The new engine's
    // startup time is not deducted (the clock restarts when it is ready).
    m_clock->stop();
    m_engine->stop();
    setThinking(false);
    PlaySettings settings = m_settings;
    settings.engineId = engine.id;
    m_engine->quit();
    m_engineReady = false;
    if (!m_engine->start(engine.path)) {
        if (error) *error = tr("Impossible de lancer le moteur :\n%1").arg(engine.path);
        finish("*", tr("Le moteur s'est arrêté"));
        return false;
    }
    m_config = engine;
    if (engine.id.isEmpty()) {
        m_engine->setOption(QStringLiteral("Threads"), QString::number(std::max(1, QThread::idealThreadCount() / 2)));
        m_engine->setOption(QStringLiteral("Hash"), QStringLiteral("256"));
    }
    for (auto it = engine.values.cbegin(); it != engine.values.cend(); ++it) m_engine->setOption(it.key(), it.value());
    m_settings = settings;
    m_engine->setOption(QStringLiteral("UCI_LimitStrength"), settings.fullStrength && settings.beginnerLevel == 0 ? QStringLiteral("false") : QStringLiteral("true"));
    sendSkillLevel();
    if (!settings.fullStrength || settings.beginnerLevel > 0)
        m_engine->setOption(QStringLiteral("UCI_Elo"), QString::number(settings.beginnerLevel > 0 ? 1320 : settings.elo));
    if (module::supportsMultiPv(engine))
        m_engine->setOption(QStringLiteral("MultiPV"), QString::number(settings.beginnerLevel > 0 ? std::max(m_multiPv, 6) : m_multiPv));
    m_engine->newGame();
    updateHeaders(); // opponent's name in the game header
    m_newMove = false;
    if (!m_suspended) onPositionChanged(); // if it is to move, it thinks as soon as it is ready
    emit stateChanged();
    return true;
}

QString EngineGame::strengthText(const PlaySettings& settings)
{
    if (settings.beginnerLevel > 0) return tr("débutant %1").arg(settings.beginnerLevel);
    if (!settings.fullStrength) return tr("Elo %1").arg(settings.elo);
    return settings.tablebases ? tr("force maximale avec tables de finales") : tr("force maximale");
}

// "Skill Level" (Stockfish and derivatives): ignored by the engine as long as strength is limited by
// Elo, but active at full strength. "Maximum strength" therefore sets it to its maximum, whatever the
// engine's saved setting.
void EngineGame::sendSkillLevel()
{
    if (!m_settings.fullStrength || m_settings.beginnerLevel > 0) return;
    if (const UciOption* skill = m_config.option(QStringLiteral("Skill Level"))) m_engine->setOption(skill->name, QString::number(skill->max));
    // "UCI_Elo" has no effect at full strength (limitation off); it is still set to the maximum,
    // so that the engine settings say the same thing as the slider
    if (const UciOption* elo = m_config.option(QStringLiteral("UCI_Elo"))) m_engine->setOption(elo->name, QString::number(elo->max));
}

QMap<QString, QString> EngineGame::managedOptions() const
{
    QMap<QString, QString> out;
    if (!m_active) return out;
    const bool limited = !m_settings.fullStrength || m_settings.beginnerLevel > 0;
    out[QStringLiteral("UCI_LimitStrength")] = limited ? QStringLiteral("true") : QStringLiteral("false");
    // At full strength, Elo has no effect (limitation off) but is set to the engine's maximum
    const UciOption* eloOption = m_config.option(QStringLiteral("UCI_Elo"));
    out[QStringLiteral("UCI_Elo")] = limited ? QString::number(m_settings.beginnerLevel > 0 ? 1320 : m_settings.elo) : eloOption ? QString::number(eloOption->max) : QString();
    out[QStringLiteral("MultiPV")] = QString::number(m_settings.beginnerLevel > 0 ? std::max(m_multiPv, 6) : m_multiPv);
    // Limited by Elo: the engine ignores "Skill Level"; at full strength, the game sets it to the maximum
    if (const UciOption* skill = m_config.option(QStringLiteral("Skill Level"))) out[skill->name] = limited ? QString() : QString::number(skill->max);
    return out;
}

bool EngineGame::hasStrengthControl() const
{
    return m_config.option(QStringLiteral("UCI_Elo")) || m_config.options.isEmpty(); // unknown options: we assume
}

bool EngineGame::setStrength(bool fullStrength, int elo, int beginnerLevel, bool tablebases)
{
    beginnerLevel = std::clamp(beginnerLevel, 0, 3);
    if (beginnerLevel > 0) fullStrength = false;
    if (!m_active || !m_engine->isRunning() || !hasStrengthControl()) return false;
    PlaySettings settings = m_settings;
    settings.fullStrength = fullStrength;
    settings.elo = elo;
    settings.beginnerLevel = beginnerLevel;
    settings.tablebases = tablebases;
    const QString before = strengthText(m_settings), after = strengthText(settings);
    if (before == after) return false;
    const bool beginnerChanged = (m_settings.beginnerLevel > 0) != (beginnerLevel > 0);
    m_settings = settings;
    // Sent as soon as the engine is idle: the thinking in progress ends at the old strength
    m_engine->setOption(QStringLiteral("UCI_LimitStrength"), fullStrength ? QStringLiteral("false") : QStringLiteral("true"));
    sendSkillLevel();
    if (!fullStrength) m_engine->setOption(QStringLiteral("UCI_Elo"), QString::number(beginnerLevel > 0 ? 1320 : elo));
    // Beginner: it needs several lines to sometimes choose a weaker move
    if (beginnerChanged && module::supportsMultiPv(m_config))
        m_engine->setOption(QStringLiteral("MultiPV"), QString::number(beginnerLevel > 0 ? std::max(m_multiPv, 6) : m_multiPv));
    if (beginnerChanged) m_lines.clear();
    // Endgame tablebases: only at full strength, if the level includes them (passed on or removed at the next search)
    m_engine->setTablebaseRole(UciEngine::TablebaseRole::Play, fullStrength && tablebases);
    updateHeaders();
    emit stateChanged();
    return true;
}

void EngineGame::moveNow()
{
    if (!m_active || m_suspended) return;
    if (m_thinking) { // it is thinking: it plays its best move immediately
        m_engine->moveNow();
        return;
    }
    // Otherwise it takes the side to move (in the displayed position): the player takes the other.
    if (!isAtGameEnd()) {
        // Resuming from a past position: the continuation is dropped, as for a move played.
        Node* cur = m_session->current();
        while (!cur->children.empty()) m_session->deleteNode(cur->children.front().get());
        while (!cur->isMainline()) m_session->promoteVariation(cur);
    }
    m_human = ~m_session->current()->pos.sideToMove();
    m_halted = false;
    m_paused = false;
    m_awaiting = false;
    updateHeaders();
    if (isTimed() && m_engineReady) m_clock->start(m_session->current()->pos.sideToMove());
    m_newMove = false; // the deletions above are not a move played
    m_deleted = false; // nor a deletion made by hand
    onPositionChanged();
}

void EngineGame::setUseBook(bool on)
{
    if (m_useBook == on) return;
    m_useBook = on;
    QSettings().setValue(QStringLiteral("play/useBook"), on);
    emit stateChanged();
}

void EngineGame::halt()
{
    if (!m_active || m_halted) return;
    m_halted = true;
    stopPondering();
    m_engine->stop();
    setThinking(false);
    m_clock->stop();
    emit stateChanged();
}

void EngineGame::setMultiPv(int n)
{
    n = std::clamp(n, 1, MaxMultiPv);
    if (n == m_multiPv) return;
    m_multiPv = n;
    QSettings().setValue(QStringLiteral("playVariants"), n);
    if (m_engine->isRunning() && module::supportsMultiPv(m_config)) m_engine->setOption(QStringLiteral("MultiPV"), QString::number(n)); // taken into account at the next search
}

void EngineGame::applyRegistryChanges()
{
    if (!m_active || m_config.id.isEmpty() || !m_engine->isRunning()) return;
    const EngineConfig cfg = tabengine::effective(QLatin1String(tabengine::PlayTab), EngineRegistry::instance().engine(m_config.id));
    if (!cfg.isValid()) return;
    // Options driven by the game itself: never overwritten.
    static const QStringList managed = {QStringLiteral("UCI_LimitStrength"), QStringLiteral("UCI_Elo"), QStringLiteral("MultiPV"), QStringLiteral("Skill Level")};
    QStringList names;
    for (const UciOption& o : cfg.options) names << o.name;
    for (auto it = m_config.values.cbegin(); it != m_config.values.cend(); ++it)
        if (!names.contains(it.key())) names << it.key();
    for (const QString& name : std::as_const(names)) {
        if (managed.contains(name, Qt::CaseInsensitive)) continue;
        const UciOption* o = cfg.option(name);
        if (o && o->type == QLatin1String("button")) continue;
        // Effective value: changed, otherwise the default one (going back to the default counts too).
        const QString now = cfg.value(name), before = m_config.value(name);
        if (now != before && !now.isEmpty()) m_engine->setOption(name, now); // sent as soon as the engine is idle
    }
    m_config = cfg;
    updateHeaders(); // the name may have changed
}

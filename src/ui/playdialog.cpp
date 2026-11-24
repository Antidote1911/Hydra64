#include "playdialog.h"
#include <QTimer>
#include <QRegularExpression>
#include "chess/pgn.h"
#include "openings.h"
#include "ecobrowser.h"
#include "theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

const char* kCustom = "custom";

// "base+increment" in seconds; "0+0" = no clock
void applyTimeControl(PlaySettings& p, const QString& tc, int customMin, int customInc, bool separate, int engineMin, int engineInc)
{
    if (tc == QLatin1String(kCustom)) {
        p.baseMs = qint64(customMin) * 60000;
        p.incMs = qint64(customInc) * 1000;
    } else {
        const QStringList parts = tc.split('+');
        p.baseMs = parts.value(0).toLongLong() * 1000;
        p.incMs = parts.value(1).toLongLong() * 1000;
    }
    p.engineSeparate = separate && p.baseMs > 0;
    p.engineBaseMs = qint64(engineMin) * 60000;
    p.engineIncMs = qint64(engineInc) * 1000;
}

QSpinBox* spin(int min, int max, int value, const QString& suffix)
{
    auto* sb = new QSpinBox;
    sb->setRange(min, max);
    sb->setValue(value);
    sb->setSuffix(suffix);
    sb->setFixedWidth(90);
    return sb;
}

QLabel* sectionLabel(const QString& text)
{
    auto* l = new QLabel(text.toUpper());
    l->setObjectName("Faint");
    QFont f = l->font();
    f.setPixelSize(10);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    l->setFont(f);
    return l;
}

} // namespace

PlayDialog::PlayDialog(const QString& preferredId, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Jouer contre le moteur"));
    setMinimumWidth(420);
    QSettings s;
    s.beginGroup("play");

    auto* title = new QLabel(tr("Nouvelle partie contre le moteur"));
    QFont tf = title->font();
    tf.setPixelSize(16);
    tf.setWeight(QFont::DemiBold);
    title->setFont(tf);

    // Opponent engine
    m_engineBox = new QComboBox;
    const EngineRegistry& reg = EngineRegistry::instance();
    for (const EngineConfig& e : reg.engines()) m_engineBox->addItem(e.name, e.id);
    QString pick = s.value("engine").toString();
    if (reg.engine(pick).id.isEmpty()) pick = preferredId;
    if (reg.engine(pick).id.isEmpty()) pick = reg.fallbackEngine().id;
    m_engineBox->setCurrentIndex(std::max(0, m_engineBox->findData(pick)));

    m_name = new QLineEdit(s.value("playerName", savedSettings().playerName).toString());
    m_name->setPlaceholderText(tr("Nom inscrit dans la partie"));

    // Side: segmented buttons
    auto* sideRow = new QHBoxLayout;
    sideRow->setSpacing(0);
    m_side = new QButtonGroup(this);
    const std::pair<PlaySettings::Side, QString> sides[] = {
        {PlaySettings::PlayWhite, tr("Blancs")}, {PlaySettings::PlayBlack, tr("Noirs")}, {PlaySettings::PlayRandom, tr("Au hasard")}};
    for (const auto& [side, label] : sides) {
        auto* b = new QPushButton(label);
        b->setObjectName("Segment");
        b->setCheckable(true);
        b->setProperty("position", side == PlaySettings::PlayWhite ? "first" : side == PlaySettings::PlayRandom ? "last" : "middle");
        m_side->addButton(b, side);
        sideRow->addWidget(b);
    }
    m_side->button(s.value("side", int(PlaySettings::PlayWhite)).toInt())->setChecked(true);

    // Strength
    m_strength = new StrengthSlider;
    m_strength->setObjectName("PlayStrengthSlider");
    m_strength->setStrength(s.value("full", false).toBool(), s.value("elo", 1800).toInt(), std::clamp(s.value("beginner", 0).toInt(), 0, 3), s.value("tables", true).toBool());
    m_noStrength = new QLabel(tr("Ce moteur ne propose pas de réglage de force."));
    m_noStrength->setObjectName("Faint");

    // Time per move
    m_time = new QComboBox;
    for (int ms : {300, 1000, 2000, 5000, 10000}) m_time->addItem(ms < 1000 ? tr("%1 s").arg(ms / 1000.0, 0, 'f', 1) : tr("%1 s").arg(ms / 1000), ms);
    // Fixed depth (negative value in the list): same strength whatever the machine
    for (int d : {4, 8, 12, 16, 20}) m_time->addItem(tr("Profondeur %1").arg(d), -d);
    m_time->setCurrentIndex(std::max(0, m_time->findData(s.value("moveTime", 1000).toInt())));

    // Time control
    m_tc = new QComboBox;
    for (const auto& [code, label] : timeControls()) m_tc->addItem(label, code);
    m_tc->setCurrentIndex(std::max(0, m_tc->findData(savedTimeControl())));

    m_customMin = spin(1, 300, s.value("customMin", 10).toInt(), tr(" min"));
    m_customInc = spin(0, 180, s.value("customInc", 5).toInt(), tr(" s"));
    m_customRow = new QWidget;
    auto* cr = new QHBoxLayout(m_customRow);
    cr->setContentsMargins(0, 0, 0, 0);
    cr->addWidget(new QLabel(tr("Temps")));
    cr->addWidget(m_customMin);
    cr->addSpacing(12);
    cr->addWidget(new QLabel(tr("Incrément par coup")));
    cr->addWidget(m_customInc);
    cr->addStretch(1);

    m_engineSeparate = new QCheckBox(tr("Cadence différente pour le moteur (handicap)"));
    m_engineSeparate->setChecked(s.value("engineSeparate", false).toBool());
    m_engineMin = spin(1, 300, s.value("engineMin", 1).toInt(), tr(" min"));
    m_engineInc = spin(0, 180, s.value("engineInc", 0).toInt(), tr(" s"));
    m_engineRow = new QWidget;
    auto* er = new QHBoxLayout(m_engineRow);
    er->setContentsMargins(0, 0, 0, 0);
    er->addWidget(new QLabel(tr("Moteur")));
    er->addWidget(m_engineMin);
    er->addSpacing(12);
    er->addWidget(new QLabel(tr("Incrément par coup")));
    er->addWidget(m_engineInc);
    er->addStretch(1);

    m_fromCurrent = new QCheckBox(tr("Partir de la position actuelle"));
    m_opening = new QCheckBox(tr("Ouverture imposée"));
    m_opening->setToolTip(tr("Les coups de l'ouverture sont joués d'office, la partie commence ensuite"));
    m_openingText = new QLineEdit(s.value("opening").toString());
    m_openingText->setPlaceholderText(tr("code ECO (« B90 ») ou coups (« 1. e4 c5 2. Nf3 »)"));
    m_openingText->setClearButtonEnabled(true);
    m_openingPick = new QPushButton(theme::icon("book"), QString());
    m_openingPick->setToolTip(tr("Choisir dans le navigateur ECO"));
    m_openingName = new QLabel;
    m_openingName->setObjectName("Faint");
    m_openingName->setWordWrap(true);
    m_opening->setChecked(s.value("openingOn", false).toBool());
    auto updateOpening = [this] {
        const bool on = m_opening->isChecked();
        m_openingText->setEnabled(on);
        m_openingPick->setEnabled(on);
        if (on) m_fromCurrent->setChecked(false);
        const QString line = openingLine(m_openingText->text());
        QString name;
        if (!line.isEmpty()) {
            chess::Game g;
            chess::pgn::parseGame(line.toStdString() + " *", g);
            const chess::Position end = g.mainline().empty() ? g.root()->pos : g.mainline().back()->pos;
            const chess::eco::Opening* o = openingsBook().lookup(end);
            name = o ? QStringLiteral("%1 %2 — %3").arg(QString::fromStdString(o->eco), openingName(o->name), line) : line;
        } else if (!m_openingText->text().trimmed().isEmpty()) {
            name = tr("Ligne non reconnue");
        }
        m_openingName->setText(name);
        m_openingName->setVisible(on && !name.isEmpty());
    };
    connect(m_opening, &QCheckBox::toggled, this, updateOpening);
    connect(m_openingText, &QLineEdit::textChanged, this, updateOpening);
    connect(m_fromCurrent, &QCheckBox::toggled, this, [this](bool on) {
        if (on) m_opening->setChecked(false);
    });
    connect(m_openingPick, &QPushButton::clicked, this, [this] {
        EcoBrowser browser({}, this);
        connect(&browser, &EcoBrowser::lineRequested, &browser, [this, &browser](const QString& moves) {
            m_openingText->setText(moves);
            browser.accept();
        });
        browser.exec();
    });
    QTimer::singleShot(0, this, updateOpening);
    m_coach = new QCheckBox(tr("Coach : signaler mes coups faibles et proposer de les reprendre"));
    m_coach->setChecked(s.value("coach", false).toBool());
    m_coach->setToolTip(tr("Après chacun de vos coups, un Stockfish à part vérifie le coup avant la réponse de l'adversaire"));
    m_coachLevel = new QComboBox;
    m_coachLevel->addItem(tr("dès l'imprécision (?!)"), 0.1);
    m_coachLevel->addItem(tr("dès l'erreur (?)"), 0.2);
    m_coachLevel->addItem(tr("seulement les gaffes (?\?)"), 0.3);
    m_coachLevel->setCurrentIndex(std::clamp(s.value("coachLevel", 1).toInt(), 0, 2));
    m_ponder = new QCheckBox(tr("Réflexion permanente : le moteur réfléchit aussi pendant votre temps"));
    m_ponder->setChecked(s.value("ponder", false).toBool());
    m_ponder->setToolTip(tr("Option UCI Ponder : plus fort à la même cadence, mais il utilise les processeurs pendant que vous réfléchissez"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Jouer"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("Primary");
    buttons->button(QDialogButtonBox::Ok)->setIcon(theme::icon("play"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addSpacing(10);
    if (m_engineBox->count() > 0) {
        layout->addWidget(sectionLabel(tr("Moteur")));
        layout->addWidget(m_engineBox);
        layout->addSpacing(10);
    }
    layout->addWidget(sectionLabel(tr("Votre nom")));
    layout->addWidget(m_name);
    layout->addSpacing(10);
    layout->addWidget(sectionLabel(tr("Votre camp")));
    layout->addLayout(sideRow);
    layout->addSpacing(10);
    layout->addWidget(sectionLabel(tr("Force du moteur")));
    layout->addWidget(m_strength);
    layout->addWidget(m_noStrength);
    layout->addSpacing(10);
    layout->addWidget(sectionLabel(tr("Cadence")));
    layout->addWidget(m_tc);
    layout->addWidget(m_customRow);
    layout->addWidget(m_engineSeparate);
    layout->addWidget(m_engineRow);
    m_moveTimeBox = new QWidget;
    auto* mt = new QVBoxLayout(m_moveTimeBox);
    mt->setContentsMargins(0, 4, 0, 0);
    mt->setSpacing(6);
    mt->addWidget(sectionLabel(tr("Temps de réflexion du moteur par coup")));
    mt->addWidget(m_time);
    layout->addWidget(m_moveTimeBox);
    layout->addSpacing(10);
    layout->addWidget(m_fromCurrent);
    {
        auto* openingRow = new QHBoxLayout;
        openingRow->addWidget(m_opening);
        openingRow->addWidget(m_openingText, 1);
        openingRow->addWidget(m_openingPick);
        layout->addLayout(openingRow);
        layout->addWidget(m_openingName);
    }
    {
        auto* coachRow = new QHBoxLayout;
        coachRow->addWidget(m_coach, 1);
        coachRow->addWidget(m_coachLevel);
        layout->addLayout(coachRow);
        layout->addWidget(m_ponder);
        m_coachLevel->setEnabled(m_coach->isChecked());
        connect(m_coach, &QCheckBox::toggled, m_coachLevel, &QWidget::setEnabled);
    }
    layout->addSpacing(14);
    layout->addWidget(buttons);

    connect(m_tc, &QComboBox::currentIndexChanged, this, &PlayDialog::updateTimeControl);
    connect(m_engineSeparate, &QCheckBox::toggled, this, &PlayDialog::updateTimeControl);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_engineBox, &QComboBox::currentIndexChanged, this, &PlayDialog::updateStrengthAvailability);
    updateTimeControl();
    updateStrengthAvailability();
}

void PlayDialog::updateStrengthAvailability()
{
    const EngineConfig e = EngineRegistry::instance().engine(m_engineBox->currentData().toString());
    const bool available = !e.isValid() || e.options.isEmpty() || e.option(QStringLiteral("UCI_Elo"));
    m_strength->setVisible(available);
    m_noStrength->setVisible(!available);
    adjustSize();
}

void PlayDialog::updateTimeControl()
{
    const QString tc = m_tc->currentData().toString();
    const bool timed = tc != QLatin1String("0+0");
    m_customRow->setVisible(tc == QLatin1String(kCustom));
    m_engineSeparate->setVisible(timed);
    m_engineRow->setVisible(timed && m_engineSeparate->isChecked());
    m_moveTimeBox->setVisible(!timed);
    adjustSize();
}

QList<QPair<QString, QString>> PlayDialog::timeControls()
{
    return {
        {QStringLiteral("0+0"), tr("Sans pendule (temps fixe par coup)")},
        {QStringLiteral("60+0"), tr("Bullet 1+0")},
        {QStringLiteral("120+1"), tr("Bullet 2+1")},
        {QStringLiteral("180+0"), tr("Blitz 3+0")},
        {QStringLiteral("180+2"), tr("Blitz 3+2")},
        {QStringLiteral("300+0"), tr("Blitz 5+0")},
        {QStringLiteral("300+3"), tr("Blitz 5+3")},
        {QStringLiteral("600+0"), tr("Rapide 10+0")},
        {QStringLiteral("600+5"), tr("Rapide 10+5")},
        {QStringLiteral("900+10"), tr("Rapide 15+10")},
        {QStringLiteral("1800+0"), tr("Classique 30+0")},
        {QStringLiteral("1800+20"), tr("Classique 30+20")},
        {QString::fromLatin1(kCustom), tr("Personnalisée…")},
    };
}

QString PlayDialog::savedTimeControl() { return QSettings().value("play/timeControl", QStringLiteral("300+3")).toString(); }

void PlayDialog::saveTimeControl(const QString& code) { QSettings().setValue("play/timeControl", code); }

QString PlayDialog::shortLabel(const PlaySettings& p)
{
    if (!p.isTimed()) return tr("Sans pendule");
    const qint64 sec = p.baseMs / 1000;
    const QString base = sec % 60 ? QStringLiteral("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QLatin1Char('0')) : QString::number(sec / 60);
    QString label = QStringLiteral("%1+%2").arg(base).arg(p.incMs / 1000);
    if (p.engineSeparate) label += QStringLiteral(" / %1+%2").arg(p.engineBaseMs / 60000).arg(p.engineIncMs / 1000);
    return label;
}

PlaySettings PlayDialog::savedSettings()
{
    QSettings s;
    s.beginGroup("play");
    PlaySettings p;
    p.side = PlaySettings::Side(std::clamp(s.value("side", int(PlaySettings::PlayWhite)).toInt(), 0, 2));
    p.elo = std::clamp(s.value("elo", 1800).toInt(), 1320, 3190);
    p.fullStrength = s.value("full", false).toBool();
    p.tablebases = s.value("tables", true).toBool();
    p.beginnerLevel = std::clamp(s.value("beginner", 0).toInt(), 0, 3);
    if (p.beginnerLevel > 0) p.fullStrength = false;
    const int saved = s.value("moveTime", 1000).toInt();
    p.moveTimeMs = saved > 0 ? std::max(50, saved) : 1000;
    p.depth = saved < 0 ? -saved : 0;
    p.coach = s.value("coach", false).toBool();
    p.coachThreshold = 0.1 * (1 + std::clamp(s.value("coachLevel", 1).toInt(), 0, 2));
    p.ponder = s.value("ponder", false).toBool();
    p.engineId = s.value("engine").toString();
    // By default, the account's full name (or the login), rather than "Vous".
    QString def = qEnvironmentVariable("USER");
    p.playerName = s.value("playerName", def).toString();
    applyTimeControl(p, savedTimeControl(), s.value("customMin", 10).toInt(), s.value("customInc", 5).toInt(),
                     s.value("engineSeparate", false).toBool(), s.value("engineMin", 1).toInt(), s.value("engineInc", 0).toInt());
    return p;
}

StrengthSlider::StrengthSlider(QWidget* parent)
    : QWidget(parent)
{
    m_label = new QLabel;
    m_label->setAlignment(Qt::AlignCenter);
    m_slider = new QSlider(Qt::Horizontal);
    // Levels: 3 beginner, the Elos, maximum strength, maximum strength + tablebases
    m_slider->setRange(0, 3 + EloSteps + 1);
    m_slider->setSingleStep(1);
    m_slider->setPageStep(4);
    m_slider->setTickPosition(QSlider::TicksBelow);
    m_slider->setTickInterval(1);
    m_slider->setMinimumWidth(300);
    m_slider->setToolTip(tr("Tout à gauche : niveaux débutant, sous le plus bas Elo du moteur (il choisit parfois un coup plus faible parmi ses lignes, comme un joueur débutant). "
                            "Tout à droite : force maximale, sans aucune limitation, puis avec les tables de finales."));
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    l->addWidget(m_slider);
    l->addWidget(m_label);
    connect(m_slider, &QSlider::valueChanged, this, [this] {
        refresh();
        emit changed();
    });
    refresh();
}

void StrengthSlider::setStrength(bool fullStrength, int elo, int beginnerLevel, bool tablebases)
{
    m_elo = std::clamp(elo, int(MinElo), int(MaxElo));
    int index = 0; // nearest Elo level
    for (int i = 1; i < EloSteps; ++i)
        if (std::abs(eloAt(i) - m_elo) < std::abs(eloAt(index) - m_elo)) index = i;
    m_slider->setValue(beginnerLevel > 0 ? std::min(beginnerLevel, 3) - 1 : fullStrength ? m_slider->maximum() - (tablebases ? 0 : 1) : 3 + index);
    refresh();
}

int StrengthSlider::step() const { return m_slider->value(); }
bool StrengthSlider::fullStrength() const { return m_slider->value() >= m_slider->maximum() - 1; }
bool StrengthSlider::tablebases() const { return m_slider->value() == m_slider->maximum(); }
int StrengthSlider::beginnerLevel() const { return m_slider->value() < 3 ? m_slider->value() + 1 : 0; }
int StrengthSlider::elo() const { return m_elo; }

QString StrengthSlider::text() const
{
    switch (beginnerLevel()) {
    case 1: return tr("Débutant 1 — le plus faible, gaffes fréquentes");
    case 2: return tr("Débutant 2 — erreurs fréquentes");
    case 3: return tr("Débutant 3 — sous 1320 Elo, erreurs rares");
    default: break;
    }
    if (!fullStrength()) return tr("Elo %1").arg(m_elo);
    if (!tablebases()) return tr("Force maximale");
    // Tablebases are only used if installed and given to games (Preferences → Tablebases)
    const UciEngine::Tablebases t = UciEngine::tablebases();
    return !t.path.isEmpty() && t.play ? tr("Force maximale + Tables de finales") : tr("Force maximale + Tables de finales (aucune table disponible)");
}

void StrengthSlider::refresh()
{
    if (beginnerLevel() == 0 && !fullStrength()) m_elo = eloAt(m_slider->value() - 3);
    m_label->setText(text());
}

PlaySettings PlayDialog::settings() const
{
    PlaySettings p;
    p.side = PlaySettings::Side(m_side->checkedId());
    p.elo = m_strength->elo();
    p.fullStrength = m_strength->fullStrength();
    p.tablebases = m_strength->tablebases() || !p.fullStrength; // not applicable below maximum strength
    p.beginnerLevel = m_strength->beginnerLevel();
    const int time = m_time->currentData().toInt();
    p.moveTimeMs = time > 0 ? time : 1000;
    p.depth = time < 0 ? -time : 0;
    p.coach = m_coach->isChecked();
    p.coachThreshold = m_coachLevel->currentData().toDouble();
    p.ponder = m_ponder->isChecked();
    p.fromCurrentPosition = m_fromCurrent->isChecked();
    if (m_opening->isChecked()) {
        p.openingMoves = openingLine(m_openingText->text());
        if (!p.openingMoves.isEmpty()) p.fromCurrentPosition = false;
    }
    p.engineId = m_engineBox->currentData().toString();
    p.playerName = m_name->text().trimmed();
    applyTimeControl(p, m_tc->currentData().toString(), m_customMin->value(), m_customInc->value(), m_engineSeparate->isChecked(),
                     m_engineMin->value(), m_engineInc->value());
    return p;
}

QString PlayDialog::openingLine(const QString& text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) return {};
    static const QRegularExpression code(QStringLiteral("^[A-Ea-e][0-9]{2}$"));
    if (code.match(t).hasMatch()) {
        const std::string eco = t.toUpper().toStdString();
        const std::string name = openingsBook().nameOf(eco);
        for (const auto& l : openingsBook().lines())
            if (l.eco == eco && l.name == name) return QString::fromStdString(l.moves);
        return {};
    }
    chess::Game g;
    if (!chess::pgn::parseGame(t.toStdString() + " *", g) || g.mainline().empty()) return {};
    // Text rewritten as clean SAN (move numbers)
    QStringList out;
    chess::Position pos = chess::Position::startPos();
    int ply = 0;
    for (const chess::Node* n : g.mainline()) {
        if (ply % 2 == 0) out << QStringLiteral("%1.").arg(ply / 2 + 1);
        out << QString::fromStdString(pos.toSan(n->move));
        pos.makeMove(n->move);
        ++ply;
    }
    return out.join(QLatin1Char(' '));
}

void PlayDialog::accept()
{
    QSettings s;
    s.beginGroup("play");
    s.setValue("opening", m_openingText->text().trimmed());
    s.setValue("beginner", m_strength->beginnerLevel());
    s.setValue("openingOn", m_opening->isChecked());
    s.setValue("side", m_side->checkedId());
    s.setValue("elo", m_strength->elo());
    s.setValue("full", m_strength->fullStrength());
    if (m_strength->fullStrength()) s.setValue("tables", m_strength->tablebases());
    s.setValue("moveTime", m_time->currentData());
    s.setValue("coach", m_coach->isChecked());
    s.setValue("coachLevel", m_coachLevel->currentIndex());
    s.setValue("ponder", m_ponder->isChecked());
    s.setValue("engine", m_engineBox->currentData());
    s.setValue("playerName", m_name->text().trimmed());
    s.setValue("timeControl", m_tc->currentData());
    s.setValue("customMin", m_customMin->value());
    s.setValue("customInc", m_customInc->value());
    s.setValue("engineSeparate", m_engineSeparate->isChecked());
    s.setValue("engineMin", m_engineMin->value());
    s.setValue("engineInc", m_engineInc->value());
    QDialog::accept();
}

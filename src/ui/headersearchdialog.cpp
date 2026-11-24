#include "headersearchdialog.h"

#include "db/format.h"
#include "gamelist.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QGridLayout>
#include <QTabWidget>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
QSpinBox* bound(int lo, int hi)
{
    auto* s = new QSpinBox;
    s->setRange(0, hi);
    s->setSpecialValueText(HeaderSearchDialog::tr("–"));
    s->setMinimum(0);
    (void)lo;
    return s;
}
QHBoxLayout* pair(QWidget* a, QWidget* b)
{
    auto* l = new QHBoxLayout;
    l->addWidget(a, 1);
    l->addWidget(new QLabel(HeaderSearchDialog::tr("à")));
    l->addWidget(b, 1);
    return l;
}
} // namespace

HeaderSearchDialog::HeaderSearchDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Recherche avancée"));
    auto line = [](const QString& ph) {
        auto* e = new QLineEdit;
        e->setPlaceholderText(ph);
        e->setClearButtonEnabled(true);
        return e;
    };
    m_white = line(tr("début d'un mot du nom : « carls », « magnus »…"));
    m_black = line(QString());
    m_ignoreColors = new QCheckBox(tr("Ignorer les couleurs (l'un contre l'autre, avec les Blancs ou les Noirs)"));
    m_event = line(QString());
    m_site = line(QString());
    m_round = line(QString());
    m_annotator = line(QString());
    m_wMin = bound(0, 4000);
    m_wMax = bound(0, 4000);
    m_bMin = bound(0, 4000);
    m_bMax = bound(0, 4000);
    m_yFrom = bound(0, 3000);
    m_yTo = bound(0, 3000);
    m_ecoFrom = line(QStringLiteral("A00"));
    m_ecoTo = line(QStringLiteral("E99"));
    m_movesMin = bound(0, 1000);
    m_movesMax = bound(0, 1000);
    m_rWhite = new QCheckBox(QStringLiteral("1-0"));
    m_rDraw = new QCheckBox(QStringLiteral("½-½"));
    m_rBlack = new QCheckBox(QStringLiteral("0-1"));
    m_rUnknown = new QCheckBox(QStringLiteral("*"));
    auto* results = new QHBoxLayout;
    for (QCheckBox* c : {m_rWhite, m_rDraw, m_rBlack, m_rUnknown}) {
        c->setChecked(true);
        results->addWidget(c);
    }
    results->addStretch(1);
    m_variations = new QCheckBox(tr("avec variantes"));
    m_comments = new QCheckBox(tr("avec commentaires"));
    auto* content = new QHBoxLayout;
    content->addWidget(m_variations);
    content->addWidget(m_comments);
    content->addStretch(1);

    auto* form = new QFormLayout;
    form->addRow(tr("Blancs"), m_white);
    form->addRow(tr("Noirs"), m_black);
    form->addRow(QString(), m_ignoreColors);
    form->addRow(tr("Elo des Blancs"), pair(m_wMin, m_wMax));
    form->addRow(tr("Elo des Noirs"), pair(m_bMin, m_bMax));
    form->addRow(tr("Tournoi"), m_event);
    form->addRow(tr("Lieu"), m_site);
    form->addRow(tr("Ronde"), m_round);
    form->addRow(tr("Annotateur"), m_annotator);
    form->addRow(tr("Années"), pair(m_yFrom, m_yTo));
    form->addRow(tr("ECO"), pair(m_ecoFrom, m_ecoTo));
    form->addRow(tr("Résultat"), results);
    form->addRow(tr("Coups"), pair(m_movesMin, m_movesMax));
    form->addRow(tr("Contenu"), content);
    auto* flagsRow = new QHBoxLayout;
    for (int f = 0; f < hy64db::UserFlagCount; ++f) {
        auto* c = new QCheckBox(GameListModel::userFlagName(f));
        c->setToolTip(tr("Parties avec ce drapeau (bases Hydra64)"));
        m_flags << c;
        flagsRow->addWidget(c);
    }
    flagsRow->addStretch(1);
    form->addRow(tr("Drapeaux"), flagsRow);

    auto* modes = new QGroupBox(tr("Résultat de la recherche"));
    auto* ml = new QVBoxLayout(modes);
    m_combine = new QButtonGroup(this);
    const std::pair<IndexedGameSource::Combine, QString> choices[] = {
        {IndexedGameSource::Combine::Replace, tr("Nouvelle recherche (toute la base)")},
        {IndexedGameSource::Combine::Within, tr("Dans le résultat actuel (ET)")},
        {IndexedGameSource::Combine::Add, tr("Ajouter au résultat actuel (OU)")},
    };
    for (const auto& [mode, text] : choices) {
        auto* r = new QRadioButton(text);
        m_combine->addButton(r, int(mode));
        ml->addWidget(r);
    }
    m_combine->button(int(IndexedGameSource::Combine::Replace))->setChecked(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Rechercher"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("Primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    buttons->button(QDialogButtonBox::Reset)->setText(tr("Effacer les champs"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        // Last search remembered
        const hy64db::HeaderQuery q = query();
        QJsonObject o{{"white", QString::fromStdString(q.white)}, {"black", QString::fromStdString(q.black)}, {"ignore", q.ignoreColors},
                      {"event", QString::fromStdString(q.event)}, {"site", QString::fromStdString(q.site)}, {"round", QString::fromStdString(q.round)},
                      {"annotator", QString::fromStdString(q.annotator)}, {"wmin", q.whiteEloMin}, {"wmax", q.whiteEloMax}, {"bmin", q.blackEloMin},
                      {"bmax", q.blackEloMax}, {"yfrom", q.yearFrom}, {"yto", q.yearTo}, {"ecofrom", m_ecoFrom->text()}, {"ecoto", m_ecoTo->text()},
                      {"results", q.results}, {"mmin", q.movesMin}, {"mmax", q.movesMax}, {"var", q.withVariations}, {"com", q.withComments}};
        QSettings().setValue("headerSearch/last", QJsonDocument(o).toJson(QJsonDocument::Compact));
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, &HeaderSearchDialog::clear);

    // --- Material and patterns
    using MQ = hy64db::MaterialQuery;
    auto* material = new QWidget;
    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("<b>Blancs</b>")), 0, 1, 1, 3, Qt::AlignCenter);
    grid->addWidget(new QLabel(tr("<b>Noirs</b>")), 0, 4, 1, 3, Qt::AlignCenter);
    const std::pair<chess::PieceType, QString> kinds[] = {{chess::Queen, tr("Dames")}, {chess::Rook, tr("Tours")}, {chess::Bishop, tr("Fous")},
                                                           {chess::Knight, tr("Cavaliers")}, {chess::Pawn, tr("Pions")}};
    int row = 1;
    for (const auto& [t, name] : kinds) {
        grid->addWidget(new QLabel(name), row, 0);
        for (int side = 0; side < 2; ++side) {
            const int top = t == chess::Pawn ? 8 : 9;
            auto* lo = new QSpinBox;
            lo->setRange(0, top);
            auto* hi = new QSpinBox;
            hi->setRange(0, top);
            hi->setValue(top);
            lo->setObjectName(QStringLiteral("min%1%2").arg(side).arg(int(t)));
            hi->setObjectName(QStringLiteral("max%1%2").arg(side).arg(int(t)));
            m_min[side][t] = lo;
            m_max[side][t] = hi;
            grid->addWidget(lo, row, 1 + side * 3);
            grid->addWidget(new QLabel(tr("à")), row, 2 + side * 3);
            grid->addWidget(hi, row, 3 + side * 3);
        }
        ++row;
    }
    m_useDiff = new QCheckBox(tr("Différence de matériel (Blancs − Noirs, en pions)"));
    m_diffMin = new QSpinBox;
    m_diffMax = new QSpinBox;
    for (QSpinBox* sb : {m_diffMin, m_diffMax}) sb->setRange(-40, 40);
    m_diffMin->setValue(1);
    m_diffMax->setValue(40);
    auto* diff = new QHBoxLayout;
    diff->addWidget(m_useDiff);
    diff->addWidget(m_diffMin);
    diff->addWidget(new QLabel(tr("à")));
    diff->addWidget(m_diffMax);
    diff->addStretch(1);
    m_bishops = new QComboBox;
    m_bishops->addItem(tr("indifférent"), int(MQ::Bishops::Any));
    m_bishops->addItem(tr("de même couleur (un chacun)"), int(MQ::Bishops::SameColor));
    m_bishops->addItem(tr("de couleurs opposées (un chacun)"), int(MQ::Bishops::OppositeColor));
    auto* patterns = new QGroupBox(tr("Motifs (tous exigés)"));
    auto* pl = new QGridLayout(patterns);
    int k = 0;
    for (uint32_t bit = 1; bit <= MQ::BlackPassedPawn; bit <<= 1, ++k) {
        static const QString labels[] = {tr("Pion d isolé (Blancs)"),     tr("Pion d isolé (Noirs)"),      tr("Pions pendants c/d (Blancs)"),
                                         tr("Pions pendants c/d (Noirs)"), tr("Centre Maroczy (Blancs)"),   tr("Centre Maroczy (Noirs)"),
                                         tr("Fianchetto roi (Blancs)"),    tr("Fianchetto roi (Noirs)"),    tr("Fianchetto dame (Blancs)"),
                                         tr("Fianchetto dame (Noirs)"),    tr("Roques opposés"),            tr("Tour noire prend en c3"),
                                         tr("Pion passé (Blancs)"),        tr("Pion passé (Noirs)")};
        auto* c = new QCheckBox(labels[k]);
        m_patterns << qMakePair(c, bit);
        pl->addWidget(c, k / 2, k % 2);
    }
    m_reference = new QComboBox;
    m_reference->addItem(tr("aucune"), int(MQ::Reference::None));
    m_reference->addItem(tr("mêmes pions sur les mêmes cases"), int(MQ::Reference::SamePawns));
    m_reference->addItem(tr("mêmes colonnes de pions"), int(MQ::Reference::SamePawnFiles));
    m_reference->addItem(tr("même matériel"), int(MQ::Reference::SameMaterial));
    m_reference->setToolTip(tr("Comparaison avec la position de l'échiquier (la recherche exacte reste « Rechercher la position »)"));
    m_plyFrom = bound(0, 1000);
    m_plyTo = bound(0, 1000);
    m_minPlies = new QSpinBox;
    m_minPlies->setRange(1, 200);
    m_minPlies->setToolTip(tr("Les critères doivent rester vrais pendant autant de demi-coups de suite"));
    auto* mform = new QFormLayout;
    mform->addRow(tr("Fous"), m_bishops);
    mform->addRow(tr("Comparer à l'échiquier"), m_reference);
    mform->addRow(tr("Entre les demi-coups"), pair(m_plyFrom, m_plyTo));
    mform->addRow(tr("Pendant au moins (demi-coups)"), m_minPlies);
    auto* note = new QLabel(tr("Ces critères rejouent les parties : la recherche est plus longue que par en-têtes."));
    note->setObjectName("Faint");
    note->setWordWrap(true);
    auto* mlay = new QVBoxLayout(material);
    mlay->addLayout(grid);
    mlay->addLayout(diff);
    mlay->addLayout(mform);
    mlay->addWidget(patterns);
    mlay->addWidget(note);
    mlay->addStretch(1);
    auto* headers = new QWidget;
    headers->setLayout(form);
    m_tabs = new QTabWidget;
    m_tabs->addTab(headers, tr("En-têtes"));
    m_tabs->addTab(material, tr("Matériel et motifs"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs);
    layout->addWidget(modes);
    layout->addWidget(buttons);

    const QJsonObject o = QJsonDocument::fromJson(QSettings().value("headerSearch/last").toByteArray()).object();
    if (!o.isEmpty()) {
        m_white->setText(o.value("white").toString());
        m_black->setText(o.value("black").toString());
        m_ignoreColors->setChecked(o.value("ignore").toBool());
        m_event->setText(o.value("event").toString());
        m_site->setText(o.value("site").toString());
        m_round->setText(o.value("round").toString());
        m_annotator->setText(o.value("annotator").toString());
        m_wMin->setValue(o.value("wmin").toInt());
        m_wMax->setValue(o.value("wmax").toInt());
        m_bMin->setValue(o.value("bmin").toInt());
        m_bMax->setValue(o.value("bmax").toInt());
        m_yFrom->setValue(o.value("yfrom").toInt());
        m_yTo->setValue(o.value("yto").toInt());
        m_ecoFrom->setText(o.value("ecofrom").toString());
        m_ecoTo->setText(o.value("ecoto").toString());
        const int r = o.value("results").toInt(15);
        m_rWhite->setChecked(r & 1);
        m_rDraw->setChecked(r & 2);
        m_rBlack->setChecked(r & 4);
        m_rUnknown->setChecked(r & 8);
        m_movesMin->setValue(o.value("mmin").toInt());
        m_movesMax->setValue(o.value("mmax").toInt());
        m_variations->setChecked(o.value("var").toBool());
        m_comments->setChecked(o.value("com").toBool());
    }
}

void HeaderSearchDialog::clear()
{
    for (QLineEdit* e : {m_white, m_black, m_event, m_site, m_round, m_annotator, m_ecoFrom, m_ecoTo}) e->clear();
    for (QSpinBox* s : {m_wMin, m_wMax, m_bMin, m_bMax, m_yFrom, m_yTo, m_movesMin, m_movesMax}) s->setValue(0);
    for (QCheckBox* c : {m_rWhite, m_rDraw, m_rBlack, m_rUnknown}) c->setChecked(true);
    for (QCheckBox* c : {m_ignoreColors, m_variations, m_comments}) c->setChecked(false);
    for (QCheckBox* c : m_flags) c->setChecked(false);
    for (int side = 0; side < 2; ++side)
        for (int t = chess::Pawn; t <= chess::Queen; ++t) {
            m_min[side][t]->setValue(0);
            m_max[side][t]->setValue(m_max[side][t]->maximum());
        }
    m_useDiff->setChecked(false);
    m_bishops->setCurrentIndex(0);
    m_reference->setCurrentIndex(0);
    m_plyFrom->setValue(0);
    m_plyTo->setValue(0);
    m_minPlies->setValue(1);
    for (const auto& p : m_patterns) p.first->setChecked(false);
}

void HeaderSearchDialog::showMaterialTab() { m_tabs->setCurrentIndex(1); }

hy64db::HeaderQuery HeaderSearchDialog::query() const
{
    hy64db::HeaderQuery q;
    q.white = m_white->text().trimmed().toStdString();
    q.black = m_black->text().trimmed().toStdString();
    q.ignoreColors = m_ignoreColors->isChecked();
    q.event = m_event->text().trimmed().toStdString();
    q.site = m_site->text().trimmed().toStdString();
    q.round = m_round->text().trimmed().toStdString();
    q.annotator = m_annotator->text().trimmed().toStdString();
    q.whiteEloMin = m_wMin->value();
    q.whiteEloMax = m_wMax->value();
    q.blackEloMin = m_bMin->value();
    q.blackEloMax = m_bMax->value();
    q.yearFrom = m_yFrom->value();
    q.yearTo = m_yTo->value();
    q.ecoFrom = hy64db::parseEco(m_ecoFrom->text().trimmed().toUpper().toStdString());
    q.ecoTo = hy64db::parseEco(m_ecoTo->text().trimmed().toUpper().toStdString());
    q.results = uint8_t((m_rWhite->isChecked() ? 1 : 0) | (m_rDraw->isChecked() ? 2 : 0) | (m_rBlack->isChecked() ? 4 : 0) | (m_rUnknown->isChecked() ? 8 : 0));
    q.movesMin = m_movesMin->value();
    q.movesMax = m_movesMax->value();
    q.withVariations = m_variations->isChecked();
    q.withComments = m_comments->isChecked();
    for (int f = 0; f < m_flags.size(); ++f)
        if (m_flags[f]->isChecked()) q.userFlags |= uint8_t(1 << f);
    hy64db::MaterialQuery& m = q.material;
    for (int side = 0; side < 2; ++side)
        for (int t = chess::Pawn; t <= chess::Queen; ++t) {
            m.minCount[side][t] = int8_t(m_min[side][t]->value());
            m.maxCount[side][t] = int8_t(m_max[side][t]->value());
        }
    m.useDiff = m_useDiff->isChecked();
    m.diffMin = std::min(m_diffMin->value(), m_diffMax->value());
    m.diffMax = std::max(m_diffMin->value(), m_diffMax->value());
    m.bishops = hy64db::MaterialQuery::Bishops(m_bishops->currentData().toInt());
    for (const auto& p : m_patterns)
        if (p.first->isChecked()) m.patterns |= p.second;
    m.reference = hy64db::MaterialQuery::Reference(m_reference->currentData().toInt());
    if (m.reference != hy64db::MaterialQuery::Reference::None) m.refPos = m_ref;
    m.plyFrom = m_plyFrom->value();
    m.plyTo = m_plyTo->value();
    m.minPlies = m_minPlies->value();
    return q;
}

IndexedGameSource::Combine HeaderSearchDialog::combine() const { return IndexedGameSource::Combine(m_combine->checkedId()); }

void HeaderSearchDialog::setQuery(const hy64db::HeaderQuery& q)
{
    clear();
    m_white->setText(QString::fromStdString(q.white));
    m_black->setText(QString::fromStdString(q.black));
    m_ignoreColors->setChecked(q.ignoreColors);
    m_event->setText(QString::fromStdString(q.event));
}

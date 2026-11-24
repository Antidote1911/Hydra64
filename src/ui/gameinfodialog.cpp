#include "gameinfodialog.h"
#include "display.h"
#include "theme.h"

#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

namespace {

QLabel* section(const QString& text)
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

std::string header(const chess::Game::Headers& h, const char* key)
{
    for (const auto& [k, v] : h)
        if (k == key) return v;
    return {};
}

} // namespace

bool GameInfoDialog::isUnknown(const std::string& v)
{
    return v.empty() || v == "?" || v == "????.??.??" || v == "-";
}

GameInfoDialog::GameInfoDialog(const chess::Game::Headers& headers, QWidget* parent)
    : QDialog(parent)
    , m_original(headers)
{
    setWindowTitle(tr("Informations de la partie"));
    setMinimumWidth(560);

    auto field = [&](const char* key, const QString& placeholder) {
        auto* e = new QLineEdit;
        const std::string v = header(headers, key);
        if (!isUnknown(v)) e->setText(QString::fromStdString(v));
        e->setPlaceholderText(placeholder);
        m_fields.insert(QString::fromLatin1(key), e);
        return e;
    };
    auto eloField = [&](const char* key) {
        QLineEdit* e = field(key, tr("Elo"));
        e->setValidator(new QIntValidator(0, 4000, e));
        e->setFixedWidth(80);
        return e;
    };

    auto* title = new QLabel(tr("Informations de la partie"));
    QFont tf = title->font();
    tf.setPixelSize(16);
    tf.setWeight(QFont::DemiBold);
    title->setFont(tf);

    // Players
    auto* players = new QGridLayout;
    players->setHorizontalSpacing(8);
    players->setVerticalSpacing(6);
    auto dot = [](bool white) {
        auto* l = new QLabel(white ? QStringLiteral("○") : QStringLiteral("●"));
        l->setObjectName("Dim");
        return l;
    };
    players->addWidget(dot(true), 0, 0);
    players->addWidget(field("White", tr("Nom du joueur avec les Blancs")), 0, 1);
    players->addWidget(eloField("WhiteElo"), 0, 2);
    players->addWidget(dot(false), 1, 0);
    players->addWidget(field("Black", tr("Nom du joueur avec les Noirs")), 1, 1);
    players->addWidget(eloField("BlackElo"), 1, 2);

    auto* swap = new QPushButton(theme::icon("flip"), tr("Échanger"));
    swap->setToolTip(tr("Échange les joueurs et leurs Elo"));
    players->addWidget(swap, 0, 3, 2, 1);

    // Result and date
    m_result = new QComboBox;
    const std::pair<QString, QString> results[] = {{"*", tr("* (en cours ou inconnu)")}, {"1-0", tr("1-0 (victoire des Blancs)")},
                                                   {"0-1", tr("0-1 (victoire des Noirs)")}, {"1/2-1/2", tr("½-½ (nulle)")}};
    for (const auto& [code, label] : results) m_result->addItem(label, code);
    m_result->setCurrentIndex(std::max(0, m_result->findData(QString::fromStdString(header(headers, "Result")))));

    QLineEdit* date = field("Date", display::datePlaceholder());
    date->setText(display::dateForEditing(header(headers, "Date")));
    date->setToolTip(prefs::language() == QLatin1String("en") ? tr("Date complète (1851.06.21), année et mois (1851.06) ou année seule (1851)")
                                                               : tr("Date complète (21/06/1851), mois et année (06/1851) ou année seule (1851)"));
    date->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral(R"([0-9?]{0,4}([/.][0-9?]{0,4}){0,2})")), date));
    auto* today = new QPushButton(tr("Aujourd'hui"));
    auto* dateRow = new QHBoxLayout;
    dateRow->addWidget(date, 1);
    dateRow->addWidget(today);

    auto* resultDate = new QGridLayout;
    resultDate->setHorizontalSpacing(12);
    resultDate->addWidget(section(tr("Résultat")), 0, 0);
    resultDate->addWidget(section(tr("Date")), 0, 1);
    resultDate->addWidget(m_result, 1, 0);
    resultDate->addLayout(dateRow, 1, 1);

    // Event
    auto* event = new QGridLayout;
    event->setHorizontalSpacing(12);
    event->setVerticalSpacing(6);
    event->addWidget(section(tr("Tournoi")), 0, 0);
    event->addWidget(section(tr("Ronde")), 0, 1);
    event->addWidget(field("Event", tr("Tournoi, match, partie amicale…")), 1, 0);
    QLineEdit* round = field("Round", tr("ex. 3 ou 2.1"));
    round->setFixedWidth(110);
    event->addWidget(round, 1, 1);
    event->addWidget(section(tr("Lieu")), 2, 0);
    event->addWidget(section(tr("ECO")), 2, 1);
    event->addWidget(field("Site", tr("Ville, club ou site web")), 3, 0);
    QLineEdit* eco = field("ECO", tr("ex. C41"));
    eco->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[A-Ea-e]([0-9]{0,2})")), eco));
    eco->setFixedWidth(110);
    event->addWidget(eco, 3, 1);
    event->addWidget(section(tr("Annotateur")), 4, 0);
    event->addWidget(field("Annotator", tr("Auteur des commentaires")), 5, 0, 1, 2);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Valider"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("Primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 16);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addSpacing(8);
    layout->addWidget(section(tr("Joueurs")));
    layout->addLayout(players);
    layout->addSpacing(10);
    layout->addLayout(resultDate);
    layout->addSpacing(10);
    layout->addLayout(event);
    layout->addSpacing(14);
    layout->addWidget(buttons);

    connect(today, &QPushButton::clicked, this, [date] {
        date->setText(QDate::currentDate().toString(prefs::language() == QLatin1String("en") ? QStringLiteral("yyyy.MM.dd") : QStringLiteral("dd/MM/yyyy")));
    });
    connect(swap, &QPushButton::clicked, this, [this] {
        for (const auto& [a, b] : {std::pair{"White", "Black"}, std::pair{"WhiteElo", "BlackElo"}}) {
            const QString t = m_fields[a]->text();
            m_fields[a]->setText(m_fields[b]->text());
            m_fields[b]->setText(t);
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // The first empty field gets the focus
    for (const char* k : {"White", "Black", "Event"})
        if (m_fields[k]->text().isEmpty()) {
            m_fields[k]->setFocus();
            break;
        }
}

chess::Game::Headers GameInfoDialog::headers() const
{
    chess::Game::Headers out = m_original;
    auto set = [&](const std::string& key, const std::string& value) {
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& p) { return p.first == key; });
        if (value.empty()) {
            if (it != out.end()) out.erase(it); // field emptied: header removed ("?" when written)
        } else if (it != out.end()) {
            it->second = value;
        } else {
            out.emplace_back(key, value);
        }
    };
    for (auto it = m_fields.cbegin(); it != m_fields.cend(); ++it) {
        QString v = it.value()->text().simplified();
        if (it.key() == QLatin1String("ECO")) v = v.toUpper();
        if (it.key() == QLatin1String("Date")) set("Date", display::pgnDate(v)); // PGN storage: YYYY.MM.DD
        else set(it.key().toStdString(), v.toStdString());
    }
    set("Result", m_result->currentData().toString().toStdString());
    return out;
}

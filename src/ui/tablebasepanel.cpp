#include "tablebasepanel.h"

#include "tablebases.h"
#include "preferences.h"
#include "display.h"

#include "theme.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QToolButton>
#include <QUrlQuery>
#include <QVBoxLayout>

using namespace chess;

namespace {
QString wdlText(int wdl)
{
    switch (wdl) {
    case 2: return TablebasePanel::tr("Gain");
    case 1: return TablebasePanel::tr("Gain (annulé par les 50 coups)");
    case -1: return TablebasePanel::tr("Perte (sauvée par les 50 coups)");
    case -2: return TablebasePanel::tr("Perte");
    default: return TablebasePanel::tr("Nulle");
    }
}
QColor wdlColor(int wdl) { return wdl > 0 ? QColor(0x6a, 0xbf, 0x69) : wdl < 0 ? QColor(0xe0, 0x5c, 0x4f) : theme::TextDim; }
int categoryWdl(const QString& c)
{
    if (c == QLatin1String("win")) return 2;
    if (c == QLatin1String("cursed-win") || c == QLatin1String("maybe-win")) return 1;
    if (c == QLatin1String("blessed-loss") || c == QLatin1String("maybe-loss")) return -1;
    if (c == QLatin1String("loss")) return -2;
    return 0;
}
} // namespace

QString TablebasePanel::defaultPath() { return tablebases::effectivePath(); }

TablebasePanel::TablebasePanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("PanelBody");
    m_header = new QLabel;
    m_header->setWordWrap(true);
    m_settings = new QToolButton;
    m_settings->setIcon(theme::icon("edit"));
    m_settings->setAutoRaise(true);
    m_settings->setToolTip(tr("Tables de finales : dossiers, images ISO, moteurs, téléchargement (Préférences)"));
    auto* top = new QHBoxLayout;
    top->setContentsMargins(8, 6, 6, 2);
    top->addWidget(m_header, 1);
    top->addWidget(m_settings, 0, Qt::AlignTop);
    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({tr("Coup"), tr("Résultat"), tr("Distance")});
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setToolTip(tr("Double-clic : jouer le coup. DTZ : demi-coups jusqu'à la prochaine prise ou poussée de pion ; DTM : jusqu'au mat"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(top);
    layout->addWidget(m_table, 1);
    connect(m_settings, &QToolButton::clicked, this, &TablebasePanel::editSettings);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        if (row >= 0 && row < m_shown.moves.size()) emit movePicked(m_shown.moves[row].move);
    });
    tablebases::apply(); // folders and disc images chosen in the Preferences
    connect(&prefs::Notifier::instance(), &prefs::Notifier::changed, this, [this] { refresh(); }); // change of notation
    m_pos = Position::startPos();
    refresh();
}

bool TablebasePanel::setTablesPath(const QString& path)
{
    tablebases::setSources(path.isEmpty() ? QStringList() : QStringList{path});
    tablebases::apply();
    refresh();
    return tb::largest() > 0;
}

void TablebasePanel::setPosition(const Position& pos)
{
    m_pos = pos;
    refresh();
}

void TablebasePanel::refresh()
{
    m_shown = {};
    m_table->setRowCount(0);
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    const int pieces = tb::pieceCount(m_pos);
    const bool online = QSettings().value("tablebase/online", false).toBool();
    if (tb::largest() > 0 && pieces <= tb::largest() && m_pos.castlingRights() == 0) {
        const tb::Probe p = tb::probe(m_pos);
        if (p.ok) {
            showLocal(p);
            return;
        }
    }
    if (online && pieces <= 7 && m_pos.castlingRights() == 0) {
        const QString fen = QString::fromStdString(m_pos.fen());
        if (m_cache.contains(fen)) {
            showOnline(m_cache.value(fen));
            return;
        }
        if (!m_net) m_net = new QNetworkAccessManager(this);
        QUrl url(QStringLiteral("https://tablebase.lichess.ovh/standard"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("fen"), fen);
        url.setQuery(q);
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Hydra64"));
        m_reply = m_net->get(req);
        m_header->setText(tr("Interrogation des tables en ligne…"));
        connect(m_reply, &QNetworkReply::finished, this, [this, fen] {
            QNetworkReply* r = m_reply;
            m_reply = nullptr;
            r->deleteLater();
            if (r->error() != QNetworkReply::NoError) {
                m_header->setText(tr("Tables en ligne indisponibles : %1").arg(r->errorString()));
                return;
            }
            const QByteArray json = r->readAll();
            m_cache.insert(fen, json);
            if (QString::fromStdString(m_pos.fen()) == fen) showOnline(json);
        });
        return;
    }
    if (pieces > 7) m_header->setText(tr("Plus de 7 pièces : hors des tables de finales."));
    else if (tb::largest() == 0 && !online)
        m_header->setText(tr("Aucune table : choisissez un dossier ou une image ISO, téléchargez-les, ou activez le service en ligne (bouton crayon)."));
    else m_header->setText(tr("Position hors des tables disponibles (%n pièce(s) au plus en local).", nullptr, tb::largest()));
}

void TablebasePanel::showLocal(const tb::Probe& p)
{
    m_shown.source = QStringLiteral("local");
    m_shown.wdl = p.wdl;
    m_shown.dtz = p.dtz;
    for (const tb::MoveResult& m : p.moves) {
        m_shown.moves << m;
        m_shown.dtm << 0;
    }
    fill();
}

void TablebasePanel::showOnline(const QByteArray& json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    m_shown.source = QStringLiteral("en ligne");
    m_shown.wdl = categoryWdl(o.value("category").toString());
    m_shown.dtz = std::abs(o.value("dtz").toInt());
    for (const QJsonValue& v : o.value("moves").toArray()) {
        const QJsonObject mo = v.toObject();
        tb::MoveResult m;
        m.move = m_pos.parseUci(mo.value("uci").toString().toStdString());
        if (m.move.isNull()) continue;
        m.wdl = -categoryWdl(mo.value("category").toString()); // category seen from the opponent
        m.dtz = std::abs(mo.value("dtz").toInt());
        m_shown.moves << m;
        m_shown.dtm << std::abs(mo.value("dtm").toInt());
    }
    fill();
}

void TablebasePanel::fill()
{
    const QString side = m_pos.sideToMove() == White ? tr("les Blancs") : tr("les Noirs");
    QString head = m_pos.isCheckmate() ? tr("Échec et mat.")
                   : m_pos.legalMoves().empty() ? tr("Pat.")
                                                 : tr("<b>%1</b> pour %2").arg(wdlText(m_shown.wdl), side);
    if (m_shown.dtz && m_shown.wdl != 0) head += tr(" · DTZ %1").arg(m_shown.dtz);
    head += QStringLiteral(" <span style='color:gray'>(%1)</span>").arg(m_shown.source == QLatin1String("local") ? tr("tables locales") : tr("Lichess, en ligne"));
    m_header->setText(head);
    m_table->setRowCount(int(m_shown.moves.size()));
    for (int i = 0; i < m_shown.moves.size(); ++i) {
        const tb::MoveResult& m = m_shown.moves[i];
        auto* move = new QTableWidgetItem(display::san(m_pos.toSan(m.move))); // notation chosen in the Preferences
        auto* res = new QTableWidgetItem(wdlText(m.wdl));
        res->setForeground(wdlColor(m.wdl));
        QString dist;
        if (m.wdl != 0 && m.dtz) dist = tr("DTZ %1").arg(m.dtz);
        if (i < m_shown.dtm.size() && m_shown.dtm[i]) dist += (dist.isEmpty() ? QString() : QStringLiteral(" · ")) + tr("DTM %1").arg(m_shown.dtm[i]);
        m_table->setItem(i, 0, move);
        m_table->setItem(i, 1, res);
        m_table->setItem(i, 2, new QTableWidgetItem(dist));
    }
}

void TablebasePanel::editSettings() { emit settingsRequested(); } // Preferences → Tablebases

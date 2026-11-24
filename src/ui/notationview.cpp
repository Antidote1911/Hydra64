#include "notationview.h"

#include <algorithm>
#include <QLineEdit>
#include "chess/pgn.h"
#include "display.h"
#include "preferences.h"
#include "gamesession.h"
#include "theme.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>

using namespace chess;

namespace {

QString esc(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }

// Comment to display: without the engine note of tournament games ("+0.23/31 2.6s",
// "#8/63 1.3s", "livre") when the user hides them. The rest of the comment is kept.
std::string shownComment(const std::string& raw)
{
    // ChessBase's "[%emt …]", "[%eval …]"… commands are never shown
    const std::string comment = raw.find("[%") == std::string::npos ? raw : pgn::withoutCommands(raw);
    if (comment.empty() || prefs::showEngineComments()) return comment;
    static const QRegularExpression note(QStringLiteral(R"(^(?:[+-]\d+\.\d\d|#-?\d+)/\d+ \d+(?:[.,]\d+)?s\s*)"));
    QString c = QString::fromStdString(comment);
    if (c == NotationView::tr("livre") || c == QLatin1String("livre") || c == QLatin1String("book")) return {};
    c.remove(note);
    return c.toStdString();
}

QString nagColor(int nag)
{
    switch (nag) {
    case 1:
    case 3: return theme::Good.name();
    case 2: return theme::Accent.name();
    case 4: return theme::Bad.name();
    case 5: return QStringLiteral("#7fb2e0");
    case 6: return QStringLiteral("#e0c050");
    default: return {};
    }
}

} // namespace

NotationView::NotationView(GameSession* session, QWidget* parent)
    : QTextBrowser(parent)
    , m_session(session)
{
    setOpenLinks(false);
    setFrameShape(QFrame::NoFrame);
    document()->setDocumentMargin(12);
    document()->setDefaultStyleSheet(QStringLiteral("a { text-decoration: none; }"));

    connect(this, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        const QString s = url.toString();
        if (s == QLatin1String("info:")) emit infoRequested();
        else if (s.startsWith(QLatin1String("n:")))
            if (Node* n = m_session->game().findById(s.mid(2).toInt())) m_session->goTo(n);
    });
    connect(session, &GameSession::treeChanged, this, &NotationView::rebuild);
    connect(session, &GameSession::graphicsChanged, this, &NotationView::rebuild); // "↗" marker
    // Change of move: only the highlight moves (the text is only rebuilt if the tree changes).
    connect(session, &GameSession::currentChanged, this, &NotationView::highlightCurrent);
    connect(session, &GameSession::headersChanged, this, &NotationView::rebuild);
    connect(&prefs::Notifier::instance(), &prefs::Notifier::changed, this, &NotationView::rebuild);
    rebuild();
}

QString NotationView::moveHtml(const Node* n, bool forceNumber, bool mainline) const
{
    QString html;
    const Position& before = n->parent->pos;
    if (const std::string pre = shownComment(n->preComment); !pre.empty()) { // pre-move comment, then the number
        html += QStringLiteral("<span style='color:%1;font-weight:400;'>%2</span> ").arg(theme::Comment.name(), display::comment(QString::fromStdString(pre)).toHtmlEscaped());
        forceNumber = true;
    }
    if (before.sideToMove() == White || forceNumber) {
        html += QStringLiteral("<span style='color:%1;font-weight:400;'>%2%3</span>")
                    .arg(theme::TextFaint.name())
                    .arg(before.fullmoveNumber())
                    .arg(before.sideToMove() == White ? QStringLiteral(".") : QStringLiteral("…"));
        html += QChar(0x2009); // thin space
    }

    QString san = display::san(n->san).toHtmlEscaped();
    // Figurines enlarged (and in a font that draws them well) to stay readable.
    if (prefs::notation() == prefs::Notation::Figurine) {
        QString out;
        for (const QChar c : std::as_const(san)) {
            if (c.unicode() >= 0x265A && c.unicode() <= 0x265E)
                out += QStringLiteral("<span style=\"font-family:'DejaVu Sans';font-size:%1px;\">%2</span>")
                           .arg(qRound(prefs::notationFontSize() * 1.3))
                           .arg(c);
            else out += c;
        }
        san = out;
    }
    QString suffix;
    QString color;
    for (int nag : n->nags) {
        if (nag <= 6) {
            san += QString::fromStdString(pgn::nagSymbol(nag));
            if (color.isEmpty()) color = nagColor(nag);
        } else {
            suffix += ' ' + QString::fromStdString(pgn::nagSymbol(nag));
        }
    }

    // The current move is not marked here: highlightCurrent() highlights it without rebuilding the text.
    QString style = QStringLiteral("color:%1;").arg(!color.isEmpty() ? color : (mainline ? theme::Text.name() : theme::TextDim.name()));
    style += mainline ? QStringLiteral("font-weight:600;") : QStringLiteral("font-weight:400;");

    html += QStringLiteral("<a href='n:%1'><span style='%2'>%3</span></a>").arg(n->id).arg(style, san);
    if (!suffix.isEmpty()) html += QStringLiteral("<span style='color:%1;'>%2</span>").arg(theme::TextDim.name(), suffix.toHtmlEscaped());
    if (n->question) // training question: this move is to be found
        html += QStringLiteral("<span style='color:%1;' title='%2'>?⃝</span>").arg(theme::Accent.name(), QString::fromStdString(n->questionText).toHtmlEscaped());
    if (n->medals) { // ChessBase medals
        QStringList names;
        for (const std::string& m : pgn::medalNames(n->medals)) names << QString::fromStdString(m);
        html += QStringLiteral(" <span style='color:%1;'>★ %2</span>").arg(theme::Accent.name(), names.join(QStringLiteral(", ")).toHtmlEscaped());
    }
    if (!n->graphics.empty()) // arrows or marked squares on this position
        html += QStringLiteral("<span style='color:%1;'>↗</span>").arg(theme::Accent.name());
    // Time spent on the move (ChessBase's "[%emt]"): shown with the engine notes, hidden with them
    if (prefs::showEngineComments() && n->comment.find("[%emt") != std::string::npos)
        if (const int s = pgn::elapsedSeconds(n->comment); s >= 0)
            html += QStringLiteral(" <span style='color:%1;font-weight:400;' title='%2'>%3</span>")
                        .arg(theme::TextFaint.name(), NotationView::tr("Temps passé sur ce coup"),
                             s < 60 ? NotationView::tr("%1 s").arg(s) : s < 3600 ? QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'))
                                                                                  : QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0')));
    if (const std::string comment = shownComment(n->comment); !comment.empty())
        html += QStringLiteral(" <span style='color:%1;font-weight:400;'>%2</span>").arg(theme::Comment.name(), display::comment(QString::fromStdString(comment)).toHtmlEscaped());
    return html;
}

void NotationView::appendInline(QString& html, const Node* first) const
{
    html += moveHtml(first, true, false);
    bool needNumber = !shownComment(first->comment).empty();
    for (const Node* n = first; const Node* c = n->mainChild(); n = c) {
        html += ' ' + moveHtml(c, needNumber, false);
        needNumber = !shownComment(c->comment).empty();
        for (size_t i = 1; i < n->children.size(); ++i) {
            html += QStringLiteral(" <span style='color:%1;'>(</span>").arg(theme::TextFaint.name());
            appendInline(html, n->children[i].get());
            html += QStringLiteral("<span style='color:%1;'>)</span>").arg(theme::TextFaint.name());
            needNumber = true;
        }
    }
}

void NotationView::rebuild()
{
    const Game& g = m_session->game();
    const int scroll = verticalScrollBar()->value();
    QFont f = font();
    f.setPixelSize(prefs::notationFontSize());
    document()->setDefaultFont(f);

    QString html;
    const QString pMain = QStringLiteral("<p style='margin:0 0 6px 0;line-height:150%;'>");
    const QString pVar = QStringLiteral("<p style='margin:0 0 6px 16px;line-height:140%;'>");

    // Header: players, event
    const QString white = QString::fromStdString(g.header("White"));
    const QString black = QString::fromStdString(g.header("Black"));
    {
        html += QStringLiteral("<p style='margin:0 0 2px 0;'><a href='info:'><span style='font-size:%4px;font-weight:600;color:%1;'>%2 – %3</span></a></p>")
                    .arg(theme::Text.name(), (white.isEmpty() ? QStringLiteral("?") : white).toHtmlEscaped(), (black.isEmpty() ? QStringLiteral("?") : black).toHtmlEscaped())
                    .arg(prefs::notationFontSize() + 1);
        QStringList meta;
        for (const char* k : {"Event", "Site", "Date", "ECO"}) {
            QString v = std::string(k) == "Date" ? display::date(g.header(k)) : QString::fromStdString(g.header(k));
            if (!v.isEmpty() && v != QLatin1String("?") && !v.startsWith(QLatin1String("????"))) meta << v.toHtmlEscaped();
        }
        html += QStringLiteral("<p style='margin:0 0 12px 0;color:%1;'>%2</p>").arg(theme::TextFaint.name(), meta.join(QStringLiteral(" · ")));
    }

    if (const std::string intro = pgn::withoutCommands(g.root()->comment); !intro.empty())
        html += QStringLiteral("<p style='margin:0 0 8px 0;color:%1;'>%2</p>").arg(theme::Comment.name(), display::comment(QString::fromStdString(intro)).toHtmlEscaped());

    html += pMain;
    bool needNumber = true;
    bool openParagraph = true;
    for (const Node* n = g.root(); const Node* main = n->mainChild(); n = main) {
        if (!openParagraph) {
            html += pMain;
            openParagraph = true;
        }
        html += moveHtml(main, needNumber, true) + ' ';
        needNumber = !shownComment(main->comment).empty();
        if (n->children.size() > 1) {
            html += QStringLiteral("</p>");
            for (size_t i = 1; i < n->children.size(); ++i) {
                html += pVar;
                appendInline(html, n->children[i].get());
                html += QStringLiteral("</p>");
            }
            openParagraph = false;
            needNumber = true;
        }
    }
    const std::string result = g.header("Result");
    if (!result.empty() && result != "*") {
        if (!openParagraph) html += pMain;
        html += QStringLiteral("<span style='color:%1;font-weight:700;'>%2</span>").arg(theme::Text.name(), esc(result));
        openParagraph = true;
    }
    if (openParagraph) html += QStringLiteral("</p>");

    if (!g.root()->mainChild())
        html += QStringLiteral("<p style='color:%1;'>%2</p>").arg(theme::TextFaint.name(), tr("Jouez un coup sur l'échiquier pour commencer."));

    setHtml(html);
    verticalScrollBar()->setValue(scroll);

    // Position of each move in the document (a single pass), for the highlight.
    m_spans.clear();
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid()) continue;
            const QString href = f.charFormat().anchorHref();
            if (!href.startsWith(QLatin1String("n:"))) continue;
            const int id = href.mid(2).toInt();
            auto found = m_spans.find(id);
            if (found == m_spans.end()) m_spans.insert(id, {f.position(), f.length()});
            else found->second = f.position() + f.length() - found->first; // figurine: several fragments
        }
    highlightCurrent();
}

void NotationView::highlightCurrent()
{
    QList<QTextEdit::ExtraSelection> selections;
    const auto it = m_spans.constFind(m_session->current()->id);
    if (it != m_spans.constEnd()) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(document());
        sel.cursor.setPosition(it->first);
        sel.cursor.setPosition(it->first + it->second, QTextCursor::KeepAnchor);
        sel.format.setForeground(theme::Accent); // no highlight: the current move in the accent colour
        selections << sel;
        // Keep the current move visible
        QTextCursor c(document());
        c.setPosition(it->first);
        setTextCursor(c);
        ensureCursorVisible();
    }
    setExtraSelections(selections);
}

Node* NotationView::nodeAt(const QPoint& pos) const
{
    const QString href = anchorAt(pos);
    if (!href.startsWith(QLatin1String("n:"))) return nullptr;
    return m_session->game().findById(href.mid(2).toInt());
}

void NotationView::contextMenuEvent(QContextMenuEvent* e)
{
    Node* n = nodeAt(e->pos());
    if (!n) n = m_session->current();
    const bool onMove = n && n->parent; // editing of a move (not on an empty game)

    QMenu menu(this);
    QAction* comment = nullptr;
    QAction* preComment = nullptr;
    QAction* clearNags = nullptr;
    QAction* promote = nullptr;
    QAction* demote = nullptr;
    QAction* replace = nullptr;
    QAction* insert = nullptr;
    QAction* nullMove = nullptr;
    QAction* del = nullptr;
    if (onMove) {
        comment = menu.addAction(theme::icon("comment"), tr("Commentaire…"));
        // Before the move: only where PGN keeps the difference (head of a variation, after a
        // comment or a variation); elsewhere it is the previous move's comment.
        if (pgn::keepsPreComment(n) || !n->preComment.empty()) preComment = menu.addAction(theme::icon("comment"), tr("Commentaire avant le coup…"));
        if (!n->graphics.empty())
            connect(menu.addAction(tr("Effacer les flèches et les cases marquées")), &QAction::triggered, this, [this, n] { m_session->setGraphics(n, {}); });
        {
            const bool marked = std::find(n->nags.begin(), n->nags.end(), pgn::DiagramNag) != n->nags.end();
            QAction* diagram = menu.addAction(tr("Diagramme après ce coup"));
            diagram->setCheckable(true);
            diagram->setChecked(marked);
            diagram->setToolTip(tr("Repère « ▦ » : un diagramme est placé ici dans le PDF et à l'impression"));
            connect(diagram, &QAction::triggered, this, [this, n, marked] {
                m_session->checkpoint();
                if (marked) std::erase(n->nags, pgn::DiagramNag);
                else n->nags.push_back(pgn::DiagramNag);
                emit m_session->treeChanged();
            });
        }
        {
            QAction* question = menu.addAction(n->question ? tr("Question d'entraînement : modifier ou retirer…") : tr("Question d'entraînement…"));
            connect(question, &QAction::triggered, this, [this, n] {
                bool ok = false;
                const QString text = QInputDialog::getText(this, tr("Question d'entraînement"),
                                                           n->question ? tr("Énoncé de la question (ce coup est à trouver). Videz le champ et validez pour garder la question sans énoncé ; Annuler la retire.")
                                                                       : tr("Énoncé de la question (facultatif) : ce coup sera à trouver pendant l'entraînement."),
                                                           QLineEdit::Normal, QString::fromStdString(n->questionText), &ok);
                if (!ok && !n->question) return;
                m_session->checkpoint();
                n->question = ok;
                n->questionText = ok ? text.trimmed().toStdString() : std::string();
                n->questionData.clear(); // the wording changes: the hints and feedback coming from ChessBase no longer apply
                emit m_session->treeChanged();
            });
        }
        QMenu* nagMenu = menu.addMenu(tr("Annotation"));
        for (int nag : {1, 2, 3, 4, 5, 6}) {
            QAction* a = nagMenu->addAction(QString::fromStdString(pgn::nagSymbol(nag)));
            a->setData(nag);
        }
        nagMenu->addSeparator();
        clearNags = nagMenu->addAction(tr("Effacer"));
        menu.addSeparator();
        promote = menu.addAction(theme::icon("up"), tr("Promouvoir la variante"));
        promote->setEnabled(!n->isMainline());
        demote = menu.addAction(tr("Descendre la variante"));
        demote->setToolTip(tr("La ligne passe un rang plus bas parmi celles de sa position (la ligne principale devient la première variante)"));
        {
            // Enabled when a line of the same position comes after this one
            const Node* head = n;
            while (head->parent && head->parent->children.front().get() == head && head->parent->children.size() == 1) head = head->parent;
            demote->setEnabled(head->parent && head->parent->children.back().get() != head);
        }
        // Replacing a move: the new one is played on the board
        replace = menu.addAction(theme::icon("edit"), tr("Remplacer ce coup… (la suite est supprimée)"));
        replace->setToolTip(tr("L'échiquier revient avant %1 : jouez le coup qui le remplace").arg(display::san(n->san)));
        insert = menu.addAction(tr("Remplacer ce coup en gardant la suite…"));
        insert->setToolTip(tr("Comme ci-dessus, mais les coups qui suivaient sont gardés tant qu'ils restent légaux"));
        nullMove = menu.addAction(tr("Jouer un coup nul après ce coup (passer)"));
        nullMove->setEnabled(!n->pos.inCheck());
        del = menu.addAction(theme::icon("trash"), tr("Supprimer à partir d'ici"));
        menu.addSeparator();
        menu.setToolTipsVisible(true);
    }

    QAction* send = nullptr;
    if (m_sendToAnalysis) {
        send = menu.addAction(theme::icon("analysis"), tr("Envoyer dans l'analyse"));
        menu.addSeparator();
    }

    // Save the game: PGN file, or addition to an open Hydra64 database
    QMenu* save = menu.addMenu(theme::icon("save"), tr("Enregistrer la partie"));
    QAction* exportPgn = save->addAction(tr("Exporter en PGN…"));
    connect(exportPgn, &QAction::triggered, this, &NotationView::exportPgnRequested);
    QAction* exportHtml = save->addAction(tr("Exporter en page HTML…"));
    exportHtml->setToolTip(tr("Une page autonome avec les coups, les variantes, les commentaires et un échiquier pour rejouer la partie"));
    connect(exportHtml, &QAction::triggered, this, &NotationView::exportHtmlRequested);
    const QList<QPair<QString, QString>> bases = m_databases ? m_databases() : QList<QPair<QString, QString>>();
    QList<QPair<QAction*, QString>> addTo;
    if (bases.isEmpty()) {
        QAction* none = save->addAction(tr("Ajouter à une base Hydra64 (aucune ouverte)"));
        none->setEnabled(false);
    } else if (bases.size() == 1) {
        addTo.append({save->addAction(theme::icon("database"), tr("Ajouter à la base « %1 »").arg(bases.first().first)), bases.first().second});
    } else {
        QMenu* which = save->addMenu(theme::icon("database"), tr("Ajouter à la base"));
        for (int i = 0; i < bases.size(); ++i)
            addTo.append({which->addAction(i == 0 ? tr("%1 (active)").arg(bases[i].first) : bases[i].first), bases[i].second});
    }

    for (const auto& [action, path] : addTo) {
        const QString p = path;
        connect(action, &QAction::triggered, this, [this, p] { emit addToDatabaseRequested(p); });
    }

    QAction* chosen = menu.exec(e->globalPos());
    if (chosen && chosen == send) {
        emit sendToAnalysisRequested(); // the page's game is copied to the Analysis page
        return;
    }
    if (!chosen || !onMove) return; // saving: already done by the action's signal
    if (chosen == comment) {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Commentaire"), tr("Commentaire après %1 :").arg(display::san(n->san)),
                                                            QString::fromStdString(n->comment), &ok);
        if (ok) m_session->setComment(n, text);
    } else if (chosen == preComment) {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Commentaire"), tr("Commentaire avant %1 :").arg(display::san(n->san)),
                                                            QString::fromStdString(n->preComment), &ok);
        if (ok) m_session->setPreComment(n, text);
    } else if (chosen == promote) {
        m_session->promoteVariation(n);
    } else if (chosen == demote) {
        m_session->demoteVariation(n);
    } else if (chosen == replace || chosen == insert) {
        m_session->armReplace(n, chosen == insert);
    } else if (chosen == nullMove) {
        m_session->goTo(n);
        m_session->playNullMove();
    } else if (chosen == del) {
        m_session->deleteNode(n);
    } else if (chosen == clearNags) {
        m_session->checkpoint();
        n->nags.clear();
        emit m_session->treeChanged();
    } else if (chosen->data().isValid()) {
        const int nag = chosen->data().toInt();
        m_session->checkpoint();
        std::erase_if(n->nags, [](int x) { return x <= 6; });
        n->nags.insert(n->nags.begin(), nag);
        emit m_session->treeChanged();
    }
}

// ---------------------------------------------------------------- NotationPanel

NotationPanel::NotationPanel(GameSession* session, QWidget* parent)
    : QWidget(parent)
    , m_view(new NotationView(session))
{
    auto* bar = new QWidget;
    bar->setObjectName("PanelBody");
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(8, 4, 6, 0);
    bl->setSpacing(2);
    auto button = [](const QString& icon, const QString& tip) {
        auto* b = new QToolButton;
        b->setIcon(theme::icon(icon));
        b->setIconSize(QSize(18, 18));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    m_smaller = button("fontDown", tr("Réduire la police de la notation (Ctrl+-)"));
    m_larger = button("fontUp", tr("Agrandir la police de la notation (Ctrl++)"));
    m_size = new QLabel;
    m_size->setObjectName("Faint");
    m_engineNotes = button("engine", tr("Afficher les notes des moteurs dans les parties de tournoi (évaluation/profondeur, temps). Masquées, elles restent dans la partie et dans le PGN."));
    m_engineNotes->setObjectName("EngineNotes");
    m_engineNotes->setCheckable(true);
    bl->addWidget(m_engineNotes);
    bl->addStretch(1);
    bl->addWidget(m_size);
    bl->addWidget(m_smaller);
    bl->addWidget(m_larger);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar);
    layout->addWidget(m_view, 1);

    connect(m_smaller, &QToolButton::clicked, this, [] { prefs::setNotationFontSize(prefs::notationFontSize() - 1); });
    connect(m_larger, &QToolButton::clicked, this, [] { prefs::setNotationFontSize(prefs::notationFontSize() + 1); });
    connect(m_engineNotes, &QToolButton::toggled, this, [](bool on) { prefs::setShowEngineComments(on); });
    connect(&prefs::Notifier::instance(), &prefs::Notifier::changed, this, &NotationPanel::refresh);
    refresh();
}

void NotationPanel::refresh()
{
    const int px = prefs::notationFontSize();
    if (m_engineNotes->isChecked() != prefs::showEngineComments()) m_engineNotes->setChecked(prefs::showEngineComments());
    m_size->setText(QStringLiteral("%1 px").arg(px));
    m_smaller->setEnabled(px > prefs::MinNotationFontSize);
    m_larger->setEnabled(px < prefs::MaxNotationFontSize);
}

#include "addexercise.h"
#include "display.h"

#include "annotator.h"
#include "hy64source.h"
#include "chess/pgn.h"
#include "gamelist.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace chess;

namespace {
constexpr int MaxKeptPlies = 20; // continuation kept at most (except up to a mate further on)
}

namespace exercise {

Game toGame(const Exercise& ex)
{
    Game g(ex.pos);
    g.setHeader("Event", ex.name.isEmpty() ? std::string("?") : ex.name.toStdString());
    g.setHeader("Site", "?");
    g.setHeader("Date", "????.??.??");
    g.setHeader("Round", "?");
    g.setHeader("White", "?");
    g.setHeader("Black", "?");
    g.setHeader("Result", "*");
    if (ex.pos.fen() != Position::startPos().fen()) {
        g.setHeader("SetUp", "1");
        g.setHeader("FEN", ex.pos.fen());
    }
    Node* n = g.root();
    for (const Move& m : ex.line) n = g.addMove(n, m);
    // Starting comment, in EPD operation format: "dm N; c0 "text""
    QString comment;
    if (ex.mateIn > 0) comment = QStringLiteral("dm %1").arg(ex.mateIn);
    const QString text = ex.comment.trimmed();
    if (!text.isEmpty()) {
        QString quoted = text;
        quoted.replace(QLatin1Char('"'), QLatin1Char('\''));
        comment += (comment.isEmpty() ? QString() : QStringLiteral("; ")) + QStringLiteral("c0 \"%1\"").arg(quoted);
    }
    g.root()->comment = comment.toStdString();
    return g;
}

int mateLength(const Position& pos, std::vector<Move>& line)
{
    Position p = pos;
    for (size_t i = 0; i < line.size(); ++i) {
        if (!p.isLegal(line[i])) break;
        p.makeMove(line[i]);
        if (p.isCheckmate()) {
            if (i % 2 != 0) return 0; // mate delivered by the opponent
            line.resize(i + 1);
            return int(i / 2) + 1;
        }
    }
    return 0;
}

QString collectionsDir() { return QFileInfo(annotation::defaultExercisesFile()).absolutePath(); }

bool isCollectionPath(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("epd") || s == QLatin1String("pgn") || s == QLatin1String("hy64i") || s == QLatin1String("hy64");
}

QStringList collections()
{
    QStringList out;
    for (const QFileInfo& fi : QDir(collectionsDir()).entryInfoList({QStringLiteral("*.epd"), QStringLiteral("*.pgn"), QStringLiteral("*.hy64i"), QStringLiteral("*.hy64")},
                                                                      QDir::Files, QDir::Name | QDir::IgnoreCase))
        out << fi.absoluteFilePath();
    return out;
}

bool append(const QString& path, const Exercise& ex, GameListModel* open, QString* error)
{
    if (ex.line.empty()) {
        if (error) *error = QObject::tr("aucune solution");
        return false;
    }
    if (!isCollectionPath(path)) {
        if (error) *error = QObject::tr("un recueil est un fichier EPD, PGN ou une base Hydra64");
        return false;
    }
    const std::string pgn = pgn::writeGame(toGame(ex));
    if (open) return open->appendGame(pgn, error) >= 0;
    if (!QFileInfo::exists(path)) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (Hy64Source::isDatabasePath(path)) {
            if (!Hy64Source::create(path, error)) return false;
        } else {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                if (error) *error = f.errorString();
                return false;
            }
        }
    }
    GameListModel model;
    if (!model.loadFile(path, error)) return false;
    return model.appendGame(pgn, error) >= 0;
}

} // namespace exercise

// ---------------------------------------------------------------- dialog

AddExerciseDialog::AddExerciseDialog(const Position& pos, std::vector<Move> following, const QString& defaultName, QWidget* parent)
    : QDialog(parent)
    , m_pos(pos)
    , m_following(std::move(following))
{
    setWindowTitle(tr("Ajouter au recueil"));
    QSettings s;
    m_collection = new QComboBox;
    for (const QString& p : exercise::collections()) m_collection->addItem(QFileInfo(p).fileName(), p);
    const QString last = s.value(QStringLiteral("exercise/collection"), annotation::defaultExercisesFile()).toString();
    if (m_collection->findData(last) < 0) m_collection->addItem(QFileInfo(last).fileName(), last);
    m_collection->addItem(tr("Choisir ou créer un fichier…"), QString());
    m_collection->setCurrentIndex(m_collection->findData(last));
    m_collection->setToolTip(tr("Recueils de %1 : EPD, PGN ou base Hydra64").arg(QDir::toNativeSeparators(exercise::collectionsDir())));

    m_name = new QLineEdit(defaultName);
    m_name->setPlaceholderText(tr("Nom de l'exercice (facultatif)"));
    m_type = new QComboBox;
    m_type->addItem(tr("Trouver le meilleur coup"));
    m_type->addItem(tr("Mat en N"));
    m_mateIn = new QSpinBox;
    m_mateIn->setRange(1, 8);
    m_mateIn->setPrefix(tr("mat en "));
    m_keepLine = new QCheckBox(tr("Garder la suite de la notation (montrée par « Solution »)"));
    m_keepLine->setChecked(s.value(QStringLiteral("exercise/keepLine"), true).toBool());
    m_comment = new QLineEdit;
    m_comment->setPlaceholderText(tr("Commentaire (facultatif)"));
    m_solution = new QLabel;
    m_solution->setWordWrap(true);
    m_solution->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* typeRow = new QHBoxLayout;
    typeRow->addWidget(m_type, 1);
    typeRow->addWidget(m_mateIn);
    auto* form = new QFormLayout;
    form->addRow(tr("Recueil"), m_collection);
    form->addRow(tr("Nom"), m_name);
    form->addRow(tr("Exercice"), typeRow);
    form->addRow(tr("Solution"), m_solution);
    form->addRow(QString(), m_keepLine);
    form->addRow(tr("Commentaire"), m_comment);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Ajouter"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    // Mate detected: the rest of the notation ends with a mate by the side to move
    {
        std::vector<Move> probe = m_following;
        m_detectedMate = exercise::mateLength(m_pos, probe);
    }
    m_type->setCurrentIndex(m_detectedMate > 0 ? 1 : 0);
    m_mateIn->setValue(std::max(1, m_detectedMate));

    connect(m_type, &QComboBox::currentIndexChanged, this, &AddExerciseDialog::updateSolution);
    connect(m_keepLine, &QCheckBox::toggled, this, &AddExerciseDialog::updateSolution);
    connect(m_mateIn, &QSpinBox::valueChanged, this, &AddExerciseDialog::updateSolution);
    connect(m_collection, &QComboBox::activated, this, [this] {
        if (m_collection->currentData().toString().isEmpty()) chooseFile();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!hasSolution() || collectionPath().isEmpty()) return;
        QSettings st;
        st.setValue(QStringLiteral("exercise/collection"), collectionPath());
        st.setValue(QStringLiteral("exercise/keepLine"), m_keepLine->isChecked());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(hasSolution());
    updateSolution();
}

void AddExerciseDialog::chooseFile()
{
    QString filter = tr("Recueil EPD (*.epd)");
    QString path = QFileDialog::getSaveFileName(this, tr("Recueil"), exercise::collectionsDir() + QStringLiteral("/Mes exercises.epd"),
                                                tr("Recueil EPD (*.epd)") + QStringLiteral(";;") + tr("Parties PGN (*.pgn)") + QStringLiteral(";;")
                                                    + tr("Base Hydra64 (*.hy64)"),
                                                &filter, QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty()) {
        m_collection->setCurrentIndex(0);
        return;
    }
    if (!exercise::isCollectionPath(path)) path += filter.contains(QLatin1String("hy64")) ? QStringLiteral(".hy64")
                                                    : filter.contains(QLatin1String("pgn")) ? QStringLiteral(".pgn")
                                                                                            : QStringLiteral(".epd");
    setCollectionPath(path);
}

void AddExerciseDialog::setCollectionPath(const QString& path)
{
    int i = m_collection->findData(path);
    if (i < 0) {
        m_collection->insertItem(m_collection->count() - 1, QFileInfo(path).fileName(), path);
        i = m_collection->findData(path);
    }
    m_collection->setCurrentIndex(i);
}

void AddExerciseDialog::setKeepLine(bool on) { m_keepLine->setChecked(on); }
void AddExerciseDialog::setMate(bool on) { m_type->setCurrentIndex(on ? 1 : 0); }

QString AddExerciseDialog::collectionPath() const { return m_collection->currentData().toString(); }

exercise::Exercise AddExerciseDialog::exercise() const
{
    exercise::Exercise ex;
    ex.pos = m_pos;
    ex.name = m_name->text().trimmed();
    ex.comment = m_comment->text().trimmed();
    if (m_following.empty()) return ex;
    const bool mate = m_type->currentIndex() == 1;
    if (m_keepLine->isChecked()) {
        ex.line = m_following;
        // Up to the mate if there is one, otherwise a reasonable continuation
        std::vector<Move> toMate = m_following;
        if (exercise::mateLength(m_pos, toMate) > 0) ex.line = toMate;
        else if (ex.line.size() > size_t(MaxKeptPlies)) ex.line.resize(MaxKeptPlies);
    } else {
        ex.line = {m_following.front()};
    }
    ex.mateIn = mate ? m_mateIn->value() : 0;
    return ex;
}

void AddExerciseDialog::updateSolution()
{
    m_mateIn->setEnabled(m_type->currentIndex() == 1);
    if (m_following.empty()) {
        m_solution->setText(tr("Aucun coup suivant dans la notation : jouez d'abord la solution sur l'échiquier, puis revenez d'un coup."));
        return;
    }
    const exercise::Exercise ex = exercise();
    QStringList sans;
    Position p = m_pos;
    for (size_t i = 0; i < ex.line.size(); ++i) {
        const Move m = ex.line[i];
        const QString number = p.sideToMove() == White ? QStringLiteral("%1. ").arg(p.fullmoveNumber())
                               : i == 0               ? QStringLiteral("%1... ").arg(p.fullmoveNumber())
                                                      : QString();
        sans << number + display::san(p.toSan(m));
        p.makeMove(m);
    }
    QString text = QStringLiteral("<b>%1</b>").arg(sans.first().toHtmlEscaped());
    if (sans.size() > 1) text += QStringLiteral(" ") + sans.mid(1).join(QLatin1Char(' ')).toHtmlEscaped();
    if (m_detectedMate > 0) text += QStringLiteral("<br><span style='color:gray'>%1</span>").arg(tr("La suite mate en %n coup(s).", nullptr, m_detectedMate));
    m_solution->setText(text);
}

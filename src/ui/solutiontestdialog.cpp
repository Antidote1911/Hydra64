#include "solutiontestdialog.h"

#include "bundledbases.h"
#include "display.h"

#include "chess/position.h"
#include "theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Column { ColNumber, ColId, ColSolution, ColMove, ColTime, ColDepth, ColResult, ColumnCount };

// UCI move → notation, in the test's position
QString sanOf(const QString& fen, const QString& uci)
{
    chess::Position p;
    if (uci.isEmpty() || !p.setFen(fen.toStdString())) return uci;
    const chess::Move m = p.parseUci(uci.toStdString());
    return m.isNull() ? uci : display::san(p.toSan(m));
}
} // namespace

SolutionTestDialog::SolutionTestDialog(QWidget* parent)
    : QDialog(parent)
    , m_test(new SolutionTest(this))
{
    setWindowTitle(tr("Test de solutions"));
    resize(820, 620);
    QSettings s;

    m_engine = new QComboBox;
    refreshEngines();
    m_file = new QLineEdit(s.value("solutionTest/file").toString());
    m_file->setPlaceholderText(tr("Recueil de positions : EPD (bm / am), PGN ou base Hydra64"));
    auto* browse = new QPushButton(theme::icon("open"), QString());
    browse->setToolTip(tr("Choisir le recueil…"));
    auto* fileRow = new QHBoxLayout;
    fileRow->addWidget(m_file, 1);
    fileRow->addWidget(browse);
    m_time = new QDoubleSpinBox;
    m_time->setRange(0.1, 3600);
    m_time->setDecimals(1);
    m_time->setSuffix(tr(" s"));
    m_time->setValue(s.value("solutionTest/seconds", 5.0).toDouble());
    m_time->setToolTip(tr("Temps de réflexion du moteur pour chaque position"));

    auto* form = new QFormLayout;
    form->addRow(tr("Moteur"), m_engine);
    form->addRow(tr("Positions"), fileRow);
    form->addRow(tr("Temps par position"), m_time);

    m_table = new QTableWidget(0, ColumnCount);
    m_table->setHorizontalHeaderLabels({tr("N°"), tr("Position"), tr("Solution"), tr("Coup du moteur"), tr("Trouvé en"), tr("Prof."), tr("Résultat")});
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(ColNumber, 48);
    m_table->setColumnWidth(ColId, 120);
    m_table->setColumnWidth(ColSolution, 150);
    m_table->setColumnWidth(ColMove, 120);
    m_table->setColumnWidth(ColTime, 80);
    m_table->setColumnWidth(ColDepth, 50);
    m_table->setToolTip(tr("Double-clic : ouvrir la position dans l'analyse"));

    m_summary = new QLabel;
    m_summary->setObjectName("Faint");
    m_run = new QPushButton(theme::icon("play"), tr("Lancer"));
    m_run->setObjectName("Primary");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    buttons->addButton(m_run, QDialogButtonBox::ActionRole);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_summary);
    layout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, &SolutionTestDialog::chooseFile);
    connect(m_file, &QLineEdit::editingFinished, this, &SolutionTestDialog::loadFile);
    connect(m_run, &QPushButton::clicked, this, &SolutionTestDialog::toggle);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_positions.size()) emit analyseRequested(m_positions[row].fen);
    });
    connect(&EngineRegistry::instance(), &EngineRegistry::changed, this, &SolutionTestDialog::refreshEngines);
    connect(m_test, &SolutionTest::positionStarted, this, [this](int i) {
        updateRow(i);
        m_table->scrollToItem(m_table->item(i, 0));
    });
    connect(m_test, &SolutionTest::positionUpdated, this, &SolutionTestDialog::updateRow);
    connect(m_test, &SolutionTest::positionFinished, this, [this](int i) {
        updateRow(i);
        updateSummary();
    });
    connect(m_test, &SolutionTest::finished, this, [this](bool) {
        setRunning(false);
        updateSummary();
    });
    connect(m_test, &SolutionTest::errorOccurred, this, [this](const QString& message) { m_summary->setText(message); });

    if (!m_file->text().isEmpty()) loadFile();
    updateSummary();
}

void SolutionTestDialog::refreshEngines()
{
    const QString current = m_engine->currentData().toString().isEmpty() ? QSettings().value("solutionTest/engine").toString()
                                                                          : m_engine->currentData().toString();
    m_engine->clear();
    for (const EngineConfig& e : EngineRegistry::instance().engines()) m_engine->addItem(e.name, e.id);
    const int i = m_engine->findData(current);
    if (i >= 0) m_engine->setCurrentIndex(i);
}

void SolutionTestDialog::setFile(const QString& path)
{
    m_file->setText(QDir::toNativeSeparators(path));
    loadFile();
}

void SolutionTestDialog::setMovetime(double seconds) { m_time->setValue(seconds); }

void SolutionTestDialog::chooseFile()
{
    QSettings s;
    // No collection chosen: the folder of the bundled tests
    const QString start = m_file->text().isEmpty() ? bundled::databasesDir() + QStringLiteral("/engine-tests") : QFileInfo(m_file->text()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Recueil de positions"), start,
                                                      tr("Recueils (*.hy64 *.hy64i *.epd *.pgn);;Base Hydra64 (*.hy64 *.hy64i);;EPD (*.epd);;Parties PGN (*.pgn);;"
                                                         "Tous les fichiers (*)"));
    if (path.isEmpty()) return;
    s.setValue("lastDir", QFileInfo(path).absolutePath());
    setFile(path);
}

void SolutionTestDialog::loadFile()
{
    if (m_test->isRunning()) return;
    const QString path = QDir::fromNativeSeparators(m_file->text().trimmed());
    m_positions.clear();
    m_table->setRowCount(0);
    if (path.isEmpty()) {
        updateSummary();
        return;
    }
    QString error;
    m_positions = solutiontest::load(path, &error);
    if (m_positions.isEmpty()) {
        m_summary->setText(error);
        return;
    }
    QSettings().setValue("solutionTest/file", path);
    m_table->setRowCount(int(m_positions.size()));
    for (int i = 0; i < m_positions.size(); ++i) {
        auto item = [&](int col, const QString& text) {
            auto* it = new QTableWidgetItem(text);
            if (col == ColNumber || col == ColTime || col == ColDepth) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(i, col, it);
        };
        item(ColNumber, QString::number(i + 1));
        item(ColId, m_positions[i].id);
        item(ColSolution, m_positions[i].solution);
        for (int c : {ColMove, ColTime, ColDepth, ColResult}) item(c, QString());
    }
    updateSummary();
}

bool SolutionTestDialog::startTest()
{
    if (m_positions.isEmpty()) loadFile();
    if (m_positions.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), m_summary->text().isEmpty() ? tr("Choisissez un recueil de positions.") : m_summary->text());
        return false;
    }
    const EngineConfig cfg = EngineRegistry::instance().engine(m_engine->currentData().toString());
    QSettings s;
    s.setValue("solutionTest/engine", cfg.id);
    s.setValue("solutionTest/seconds", m_time->value());
    loadFile(); // previous results cleared
    QString error;
    if (!m_test->start(cfg, m_positions, int(m_time->value() * 1000), &error)) {
        QMessageBox::warning(this, windowTitle(), error);
        return false;
    }
    setRunning(true);
    updateSummary();
    return true;
}

void SolutionTestDialog::toggle()
{
    if (m_test->isRunning()) m_test->stop();
    else startTest();
}

void SolutionTestDialog::setRunning(bool running)
{
    m_run->setText(running ? tr("Arrêter") : tr("Lancer"));
    m_run->setIcon(theme::icon(running ? "close" : "play"));
    m_engine->setEnabled(!running);
    m_file->setEnabled(!running);
    m_time->setEnabled(!running);
}

void SolutionTestDialog::updateRow(int i)
{
    if (i < 0 || i >= m_test->results().size() || i >= m_table->rowCount()) return;
    const SolutionTest::Result& r = m_test->results()[i];
    const TestPosition& p = m_test->positions()[i];
    const QLocale loc;
    const bool good = solutiontest::isCorrect(p, r.move);
    m_table->item(i, ColMove)->setText(sanOf(p.fen, r.move));
    m_table->item(i, ColTime)->setText(good && r.foundMs >= 0 ? loc.toString(double(r.foundMs) / 1000.0, 'f', 2) + tr(" s") : QString());
    m_table->item(i, ColDepth)->setText(r.depth > 0 ? QString::number(r.depth) : QString());
    m_table->item(i, ColResult)->setText(!r.done ? tr("…") : r.solved ? tr("✓ résolue") : tr("✗"));
    const QColor color = !r.done ? theme::TextDim : r.solved ? QColor(0x6a, 0xbf, 0x69) : QColor(0xe0, 0x5c, 0x4f);
    for (int c : {ColMove, ColResult}) m_table->item(i, c)->setForeground(color);
}

void SolutionTestDialog::updateSummary()
{
    if (m_positions.isEmpty()) {
        if (m_summary->text().isEmpty()) m_summary->setText(tr("Choisissez un moteur et un recueil (Tactics Marathon, WAC…)."));
        return;
    }
    const QList<SolutionTest::Result>& results = m_test->results();
    int done = 0, solved = 0;
    qint64 total = 0;
    for (const SolutionTest::Result& r : results) {
        if (!r.done) continue;
        ++done;
        if (r.solved) {
            ++solved;
            total += r.foundMs;
        }
    }
    const QLocale loc;
    if (done == 0) {
        m_summary->setText(m_test->isRunning() ? tr("Test en cours…") : tr("%n position(s) à résoudre", nullptr, int(m_positions.size())));
        return;
    }
    QString text = tr("Résolues : %1 / %2 (%3 %)").arg(solved).arg(done).arg(loc.toString(100.0 * solved / done, 'f', 1));
    if (solved) text += tr(" · temps moyen de solution %1 s").arg(loc.toString(double(total) / solved / 1000.0, 'f', 2));
    if (done < m_positions.size()) text += tr(" · %1 / %2 positions passées").arg(done).arg(m_positions.size());
    m_summary->setText(text);
}

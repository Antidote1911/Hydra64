#pragma once

#include "db/headerquery.h"
#include "indexedsource.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QLineEdit;
class QSpinBox;

// Database → Advanced search…: search by headers (players, Elo, dates, ECO, result,
// length, tournament…), new or combined with the current result (AND / OR).
class HeaderSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit HeaderSearchDialog(QWidget* parent = nullptr);
    hy64db::HeaderQuery query() const;
    IndexedGameSource::Combine combine() const;
    void setQuery(const hy64db::HeaderQuery& q);
    // Reference position of the "same pawns / files / material" searches (board)
    void setReferencePosition(const chess::Position& pos) { m_ref = pos; }
    void showMaterialTab();

private:
    void clear();
    QLineEdit *m_white, *m_black, *m_event, *m_site, *m_round, *m_annotator, *m_ecoFrom, *m_ecoTo;
    QCheckBox *m_ignoreColors, *m_variations, *m_comments;
    QSpinBox *m_wMin, *m_wMax, *m_bMin, *m_bMax, *m_yFrom, *m_yTo, *m_movesMin, *m_movesMax;
    QCheckBox *m_rWhite, *m_rDraw, *m_rBlack, *m_rUnknown;
    QButtonGroup* m_combine;
    QList<QCheckBox*> m_flags;
    // Material and patterns
    class QTabWidget* m_tabs;
    QSpinBox* m_min[2][7] = {};
    QSpinBox* m_max[2][7] = {};
    QCheckBox* m_useDiff;
    QSpinBox *m_diffMin, *m_diffMax, *m_plyFrom, *m_plyTo, *m_minPlies;
    class QComboBox *m_bishops, *m_reference;
    QList<QPair<QCheckBox*, uint32_t>> m_patterns;
    chess::Position m_ref = chess::Position::startPos();
};

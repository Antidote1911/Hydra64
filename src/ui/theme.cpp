#include "theme.h"

#include <QApplication>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QSvgRenderer>

namespace theme {

namespace {

QString hex(const QColor& c)
{
    if (c.alpha() == 255) return c.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

const char* kStyle = R"QSS(
* { outline: none; }
QWidget { color: @text; background: transparent; selection-background-color: @accentSoft; selection-color: @text; }
QMainWindow, QDialog { background: @window; }
QMainWindow::separator { background: @border; width: 3px; height: 3px; }
QMainWindow::separator:hover { background: @accent; }

/* ---- Barre de menus ---- */
QMenuBar { background: @window; border-bottom: 1px solid @border; padding: 2px 6px; }
QMenuBar::item { padding: 4px 10px; border-radius: 4px; color: @textDim; }
QMenuBar::item:selected { background: @raised; color: @text; }
QMenu { background: @panelAlt; border: 1px solid @border; border-radius: 6px; padding: 5px; }
QMenu::item { padding: 6px 28px 6px 14px; border-radius: 4px; }
QMenu::item:selected { background: transparent; color: @accent; }
QMenu::item:disabled { color: @textFaint; }
QMenu::separator { height: 1px; background: @borderSoft; margin: 5px 8px; }
QMenu::icon { padding-left: 6px; }

/* ---- Panneaux ---- */
#PanelBody, #Viewer { background: @panel; }
#ViewerTop, #Transport { background: @panel; }
#PlayerName { color: @text; font-weight: 600; }
#PlayerElo { color: @textFaint; }
#PlayerClockDot { border-radius: 4px; }

/* ---- Barre des pages (bas de l'écran, façon Resolve) ---- */
#PageBar { background: @window; border-top: 1px solid @border; }
#PageBar QToolButton { color: @textFaint; background: transparent; border: none; padding: 4px 18px; font-size: 11px; }
#PageBar QToolButton:hover { color: @text; }
#PageBar QToolButton:checked { color: @accent; }
#PageBarTitle { color: @textFaint; font-size: 17px; }
#PageBar QToolButton#BrandLogo { padding: 2px; border-radius: 6px; }
#PageBar QToolButton#BrandLogo:hover { background: @raised; }

/* ---- Boutons ---- */
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 3px; }
QToolButton:hover { background: @raised; }
QToolButton:pressed { background: @border; }
QToolButton:checked { background: @accentSoft; border-color: @accent; }
#TransportButton { padding: 5px; }
QPushButton { background: @raised; border: 1px solid @border; border-radius: 4px; padding: 5px 14px; }
QPushButton:hover { background: #3d3d43; }
QPushButton:pressed { background: @header; }
QPushButton:default { border-color: @accent; }
#AnalyseButton { padding: 3px 12px 3px 8px; font-weight: 600; }
#AnalyseButton:checked { background: @accent; color: #141416; border-color: @accent; }

#Primary { background: @accent; color: #141416; border-color: @accent; font-weight: 600; }
#Primary:hover { background: #f09a48; }
#Primary:disabled { background: @raised; color: @textFaint; border-color: @border; font-weight: 400; }
#Segment { border-radius: 0; padding: 7px 14px; background: @window; color: @textDim; }
#Segment[position="first"] { border-top-left-radius: 4px; border-bottom-left-radius: 4px; }
#Segment[position="last"] { border-top-right-radius: 4px; border-bottom-right-radius: 4px; }
#Segment:hover { color: @text; }
#Segment:checked { background: @raised; color: @text; border-color: @accent; }

/* ---- Liste des catégories des Préférences ---- */
#PrefsNav { background: @panel; border: 1px solid @border; border-radius: 6px; padding: 6px; }
#PrefsNav::item { padding: 8px 10px; margin: 1px 0; border-radius: 4px; color: @textDim; }
#PrefsNav::item:hover { background: @raised; color: @text; }
#PrefsNav::item:selected { background: transparent; color: @accent; }
#PlayButton { padding: 4px 10px; color: @textDim; }
#PlayButton:hover { color: @text; }
#PlayerStatus { color: @accent; }

/* ---- Onglet du moteur adversaire (façon « module ») ---- */
#ModuleButton, #ModuleName { background: @raised; border: 1px solid @border; border-radius: 4px; padding: 3px 7px; color: @textDim; }
#ModuleButton:hover, #ModuleName:hover { background: #3d3d43; color: @text; }
#ModuleButton:disabled { color: @textFaint; background: @header; }
#ModuleButton:checked { background: @accentSoft; border-color: @accent; color: @text; }
#ModuleName { color: @text; font-weight: 700; padding: 3px 10px; }
#ModuleStat { background: @window; border: 1px solid @border; border-radius: 3px; padding: 2px 8px; color: @textDim; }

/* ---- Tournoi ---- */
#Standings { font-weight: 600; }

/* ---- Pendule ---- */
#Clock { font-family: "JetBrains Mono", "DejaVu Sans Mono", monospace; font-size: 17px; font-weight: 600; color: @textDim;
         background: @window; border: 1px solid @border; border-radius: 4px; padding: 1px 10px; }
#Clock[active="true"] { color: #141416; background: @text; border-color: @text; }
#Clock[low="true"] { color: @bad; border-color: @bad; }
#Clock[active="true"][low="true"] { color: #ffffff; background: @bad; border-color: @bad; }

/* ---- Panneau Pendule (façon Fritz) ---- */
#BigClock { font-family: "JetBrains Mono", "DejaVu Sans Mono", monospace; font-size: 30px; font-weight: 600;
            color: @textDim; background: #08080a; border: 2px solid @border; border-radius: 4px; padding: 4px 10px; }
#BigClock[active="true"] { color: #ffffff; border-color: @accent; }
#BigClock[low="true"] { color: @bad; }
#ClockMarkWhite { background: #eeece6; border: 1px solid @borderSoft; }
#ClockMarkBlack { background: #141416; border: 1px solid @borderSoft; }
#ClockMarkWhite[active="true"], #ClockMarkBlack[active="true"] { border: 2px solid @accent; }

/* ---- Bandeau de fin de partie ---- */
#Banner { background: rgba(18,18,20,236); border: 1px solid @borderSoft; border-radius: 8px; }
#BannerTitle { color: @text; font-size: 20px; font-weight: 600; }
#BannerText { color: @textDim; }
#BannerClose { background: transparent; border: none; color: @textDim; padding: 4px 10px; }
#BannerClose:hover { color: @text; }

/* ---- Gestion des moteurs ---- */
#ManagerSide { background: @panel; border-right: 1px solid @border; }
#EngineList { background: transparent; }
#EngineList::item { background: transparent; }
#TitleEdit { background: transparent; border: 1px solid transparent; font-size: 20px; font-weight: 600; padding: 2px 4px; margin-left: -5px; }
#TitleEdit:hover { border-color: @borderSoft; }
#TitleEdit:focus { border-color: @accent; background: @window; }
#OptionTable { background: @panel; border: 1px solid @border; border-radius: 4px; }
#OptionTable QLineEdit, #OptionTable QSpinBox, #OptionTable QComboBox { padding: 2px 6px; }
#OptionTable QComboBox { padding-right: 22px; }
#Segment[position="single"] { border-radius: 4px; }

/* ---- Éditeur de position ---- */
#PaletteButton { background: @window; border: 1px solid @border; border-radius: 4px; padding: 2px; }
#PaletteButton:hover { background: @raised; }
#PaletteButton:checked { background: @accentSoft; border-color: @accent; }
#FenEdit { padding: 6px 8px; }
QCheckBox:disabled { color: @textFaint; }

/* ---- Curseurs et cases à cocher ---- */
QSlider::groove:horizontal { height: 4px; background: @window; border: 1px solid @border; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::sub-page:horizontal:disabled { background: @textFaint; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -6px 0; border-radius: 7px; background: @text; border: 1px solid @border; }
QSlider::handle:horizontal:disabled { background: @textFaint; }
QCheckBox { spacing: 8px; }
/* A ticked box is not filled: a check mark in the accent colour (also in tables, trees and menus) */
QCheckBox::indicator, QGroupBox::indicator, QAbstractItemView::indicator { width: 14px; height: 14px; border-radius: 3px; border: 1px solid @borderSoft; background: @window; }
QCheckBox::indicator:checked, QGroupBox::indicator:checked, QAbstractItemView::indicator:checked { image: url(:/style/check.svg); }
QCheckBox::indicator:checked:disabled, QGroupBox::indicator:checked:disabled, QAbstractItemView::indicator:checked:disabled { image: url(:/style/check-disabled.svg); }
QCheckBox::indicator:hover { border-color: @accent; }
QMenu::indicator { width: 14px; height: 14px; margin-left: 6px; }
QMenu::indicator:checked { image: url(:/style/check.svg); }

/* ---- Champs ---- */
QLineEdit, QSpinBox, QComboBox { background: @window; border: 1px solid @border; border-radius: 4px; padding: 4px 6px; selection-background-color: @accent; selection-color: #141416; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: @accent; }
QSpinBox::up-button, QSpinBox::down-button { width: 0; border: none; }
QComboBox { padding-right: 22px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; border: none; width: 20px; }
QComboBox::down-arrow { image: url(:/style/chevron-down.svg); width: 12px; height: 12px; }
QComboBox::down-arrow:hover, QComboBox::down-arrow:on { image: url(:/style/chevron-down-hover.svg); }
QComboBox::down-arrow:disabled { image: url(:/style/chevron-down-disabled.svg); }
QComboBox QAbstractItemView { background: @panelAlt; border: 1px solid @border; selection-background-color: transparent; selection-color: @accent; }

/* ---- Tables et listes ---- */
QTableView, QTreeView, QListView { background: @panel; alternate-background-color: @panelAlt; border: none; gridline-color: transparent; }
QTableView::item { padding: 0 8px; border: none; }
/* Selected item: no highlight, its text in the accent colour */
QAbstractItemView { selection-background-color: transparent; selection-color: @accent; }
QTableView::item:selected, QTreeView::item:selected, QListView::item:selected { background: transparent; color: @accent; }
QHeaderView { background: @header; }
QHeaderView::section { background: @header; color: @textDim; border: none; border-right: 1px solid @border; border-bottom: 1px solid @border; padding: 5px 8px; font-weight: 600; }
QHeaderView::section:hover { color: @text; }
QTableCornerButton::section { background: @header; border: none; }

QTextBrowser, QPlainTextEdit, QTextEdit { background: @panel; border: none; }

/* ---- Barres de défilement fines ---- */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle { background: #3a3a40; border-radius: 3px; min-height: 30px; min-width: 30px; }
QScrollBar::handle:hover { background: #4a4a52; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QSplitter::handle { background: @border; }
QStatusBar { background: @window; color: @textFaint; border-top: 1px solid @border; }
QStatusBar QLabel { color: @textFaint; padding: 0 8px; }
QStatusBar::item { border: none; }
QToolTip { background: @panelAlt; color: @text; border: 1px solid @border; padding: 4px 8px; border-radius: 4px; }
QLabel#Dim { color: @textDim; }
QLabel#Faint { color: @textFaint; }

/* ---- Onglets des bases ouvertes ---- */
QTabBar#DatabaseTabs { background: @panel; border: none; }
QTabBar#DatabaseTabs::tab { background: transparent; color: @textFaint; border: none; border-bottom: 2px solid transparent;
                            padding: 5px 4px 5px 10px; margin-right: 2px; }
QTabBar#DatabaseTabs::tab:hover { color: @text; }
QTabBar#DatabaseTabs::tab:selected { color: @text; border-bottom-color: @accent; }
QTabBar#DatabaseTabs QToolButton { padding: 1px; border: none; }
)QSS";

const char* kDockStyle = R"QSS(
ads--CDockContainerWidget { background: @border; }
ads--CDockContainerWidget > QSplitter { padding: 0; }
ads--CDockContainerWidget ads--CDockSplitter::handle { background: @border; }
ads--CDockContainerWidget ads--CDockSplitter::handle:hover { background: @accent; }
ads--CDockAreaWidget { background: @panel; border: none; }
ads--CDockAreaTitleBar { background: @header; border: none; border-bottom: 1px solid @border; min-height: 28px; max-height: 28px; }
ads--CDockWidgetTab { background: @header; border: none; border-right: 1px solid @border; padding: 0 10px 0 8px; }
ads--CDockWidgetTab[activeTab="true"] { background: @panel; }
ads--CDockWidgetTab QLabel { color: @textFaint; font-size: 10px; font-weight: 600; }
ads--CDockWidgetTab[activeTab="true"] QLabel { color: @textDim; }
ads--CDockWidgetTab:hover QLabel { color: @text; }
ads--CDockWidget { background: @panel; border: none; }
QScrollArea#dockWidgetScrollArea { background: @panel; border: none; padding: 0; }
ads--CTitleBarButton { background: transparent; border: none; border-radius: 3px; padding: 3px; margin: 0 1px; }
ads--CTitleBarButton:hover { background: @raised; }
#tabCloseButton:hover { background: @raised; border: none; }
ads--CFloatingWidgetTitleBar { background: @header; }
ads--CAutoHideSideBar { background: @window; border: none; }
ads--CAutoHideTab { color: @textDim; background: none; }
ads--CAutoHideTab:hover, ads--CAutoHideTab[activeTab="true"] { color: @text; }
ads--CAutoHideDockContainer { background: @panel; }
ads--CAutoHideDockContainer ads--CDockAreaTitleBar,
ads--CAutoHideDockContainer ads--CDockAreaWidget[focused="true"] ads--CDockAreaTitleBar { background: @header; border-bottom: 1px solid @border; }
ads--CAutoHideDockContainer #autoHideTitleLabel { color: @textDim; font-size: 10px; font-weight: 600; }
ads--CResizeHandle { background: @border; }
)QSS";

QString withTokens(const char* css)
{
    QString s = QString::fromUtf8(css);
    const std::pair<const char*, QColor> tokens[] = {
        {"@accentSoft", AccentSoft}, {"@borderSoft", BorderSoft}, {"@panelAlt", PanelAlt},
        {"@textFaint", TextFaint},   {"@textDim", TextDim},       {"@window", Window},
        {"@panel", Panel},           {"@header", Header},         {"@raised", Raised},
        {"@border", Border},         {"@text", Text},             {"@accent", Accent},
        {"@bad", Bad},
    };
    for (const auto& [tok, color] : tokens) s.replace(QLatin1String(tok), hex(color));
    return s;
}

QString styleSheet() { return withTokens(kStyle); }

// Line icons, 24×24, colour "currentColor".
const QHash<QString, QString>& iconPaths()
{
    static const QHash<QString, QString> paths = {
        {"first", R"(<path d="M6 5v14M18 6l-7 6 7 6"/>)"},
        {"prev", R"(<path d="M15 6l-6 6 6 6"/>)"},
        {"next", R"(<path d="M9 6l6 6-6 6"/>)"},
        {"last", R"(<path d="M18 5v14M6 6l7 6-7 6"/>)"},
        {"flip", R"(<path d="M7 4v15M4 16l3 3 3-3M17 20V5M14 8l3-3 3 3"/>)"},
        {"engine", R"(<rect x="6.5" y="6.5" width="11" height="11" rx="2"/><path d="M10 3v3.5M14 3v3.5M10 17.5V21M14 17.5V21M3 10h3.5M3 14h3.5M17.5 10H21M17.5 14H21"/><rect x="10" y="10" width="4" height="4" rx=".5"/>)"},
        {"database", R"(<ellipse cx="12" cy="6" rx="7" ry="2.8"/><path d="M5 6v12c0 1.6 3.1 2.8 7 2.8s7-1.2 7-2.8V6M5 12c0 1.6 3.1 2.8 7 2.8s7-1.2 7-2.8"/>)"},
        {"analysis", R"(<path d="M4 4v16h16"/><path d="M7.5 15l3.5-4.5 3 2.5 5-6.5"/>)"},
        {"board", R"(<rect x="4" y="4" width="16" height="16" rx="2"/><path d="M4 12h16M12 4v16"/><path d="M4 4h8v8H4zM12 12h8v8h-8z" fill="currentColor" fill-opacity=".35" stroke="none"/>)"},
        {"open", R"(<path d="M3.5 7.5a2 2 0 0 1 2-2h4l2 2h7a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-13a2 2 0 0 1-2-2z"/>)"},
        {"save", R"(<path d="M5 4h11l3 3v13H5z"/><path d="M8 4v5h7V4M8 20v-6h8v6"/>)"},
        {"new", R"(<path d="M12 5v14M5 12h14"/>)"},
        {"play", R"(<path d="M8 5.5l10 6.5-10 6.5z" fill="currentColor"/>)"},
        {"stop", R"(<rect x="7" y="7" width="10" height="10" rx="1.5" fill="currentColor"/>)"},
        {"movenow", R"(<path d="M6 5.5l9 6.5-9 6.5z" fill="currentColor"/><rect x="16" y="5.5" width="2.5" height="13" rx="1" fill="currentColor" stroke="none"/>)"},
        {"copy", R"(<rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2"/>)"},
        {"paste", R"(<rect x="5" y="5" width="14" height="16" rx="2"/><path d="M9 5V3.5h6V5M9 11h6M9 15h4"/>)"},
        {"trash", R"(<path d="M4 7h16M9 7V4.5h6V7M6.5 7l1 13h9l1-13"/>)"},
        {"up", R"(<path d="M12 19V5M6 11l6-6 6 6"/>)"},
        {"comment", R"(<path d="M4 5h16v11H9l-5 4z"/>)"},
        {"search", R"(<circle cx="11" cy="11" r="6"/><path d="M20 20l-4.5-4.5"/>)"},
        {"edit", R"(<path d="M4 20h4L19 9l-4-4L4 16z"/><path d="M13.5 6.5l4 4"/>)"},
        {"cursor", R"(<path d="M6 3.5l11.5 8.5-5.2 1.1 3.2 6.4-2.4 1.2-3.2-6.4L6 18z"/>)"},
        {"eraser", R"(<path d="M9 20h11"/><path d="M5.6 14.6l8.2-8.2a2 2 0 0 1 2.8 0l2.4 2.4a2 2 0 0 1 0 2.8L12.2 18.4a2 2 0 0 1-1.4.6H8.4a2 2 0 0 1-1.4-.6l-1.4-1.4a2 2 0 0 1 0-2.4z"/><path d="M9.5 10.7l5 5"/>)"},
        {"mirror", R"(<path d="M12 3v18" stroke-dasharray="2 2.5"/><path d="M8.5 7L4 12l4.5 5M15.5 7L20 12l-4.5 5"/>)"},
        {"invert", R"(<circle cx="12" cy="12" r="8"/><path d="M12 4a8 8 0 0 1 0 16z" fill="currentColor"/>)"},
        {"clock", R"(<circle cx="12" cy="13" r="7.5"/><path d="M12 9v4l2.5 2.5M10 3h4M12 3v2.5"/>)"},
        {"minus", R"(<path d="M5 12h14"/>)"},
        {"lock", R"(<rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>)"},
        {"close", R"(<path d="M7 7l10 10M17 7L7 17"/>)"},
        {"menu", R"(<path d="M4 7h16M4 12h16M4 17h16"/>)"},
        {"tournament", R"(<path d="M8 4h8v5a4 4 0 0 1-8 0z"/><path d="M8 6H5a3 3 0 0 0 3 4M16 6h3a3 3 0 0 1-3 4M12 13v3.5M8.5 20h7M9.5 20l.5-3.5h4l.5 3.5"/>)"},
        {"fontUp", R"(<path d="M3 18l5-12 5 12M4.7 14h6.6M17 8v8M13 12h8"/>)"},
        {"fontDown", R"(<path d="M3 18l5-12 5 12M4.7 14h6.6M14 12h7"/>)"},
        {"star", R"(<path d="M12 3.5l2.6 5.3 5.9.9-4.3 4.1 1 5.8L12 16.9l-5.2 2.7 1-5.8-4.3-4.1 5.9-.9z"/>)"},
        {"book", R"(<path d="M12 6.5C10.3 5.2 7.8 4.5 4 4.5v13c3.8 0 6.3.7 8 2 1.7-1.3 4.2-2 8-2v-13c-3.8 0-6.3.7-8 2zM12 6.5v13"/>)"},
        {"list", R"(<path d="M9 6h11M9 12h11M9 18h11M4.5 6h.01M4.5 12h.01M4.5 18h.01"/>)"},
        {"grid", R"(<rect x="4" y="4" width="6.5" height="6.5" rx="1.2"/><rect x="13.5" y="4" width="6.5" height="6.5" rx="1.2"/><rect x="4" y="13.5" width="6.5" height="6.5" rx="1.2"/><rect x="13.5" y="13.5" width="6.5" height="6.5" rx="1.2"/>)"},
        {"layout", R"(<rect x="3.5" y="4.5" width="17" height="15" rx="2"/><path d="M14 4.5v15M14 12h6.5"/>)"},
    };
    return paths;
}

QPixmap renderIcon(const QString& body, const QColor& color, int size, qreal dpr)
{
    QString svg = QStringLiteral(
                      "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
                      "stroke='currentColor' stroke-width='1.7' stroke-linecap='round' "
                      "stroke-linejoin='round' color='%1'>%2</svg>")
                      .arg(color.name(), body);
    svg.replace(QLatin1String("currentColor"), color.name());
    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pm(QSize(size, size) * dpr);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p, QRectF(0, 0, size, size));
    return pm;
}

} // namespace

QIcon icon(const QString& name)
{
    static QHash<QString, QIcon> cache;
    if (auto it = cache.find(name); it != cache.end()) return *it;

    const QString body = iconPaths().value(name);
    QIcon ic;
    if (!body.isEmpty()) {
        for (int size : {16, 18, 20, 24, 32}) {
            for (qreal dpr : {1.0, 2.0}) {
                ic.addPixmap(renderIcon(body, TextDim, size, dpr), QIcon::Normal, QIcon::Off);
                ic.addPixmap(renderIcon(body, Text, size, dpr), QIcon::Active, QIcon::Off);
                ic.addPixmap(renderIcon(body, TextFaint, size, dpr), QIcon::Disabled, QIcon::Off);
                ic.addPixmap(renderIcon(body, Accent, size, dpr), QIcon::Normal, QIcon::On);
                ic.addPixmap(renderIcon(body, Accent, size, dpr), QIcon::Active, QIcon::On);
            }
        }
    }
    cache.insert(name, ic);
    return ic;
}

QIcon icon(const QString& name, const QColor& color)
{
    static QHash<QString, QIcon> cache;
    const QString key = name + color.name();
    if (auto it = cache.find(key); it != cache.end()) return *it;
    const QString body = iconPaths().value(name);
    QIcon ic;
    if (!body.isEmpty())
        for (int size : {16, 18, 20, 24, 32})
            for (qreal dpr : {1.0, 2.0})
                for (QIcon::Mode mode : {QIcon::Normal, QIcon::Active, QIcon::Disabled, QIcon::Selected}) ic.addPixmap(renderIcon(body, color, size, dpr), mode, QIcon::Off);
    cache.insert(key, ic);
    return ic;
}

QIcon drawnIcon(const QString& body)
{
    QIcon ic;
    for (int size : {16, 32, 48})
        for (qreal dpr : {1.0, 2.0}) {
            ic.addPixmap(renderIcon(body, TextDim, size, dpr), QIcon::Normal, QIcon::Off);
            ic.addPixmap(renderIcon(body, Text, size, dpr), QIcon::Active, QIcon::Off);
            ic.addPixmap(renderIcon(body, Accent, size, dpr), QIcon::Selected, QIcon::Off);
            ic.addPixmap(renderIcon(body, TextFaint, size, dpr), QIcon::Disabled, QIcon::Off);
        }
    return ic;
}

QString dockStyleSheet() { return withTokens(kDockStyle); }

QPixmap logo(int size, qreal dpr)
{
    // Three-headed hydra (resources/hydra64.svg, drawn by tools/logo/make_logo.py); below 48 px the
    // central head alone, the three would blur.
    QSvgRenderer renderer{size < 48 ? QStringLiteral(":/style/hydra64-small.svg") : QStringLiteral(":/style/hydra64.svg")};
    QPixmap pm(QSize(size, size) * dpr);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p, QRectF(0, 0, size, size));
    return pm;
}

QPixmap logoBanner(const QSize& size, qreal dpr)
{
    // Close-up of the three heads of the logo (eyes and jaws), without the board nor any text.
    QSvgRenderer renderer{QStringLiteral(":/style/hydra64.svg")};
    const qreal w = 440, h = w * size.height() / size.width();
    renderer.setViewBox(QRectF(36, 200 - h / 2, w, h)); // in the 512 × 512 drawing: centred between the eyes and the fangs
    renderer.setAspectRatioMode(Qt::KeepAspectRatioByExpanding);
    QPixmap pm(size * dpr);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF frame = QRectF(QPointF(0, 0), QSizeF(size)).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(frame, 6, 6);
    p.setClipPath(clip);
    p.fillRect(frame, QColor(0x0b, 0x0d, 0x12));
    renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    p.setClipping(false);
    p.setPen(QPen(QColor(0xd3, 0x16, 0x2a), 1));
    p.drawRoundedRect(frame, 6, 6);
    return pm;
}

QIcon logoIcon()
{
    QIcon ic;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) ic.addPixmap(logo(size, 1.0));
    return ic;
}

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette pal;
    pal.setColor(QPalette::Window, Window);
    pal.setColor(QPalette::WindowText, Text);
    pal.setColor(QPalette::Base, Panel);
    pal.setColor(QPalette::AlternateBase, PanelAlt);
    pal.setColor(QPalette::Text, Text);
    pal.setColor(QPalette::Button, Raised);
    pal.setColor(QPalette::ButtonText, Text);
    pal.setColor(QPalette::Highlight, Accent);
    pal.setColor(QPalette::HighlightedText, QColor(0x14, 0x14, 0x16));
    pal.setColor(QPalette::ToolTipBase, PanelAlt);
    pal.setColor(QPalette::ToolTipText, Text);
    pal.setColor(QPalette::PlaceholderText, TextFaint);
    pal.setColor(QPalette::Link, Text);
    pal.setColor(QPalette::Mid, Border);
    pal.setColor(QPalette::Dark, Border);
    pal.setColor(QPalette::Shadow, Border);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, TextFaint);
    app.setPalette(pal);

    QFont f;
    f.setFamilies({QStringLiteral("Inter"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")});
    f.setPixelSize(12);
    f.setHintingPreference(QFont::PreferNoHinting);
    app.setFont(f);

    app.setStyleSheet(styleSheet());
}

} // namespace theme

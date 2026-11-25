#!/usr/bin/env python3
"""Rebuilds the README.html notices of the databases/ folder (and that of resources/eco-opening/).

    python3 tools/databases/make_readmes.py

Each notice is a standalone page, in French and English (EN / FR buttons at the top right, choice
remembered as on the pages exported by Hydra64), in the application's presentation: style sheet
tools/databases/readme.css, logo resources/hydra64.svg. The text is here; after a change, run
this script again.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
CSS = open(os.path.join(HERE, "readme.css"), encoding="utf-8").read()
LOGO = open(os.path.join(ROOT, "resources", "hydra64.svg"), encoding="utf-8").read().strip()
UPDATED = ("25 novembre 2026", "25 November 2026")

SCRIPT = """
(function () {
  var lang = 'en';
  try { var saved = localStorage.getItem('hydra64-lang'); if (saved === 'en' || saved === 'fr') lang = saved; } catch (e) {}
  function apply() {
    document.documentElement.lang = lang;
    document.title = document.body.getAttribute('data-title-' + lang);
    var buttons = document.querySelectorAll('#lang button');
    for (var i = 0; i < buttons.length; ++i) buttons[i].className = buttons[i].getAttribute('data-lang') === lang ? 'on' : '';
  }
  document.getElementById('lang').addEventListener('click', function (e) {
    var l = e.target.getAttribute('data-lang');
    if (!l) return;
    lang = l;
    try { localStorage.setItem('hydra64-lang', lang); } catch (err) {}
    apply();
  });
  apply();
})();
"""


def L(fr, en):
    """Text in both languages, inside an element."""
    return f'<span class="fr">{fr}</span><span class="en">{en}</span>'


def P(fr, en, cls=""):
    c = f" {cls}" if cls else ""
    return f'<p class="fr{c}">{fr}</p>\n<p class="en{c}">{en}</p>'


def section(fr, en, body):
    return f"<section>\n<h2>{L(fr, en)}</h2>\n{body}\n</section>"


def chips(*items):
    out = []
    for item in items:
        fr, en = item[0], item[1]
        cls = f" {item[2]}" if len(item) > 2 else ""
        out.append(f'<span class="chip{cls}">{L(fr, en)}</span>')
    return '<div class="chips">' + "".join(out) + "</div>"


def card(name, chip_html, fr, en, extra=""):
    return (f'<div class="card file-card">\n<div class="file-head"><span class="file-name">{name}</span>{chip_html}</div>\n'
            f"{P(fr, en)}\n{extra}</div>")


def table(head, rows):
    h = "".join(f'<th{" class=num" if num else ""}>{L(fr, en)}</th>' for fr, en, num in head)
    body = []
    for row in rows:
        cells = []
        for (fr_en, (_, _, num)), first in zip(zip(row, head), [True] + [False] * len(row)):
            cls = ' class="file"' if first else (' class="num"' if num else "")
            text = L(*fr_en) if isinstance(fr_en, tuple) else fr_en
            cells.append(f"<td{cls}>{text}</td>")
        body.append("<tr>" + "".join(cells) + "</tr>")
    return f'<div class="card" style="padding:6px 8px"><table><thead><tr>{h}</tr></thead><tbody>\n' + "\n".join(body) + "\n</tbody></table></div>"


def tips(*items):
    return '<ul class="tips">\n' + "\n".join(f"<li><div>{L(fr, en)}</div></li>" for fr, en in items) + "\n</ul>"


def origin(*items):
    return '<dl class="card origin">\n' + "\n".join(f"<dt>{name}</dt><dd>{L(fr, en)}</dd>" for name, fr, en in items) + "\n</dl>"


def page(path, brand, title, accent, lead, sections, where):
    html = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title[1]} {accent[1]} — Hydra64</title>
<style>
{CSS}</style>
</head>
<body data-title-fr="{title[0]} {accent[0]} — Hydra64" data-title-en="{title[1]} {accent[1]} — Hydra64">

<header>
<div class="wrap">
{LOGO}
<div>
<div class="brand">{L("Hydra64 · " + brand[0], "Hydra64 · " + brand[1])}</div>
<h1>{L(f"{title[0]} <span>{accent[0]}</span>", f"{title[1]} <span>{accent[1]}</span>")}</h1>
{P(lead[0], lead[1], "lead")}
</div>
<div id="lang"><button data-lang="en">EN</button><button data-lang="fr">FR</button></div>
</div>
</header>

<main class="wrap">

{chr(10).join(sections)}

<div class="board-strip">{"<i></i>" * 16}</div>
<footer><span>Hydra64 · {where}</span><span>{L("Mis à jour le " + UPDATED[0], "Updated " + UPDATED[1])}</span></footer>

</main>
<script>{SCRIPT}</script>
</body>
</html>
"""
    full = os.path.join(ROOT, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    open(full, "w", encoding="utf-8").write(html)
    print(path)


ONE_FILE = (
    "Toutes les bases sont au format <strong>Hydra64 en un seul fichier</strong> (<code>.hy64</code>) : parties, noms, index et "
    "index des positions réunis. Elles s'ouvrent dans la page Bases (<strong>Fichier → Ouvrir</strong>, Ctrl+O) et se convertissent "
    "en PGN par <strong>Fichier → Convertir…</strong>",
    "Every database uses the <strong>single-file Hydra64 format</strong> (<code>.hy64</code>): games, names, index and position "
    "index together. Open them from the Databases page (<strong>File → Open</strong>, Ctrl+O); <strong>File → Convert…</strong> "
    "turns any of them into PGN.",
)

# --------------------------------------------------------------------------------------------- databases/
page(
    "databases/README.html",
    ("Bases livrées", "Bundled databases"),
    ("Bases de données", "Databases"), ("livrées avec Hydra64", "shipped with Hydra64"),
    ("Quatre dossiers de bases prêtes à l'emploi : un échantillon de parties de maîtres, des exercices d'entraînement, des "
     "ouvertures pour les tournois de moteurs et un test tactique pour les moteurs.",
     "Four folders of ready-to-use databases: a sample of master games, training exercises, openings for engine tournaments "
     "and a tactical test suite for engines."),
    [
        section("Les dossiers", "Folders",
                '<div class="folders">\n'
                + "\n".join(
                    f'<a class="card" href="{folder}/README.html"><span class="file-name">{folder}/</span>{chips(*c)}\n{P(fr, en, "dim")}</a>'
                    for folder, c, fr, en in [
                        ("games", [("1 base", "1 database"), ("1 000 parties", "1,000 games")],
                         "Un échantillon de parties de maîtres, de 1834 à 2024, pour découvrir le logiciel.",
                         "A sample of master games, 1834 to 2024, to explore the program."),
                        ("training", [("8 bases", "8 databases"), ("3 565 exercices", "3,565 exercises")],
                         "Exercices pour l'Entraînement : trouver le meilleur coup, mater en 1 à 5 coups.",
                         "Exercises for Training: find the best move, mate in 1 to 5."),
                        ("tournament-openings", [("8 bases", "8 databases"), ("102 118 ouvertures", "102,118 openings")],
                         "Positions de départ pour les tournois de moteurs.",
                         "Starting positions for engine tournaments."),
                        ("engine-tests", [("1 base", "1 database"), ("210 positions", "210 positions")],
                         "Positions tactiques pour le Test de solutions.",
                         "Tactical positions for the Solution test."),
                    ])
                + "\n</div>"),
        section("Format", "Format", P(*ONE_FILE)
                + "\n" + P("Ce dossier est copié à côté du programme : c'est cette copie que l'application lit. Rien n'empêche de "
                           "modifier ces bases (ajouter, annoter, supprimer des parties) ; pour retrouver les originales, "
                           "reprenez-les dans l'archive du logiciel.",
                           "This folder is copied next to the program, and that copy is the one the application reads. You are free "
                           "to edit these databases (add, annotate or delete games); to get the originals back, take them from the "
                           "program archive again.", "dim")),
        section("Origine et licences", "Sources and licences",
                origin(("games/", "Coups et en-têtes de parties jouées en tournoi, sans commentaires ni variantes, choisies dans la Mega Database de ChessBase.",
                        "Moves and headers of tournament games, without comments or variations, selected from ChessBase's Mega Database."),
                       ("training/", "Base d'exercices de Lichess, domaine public (CC0).", "Lichess puzzle database, public domain (CC0)."),
                       ("tournament-openings/", "Stefan Pohl (UHO), livres de test de Stockfish (CC0), positions de John Nunn.",
                        "Stefan Pohl (UHO), Stockfish test books (CC0), John Nunn's positions."),
                       ("engine-tests/", "Positions de l'<em>Encyclopedia of Chess Middlegames</em> (Chess Informant), base Marathon de Fritz (ChessBase).",
                        "Positions from the <em>Encyclopedia of Chess Middlegames</em> (Chess Informant), Marathon database of Fritz (ChessBase)."))
                + "\n" + P("Le détail est dans la notice de chaque dossier.", "Details are in each folder's notice.", "dim")),
    ],
    "databases",
)

# --------------------------------------------------------------------------------------------- games/
PERIODS = [("1834 – 1899", 110), ("1900 – 1929", 120), ("1930 – 1949", 90), ("1950 – 1969", 130),
           ("1970 – 1984", 120), ("1985 – 1999", 140), ("2000 – 2012", 140), ("2013 – 2024", 150)]
page(
    "databases/games/README.html",
    ("Parties", "Games"),
    ("Parties de maîtres", "Master games"), ("un échantillon de 1 000 parties", "a 1,000-game sample"),
    ("Mille parties classiques entre les meilleurs joueurs de leur temps, de La Bourdonnais – McDonnell (1834) aux championnats "
     "du monde récents : de quoi essayer la liste, les recherches, l'arbre de la base et l'analyse sans rien télécharger.",
     "A thousand classical games between the best players of their time, from La Bourdonnais – McDonnell (1834) to recent world "
     "championships: enough to try the game list, searches, the database tree and analysis without downloading anything."),
    [
        section("Le fichier", "The file",
                card("Masters_1000.hy64", chips(("1 000 parties", "1,000 games"), ("1834 – 2024", "1834 – 2024"),
                                               ("sans commentaires", "no comments"), ("index des positions", "position index", "moves")),
                     "854 parties décisives et 146 nulles, 225 joueurs. Chaque partie a son tournoi, son lieu, sa date, sa ronde, son "
                     "code ECO et, depuis 1970, le classement Elo des deux joueurs. L'index des positions est inclus : "
                     "<strong>Base → Arbre de la base</strong> et la recherche de position fonctionnent tout de suite.",
                     "854 decisive games and 146 draws, 225 players. Every game has its event, site, date, round and ECO code and, "
                     "from 1970 on, both players' Elo ratings. The position index is included, so <strong>Database → Database "
                     "tree</strong> and position search work straight away.")),
        section("Pour commencer", "Getting started",
                tips(("<strong>Fichier → Ouvrir</strong> (Ctrl+O), puis double-clic sur une partie pour la voir sur l'échiquier.",
                      "<strong>File → Open</strong> (Ctrl+O), then double-click a game to see it on the board."),
                     ("Tapez un nom dans la recherche au-dessus de la liste (« kasparov », « capablanca »…) : elle suit la frappe.",
                      "Type a name in the search box above the list (“kasparov”, “capablanca”…): it searches as you type."),
                     ("Un clic sur un en-tête de colonne trie la base ; <strong>Envoyer dans l'analyse</strong> copie la partie "
                      "affichée dans la page Analyse.",
                      "Click a column header to sort; <strong>Send to analysis</strong> copies the displayed game to the Analysis "
                      "page."))),
        section("Le choix des parties", "How the games were chosen",
                P("Le choix est fait par un programme (<code>tools/sample-games/select.cpp</code>) et donne les mêmes parties à "
                  "chaque passage. Il retient des parties classiques entières, de 20 à 120 coups, depuis la position initiale ; "
                  "ni blitz, ni parties rapides, ni simultanées, ni parties par correspondance ou en ligne.",
                  "A program makes the selection (<code>tools/sample-games/select.cpp</code>) and returns the same games on every "
                  "run. It keeps complete classical games of 20 to 120 moves from the initial position; no blitz, rapid, "
                  "simultaneous, correspondence or online games.")
                + "\n" + tips(
                    ("<strong>Avant 1970</strong> : les deux joueurs sont de grands maîtres de leur époque (Anderssen, Morphy, "
                     "Steinitz, Lasker, Capablanca, Alekhine, Botvinnik, Tal, Fischer… une soixantaine de noms).",
                     "<strong>Before 1970</strong>: both players are leading masters of their era (Anderssen, Morphy, Steinitz, "
                     "Lasker, Capablanca, Alekhine, Botvinnik, Tal, Fischer… about sixty names)."),
                    ("<strong>Depuis 1970</strong> : les deux joueurs ont un classement Elo élevé pour l'époque (2580 au moins "
                     "jusqu'en 1984, 2740 au moins depuis 2013).",
                     "<strong>From 1970</strong>: both players have a high Elo rating for the period (at least 2580 until 1984, at "
                     "least 2740 since 2013)."),
                    ("Les championnats du monde passent d'abord, les parties décisives sont un peu favorisées, et la variété est "
                     "imposée : au plus 4 parties par paire de joueurs, 6 par code ECO et 40 par joueur dans chaque époque.",
                     "World championships come first, decisive games are slightly favoured, and variety is enforced: at most 4 games "
                     "per pair of players, 6 per ECO code and 40 per player in each period."))
                + "\n" + table([("Époque", "Period", False), ("Parties", "Games", True)],
                               [(p, f"{n}") for p, n in PERIODS])),
        section("Origine", "Source",
                origin(("Masters_1000.hy64",
                        "Parties jouées en public, relevées dans la Mega Database de ChessBase. Seuls les coups et l'en-tête de chaque "
                        "partie sont repris : aucun commentaire, aucune variante, aucun symbole d'annotation.",
                        "Publicly played games, taken from ChessBase's Mega Database. Only the moves and the header of each game are "
                        "kept: no comments, no variations, no annotation symbols."))),
    ],
    "databases/games",
)

# --------------------------------------------------------------------------------------------- training/
page(
    "databases/training/README.html",
    ("Entraînement", "Training"),
    ("Bases d'entraînement", "Training databases"), ("meilleur coup et mats", "best move and mates"),
    ("Huit bases d'exercices tirés de la base publique de Lichess, une par niveau : trouver le meilleur coup (trois niveaux) et "
     "mater en 1 à 5 coups. L'Entraînement les propose d'office.",
     "Eight databases of exercises from the public Lichess puzzle database, one per level: find the best move (three levels) and "
     "mate in 1 to 5. Training offers them out of the box."),
    [
        section("Utilisation", "How to use",
                P("<strong>Partie → Entraînement…</strong> : choisissez « Trouver le meilleur coup » ou « Mats en N », puis un recueil "
                  "« Intégré : … » dans la liste <strong>Recueil</strong>. Chaque choix correspond à une de ces bases. Les positions "
                  "arrivent dans un ordre tiré au hasard ; avec la révision espacée, celles que vous ratez reviennent plus souvent.",
                  "<strong>Game → Training…</strong>: choose “Find the best move” or “Mates in N”, then a “Built-in: …” collection in "
                  "the <strong>Collection</strong> list. Each choice maps to one of these databases. Positions come in random order; "
                  "with spaced repetition, the ones you miss come back more often.")
                + "\n" + P("Les bases s'ouvrent aussi dans la page Bases (Ctrl+O) : on y trie par classement (colonne Elo), on y "
                           "regarde une solution.",
                           "The databases also open in the Databases page (Ctrl+O), where you can sort by rating (Elo column) and "
                           "look at a solution.", "dim")),
        section("Les fichiers", "The files",
                '<div class="files">\n'
                + card("best_moves/", chips(("1 800 exercices", "1,800 exercises"), ("3 bases", "3 databases"), ("solutions", "solutions", "moves")),
                       "Trois bases de 600 exercices, d'après le classement Lichess de l'exercice : <code>best_move_easy.hy64</code> "
                       "(moins de 1500), <code>best_move_medium.hy64</code> (1500 à 1999) et <code>best_move_hard.hy64</code> (2000 et "
                       "plus). Aucun mat parmi eux : il s'agit de gagner du matériel ou d'obtenir un avantage décisif.",
                       "Three databases of 600 exercises, by the puzzle's Lichess rating: <code>best_move_easy.hy64</code> (under "
                       "1500), <code>best_move_medium.hy64</code> (1500 to 1999) and <code>best_move_hard.hy64</code> (2000 and over). "
                       "None is a mate: the goal is to win material or reach a decisive advantage.")
                + "\n"
                + card("mates/", chips(("1 765 exercices", "1,765 exercises"), ("5 bases", "5 databases"), ("mats vérifiés", "verified mates", "moves")),
                       "<code>mate_in_1.hy64</code> (300), <code>mate_in_2.hy64</code> (500), <code>mate_in_3.hy64</code> (500), "
                       "<code>mate_in_4.hy64</code> (300) et <code>mate_in_5.hy64</code> (165). Chaque solution a été rejouée : elle "
                       "mate exactement au coup annoncé. Pendant l'exercice, Stockfish défend au mieux, et toute suite qui mate dans "
                       "le nombre de coups est acceptée.",
                       "<code>mate_in_1.hy64</code> (300), <code>mate_in_2.hy64</code> (500), <code>mate_in_3.hy64</code> (500), "
                       "<code>mate_in_4.hy64</code> (300) and <code>mate_in_5.hy64</code> (165). Every solution was replayed: it mates "
                       "exactly on the announced move. During the exercise Stockfish defends as well as possible, and any line that "
                       "mates within the number of moves is accepted.")
                + "\n</div>"),
        section("Contenu d'un exercice", "What an exercise holds",
                table([("Champ", "Field", False), ("Contenu", "Content", False)],
                      [("FEN", ("La position à résoudre (le coup de l'adversaire qui la précède sur Lichess est déjà joué).",
                                "The position to solve (the opponent's move that precedes it on Lichess is already played).")),
                       (("Coups", "Moves"), ("La solution en premier coup, puis la suite complète, montrée par « Solution ».",
                                             "The solution as first move, then the full line, shown by “Solution”.")),
                       (("Commentaire", "Comment"), ("<code>dm N</code> pour un mat en N (format des opérations EPD).",
                                                     "<code>dm N</code> for a mate in N (EPD operation format).")),
                       (("Tournoi", "Event"), ("La catégorie, en français : « Meilleur coup, facile », « Mat en 3 »…",
                                               "The category, in French: “Meilleur coup, facile”, “Mat en 3”…")),
                       (("Blancs · Elo", "White · Elo"), ("« Lichess &lt;id&gt; » et le classement de l'exercice ; le Lieu est le lien "
                                                          "<code>lichess.org/training/&lt;id&gt;</code>.",
                                                          "“Lichess &lt;id&gt;” and the puzzle's rating; the Site is the link "
                                                          "<code>lichess.org/training/&lt;id&gt;</code>."))])),
        section("Origine", "Source",
                origin((L("Base d'exercices de Lichess", "Lichess puzzle database"),
                        "<a href=\"https://database.lichess.org/#puzzles\">database.lichess.org</a>, placée dans le domaine public "
                        "(Creative Commons CC0). Sélection : exercices joués au moins 1 000 fois, popularité d'au moins 85 %, écart de "
                        "classement d'au plus 80, tirage fixe ; les « mat en 5 » de Lichess qui sont en réalité plus longs sont écartés.",
                        "<a href=\"https://database.lichess.org/#puzzles\">database.lichess.org</a>, released into the public domain "
                        "(Creative Commons CC0). Selection: puzzles played at least 1,000 times, popularity of at least 85%, rating "
                        "deviation of at most 80, fixed draw; Lichess “mate in 5” puzzles that are actually longer are left out."),
                       (L("Fabrication", "How they are built"),
                        "<code>tools/lichess-puzzles/</code> : <code>select.py</code> (sélection), <code>convert.cpp</code> (positions "
                        "et solutions vérifiées, en EPD), <code>to_pgn.py</code> (parties PGN), puis <code>hydra64-convert</code>.",
                        "<code>tools/lichess-puzzles/</code>: <code>select.py</code> (selection), <code>convert.cpp</code> (verified "
                        "positions and solutions, as EPD), <code>to_pgn.py</code> (PGN games), then <code>hydra64-convert</code>."))),
    ],
    "databases/training",
)

# --------------------------------------------------------------------------------------------- tournament-openings/
OPENINGS = [
    ("UHO_2022_8mvs_+110_+119.hy64", "8 939", "8,939", ("8 coups", "8 moves")),
    ("UHO_2022_8mvs_big_+100_+129.hy64", "27 073", "27,073", ("8 coups", "8 moves")),
    ("UHO_2022_6mvs_+110_+119.hy64", "3 317", "3,317", ("6 coups", "6 moves")),
    ("2moves_v2.hy64", "12 092", "12,092", ("2 coups", "2 moves")),
    ("8moves_v3.hy64", "34 700", "34,700", ("8 coups", "8 moves")),
    ("Drawkiller_balanced_big.hy64", "15 962", "15,962", ("aucun", "none")),
    ("Nunn_10.hy64", "10", "10", ("10 à 13 coups", "10 to 13 moves")),
    ("Nunn2_25.hy64", "25", "25", ("8 à 13 coups", "8 to 13 moves")),
]
WITH = ("avec coups", "with moves", "moves")
page(
    "databases/tournament-openings/README.html",
    ("Tournoi de moteurs", "Engine tournament"),
    ("Positions de départ", "Starting positions"), ("pour les tournois", "for tournaments"),
    ("Des recueils d'ouvertures pour que chaque appariement parte d'une position différente, la même pour les deux moteurs, "
     "couleurs inversées.",
     "Opening collections so that every pairing starts from a different position, the same for both engines, with colours "
     "reversed."),
    [
        section("Pourquoi", "Why",
                P("Deux moteurs déterministes qui partent toujours de la position initiale rejouent souvent les mêmes parties : le "
                  "résultat ne mesure presque rien. En faisant partir chaque appariement d'une ouverture différente, jouée une fois "
                  "avec chaque couleur (<strong>aller-retour</strong>), on obtient des parties variées et une comparaison "
                  "<strong>équitable</strong>.",
                  "Two deterministic engines that always start from the initial position often replay the same games, and the "
                  "result measures almost nothing. Starting each pairing from a different opening, played once with each colour "
                  "(<strong>reversed colours</strong>), gives varied games and a <strong>fair</strong> comparison.")
                + "\n" + P("Dans la page <strong>Tournoi</strong> : choisissez « Positions de départ », puis un fichier de ce dossier, "
                           "et lancez le tournoi.",
                           "On the <strong>Tournament</strong> page: choose “Starting positions”, pick a file from this folder, and "
                           "start the tournament.", "note")),
        section("Coups de l'ouverture", "Opening moves",
                P("La plupart des recueils contiennent les coups de l'ouverture. Hydra64 les joue, les affiche dans la notation "
                  "et les enregistre dans la base du tournoi, suivis du repère <span style=\"color:var(--comment)\">Fin des coups de "
                  "la base</span>. Les moteurs jouent ensuite.",
                  "Most collections hold the opening moves. Hydra64 plays them, shows them in the notation and stores them in the "
                  "tournament database, followed by the marker <span style=\"color:var(--comment)\">Fin des coups de la base</span> "
                  "(end of the book moves). The engines play from there.")
                + "\n" + P("<code>Drawkiller_balanced_big.hy64</code> ne contient que des positions, sans les coups qui y mènent : la "
                           "partie commence directement à la position.",
                           "<code>Drawkiller_balanced_big.hy64</code> holds positions only, without the moves leading to them: the "
                           "game starts directly from the position.", "dim")),
        section("Choisir un recueil", "Choosing a collection",
                '<div class="rule"><span class="big">45–60 %</span><div style="flex:1">'
                + L("<strong>de parties nulles</strong> : c'est la zone à viser.", "<strong>draws</strong>: that is the range to aim for.")
                + '<div class="meter"></div><div class="meter-legend"><span>'
                + L("trop peu → recueil plus équilibré", "too few → a more balanced collection") + "</span><span>"
                + L("trop → recueil plus déséquilibré (UHO)", "too many → a more unbalanced collection (UHO)") + "</span></div></div></div>\n"
                + table([("Recueil", "Collection", False), ("Situation", "Situation", False), ("Pourquoi", "Why", False)],
                        [("UHO_2022_8mvs_+110_+119", ("Moteurs forts entre eux (Stockfish…)", "Strong engines against each other (Stockfish…)"),
                          ("Avantage blanc net : beaucoup moins de nulles.", "Clear White advantage: far fewer draws.")),
                         ("UHO_2022_8mvs_big_+100_+129", ("Idem, avec plus de variété", "The same, with more variety"),
                          ("Trois fois plus d'ouvertures.", "Three times as many openings.")),
                         ("8moves_v3", ("Moteurs moyens, tournoi court", "Mid-strength engines, short tournament"),
                          ("Ouvertures variées et équilibrées.", "Varied, balanced openings.")),
                         ("2moves_v2", ("Moteurs très faibles (Shuffle…)", "Very weak engines (Shuffle…)"),
                          ("2 coups seulement : rien n'est décidé d'avance.", "Only 2 moves: nothing is decided in advance.")),
                         ("Drawkiller_balanced_big", ("Tester le milieu de partie", "Testing the middlegame"),
                          ("Positions tendues, sans coups.", "Tense positions, no moves.")),
                         ("Nunn_10 · Nunn2_25", ("Match court et classique (« match Nunn »)", "Short classic match (“Nunn match”)"),
                          ("10 ou 25 ouvertures choisies par John Nunn.", "10 or 25 openings chosen by John Nunn."))])),
        section("Les fichiers", "The files",
                table([("Fichier", "File", False), ("Ouvertures", "Openings", True), ("Coups", "Moves", False)],
                      [(name, (fr, en), moves) for name, fr, en, moves in OPENINGS])
                + '\n<div class="files" style="margin-top:14px">\n'
                + card("UHO_2022_8mvs_+110_+119.hy64", chips(("8 939 ouvertures", "8,939 openings"), WITH, ("recommandé", "recommended", "rec")),
                       "<strong>Unbalanced Human Openings 2022</strong> de Stefan Pohl. Ouvertures réellement jouées par des joueurs de "
                       "plus de 2300 Elo, coupées après 8 coups, les deux dames encore sur l'échiquier, et retenues parce que les Blancs "
                       "y ont un avantage net mais pas décisif (Komodo Dragon 2.6 : <strong>+1,10 à +1,19</strong>). C'est le jeu que "
                       "l'auteur recommande pour commencer. Avec l'aller-retour, chaque moteur a tour à tour le « bon » côté : "
                       "l'avantage s'annule, seul l'écart de force ressort. Les en-têtes d'origine sont conservés ; les ouvertures des "
                       "joueurs les plus forts sont en tête.",
                       "Stefan Pohl's <strong>Unbalanced Human Openings 2022</strong>. Openings actually played by players rated over "
                       "2300, cut after 8 moves with both queens still on the board, and kept because White has a clear but not "
                       "decisive advantage (Komodo Dragon 2.6: <strong>+1.10 to +1.19</strong>). This is the set its author recommends "
                       "to start with. With reversed colours each engine gets the “good” side in turn: the advantage cancels out and "
                       "only the difference in strength shows. Original headers are kept; openings of the strongest players come first.")
                + "\n" + card("UHO_2022_8mvs_big_+100_+129.hy64", chips(("27 073 ouvertures", "27,073 openings"), WITH),
                              "Même recueil sur un intervalle d'évaluation plus large (<strong>+1,00 à +1,29</strong>) : trois fois plus "
                              "d'ouvertures, pour les longs tournois.",
                              "The same collection over a wider evaluation range (<strong>+1.00 to +1.29</strong>): three times as many "
                              "openings, for long tournaments.")
                + "\n" + card("UHO_2022_6mvs_+110_+119.hy64", chips(("3 317 ouvertures", "3,317 openings"), WITH),
                              "Version UHO coupée après 6 coups : les moteurs jouent davantage de l'ouverture eux-mêmes.",
                              "The UHO set cut after 6 moves: engines play more of the opening themselves.")
                + "\n" + card("8moves_v3.hy64", chips(("34 700 ouvertures", "34,700 openings"), WITH),
                              "Le recueil « classique » historique des tests de Stockfish : ouvertures variées et équilibrées. Plus de "
                              "nulles entre moteurs forts, mais très bon pour des moteurs de force moyenne.",
                              "The historical “classic” book of Stockfish testing: varied, balanced openings. More draws between strong "
                              "engines, but very good for mid-strength ones.")
                + "\n" + card("2moves_v2.hy64", chips(("12 092 ouvertures", "12,092 openings"), WITH),
                              "Ouvertures très courtes et équilibrées : les moteurs jouent eux-mêmes presque toute l'ouverture. Utile pour "
                              "tester leur jeu d'ouverture, et pour les moteurs faibles.",
                              "Very short, balanced openings: engines play nearly the whole opening themselves. Useful to test their "
                              "opening play, and for weak engines.")
                + "\n" + card("Drawkiller_balanced_big.hy64", chips(("15 962 positions", "15,962 positions"), ("sans coups", "no moves", "nomoves")),
                              "Positions « anti-nulles » de milieu de partie construites par Stefan Pohl (placement des pièces modifié, "
                              "sans droits de roque), avec beaucoup de matériel et de tension. Elles ne proviennent d'aucune suite de "
                              "coups : la notation commence à la position.",
                              "“Anti-draw” middlegame positions built by Stefan Pohl (modified piece placement, no castling rights), with "
                              "plenty of material and tension. They do not come from any move sequence: the notation starts at the "
                              "position.")
                + "\n" + card("Nunn_10.hy64", chips(("10 ouvertures", "10 openings"), WITH),
                              "Les positions de <strong>John Nunn</strong>, grand maître anglais : dix débuts équilibrés et typés "
                              "(Sicilienne Sveshnikov, Sozin, Française Winawer, Espagnole fermée, Gambit dame variante d'échange, "
                              "Grünfeld, Ouest-Indienne, Est-Indienne classique, Anglaise…). Le « match Nunn » classique des logiciels "
                              "d'échecs : 20 parties par paire de moteurs avec l'aller-retour. Le nom de chaque ouverture est dans le "
                              "champ « Blancs ».",
                              "The positions of <strong>John Nunn</strong>, English grandmaster: ten balanced, characteristic openings "
                              "(Sveshnikov and Sozin Sicilians, French Winawer, Closed Ruy Lopez, Queen's Gambit Exchange, Grünfeld, "
                              "Queen's Indian, Classical King's Indian, English…). The classic “Nunn match” of chess software: 20 games "
                              "per pair of engines with reversed colours. Each opening's name is in the “White” field.")
                + "\n" + card("Nunn2_25.hy64", chips(("25 ouvertures", "25 openings"), WITH),
                              "La seconde série, plus large : 25 ouvertures de A30 (Anglaise) à E (Indiennes), 50 parties par paire de "
                              "moteurs. Moins de répétitions que Nunn_10 pour un tournoi un peu plus long.",
                              "The second, wider series: 25 openings from A30 (English) to E (Indian defences), 50 games per pair of "
                              "engines. Fewer repeats than Nunn_10 for a slightly longer tournament.")
                + "\n</div>"),
        section("Conseils", "Tips",
                tips(("Gardez <strong>« Aller-retour »</strong> coché : c'est ce qui rend les recueils déséquilibrés (UHO) équitables.",
                      "Keep <strong>“Reversed colours”</strong> ticked: it is what makes unbalanced collections (UHO) fair."),
                     ("<strong>« Aléatoire »</strong> tire les ouvertures au hasard au lieu de prendre les premières du fichier. "
                      "Recommandé avec les gros recueils, sinon tous les tournois commencent par les mêmes ouvertures.",
                      "<strong>“Random”</strong> draws openings at random instead of taking the first ones in the file. Recommended "
                      "with large collections, otherwise every tournament starts with the same openings."),
                     ("Trop de nulles malgré UHO ? L'archive complète de Stefan Pohl propose des jeux plus déséquilibrés, jusqu'à "
                      "<strong>+1,89</strong>.",
                      "Too many draws even with UHO? Stefan Pohl's full archive has more unbalanced sets, up to <strong>+1.89</strong>."),
                     ("Hydra64 accepte aussi vos propres fichiers <strong>EPD</strong>, <strong>FEN</strong> ou <strong>PGN</strong> ; "
                      "un recueil dans un format propriétaire (ChessBase…) se convertit d'abord : <strong>Fichier → Convertir…</strong>",
                      "Hydra64 also accepts your own <strong>EPD</strong>, <strong>FEN</strong> or <strong>PGN</strong> files; a "
                      "collection in a proprietary format (ChessBase…) is converted first: <strong>File → Convert…</strong>"))),
        section("Origine", "Source",
                origin(("UHO_2022_*.hy64",
                        "Stefan Pohl — <a href=\"https://www.sp-cc.de/uho_2022.htm\">sp-cc.de</a>, archive <code>uho_2022.7z</code>. "
                        "Distribué gratuitement pour les tests de moteurs ; son ReadMe ne précise pas de licence.",
                        "Stefan Pohl — <a href=\"https://www.sp-cc.de/uho_2022.htm\">sp-cc.de</a>, archive <code>uho_2022.7z</code>. "
                        "Distributed free of charge for engine testing; its ReadMe states no licence."),
                       ("8moves_v3.hy64 · 2moves_v2.hy64 · Drawkiller_balanced_big.hy64",
                        "Dépôt officiel des livres de test de Stockfish — <a href=\"https://github.com/official-stockfish/books\">"
                        "github.com/official-stockfish/books</a>, licence <strong>CC0 1.0</strong> (domaine public).",
                        "Official Stockfish test books repository — <a href=\"https://github.com/official-stockfish/books\">"
                        "github.com/official-stockfish/books</a>, <strong>CC0 1.0</strong> licence (public domain)."),
                       ("Nunn_10.hy64 · Nunn2_25.hy64",
                        "Positions de John Nunn, d'après les bases <em>nunn</em> et <em>Nunn2</em> livrées avec Fritz (ChessBase). Les "
                        "positions elles-mêmes sont largement publiées ; les fichiers d'origine viennent d'un logiciel commercial.",
                        "John Nunn's positions, from the <em>nunn</em> and <em>Nunn2</em> databases shipped with Fritz (ChessBase). The "
                        "positions themselves are widely published; the original files come from commercial software."))
                + "\n" + P("Les recueils PGN et EPD d'origine ont été convertis tels quels au format Hydra64 : mêmes ouvertures, "
                           "dans le même ordre.",
                           "The original PGN and EPD collections were converted as they are to the Hydra64 format: same openings, "
                           "same order.", "dim")),
    ],
    "databases/tournament-openings",
)

# --------------------------------------------------------------------------------------------- engine-tests/
page(
    "databases/engine-tests/README.html",
    ("Test de solutions", "Solution test"),
    ("Tests de moteurs", "Engine tests"), ("positions tactiques", "tactical positions"),
    ("Un recueil de positions à résoudre : le moteur cherche chaque position pendant un temps fixé, Hydra64 compte celles où il "
     "trouve le coup de la solution.",
     "A collection of positions to solve: the engine searches each position for a fixed time, and Hydra64 counts those where it "
     "finds the solution move."),
    [
        section("Utilisation", "How to use",
                P("<strong>Moteur → Test de solutions…</strong> : choisissez un moteur, un recueil de ce dossier et le temps par "
                  "position, puis <strong>Lancer</strong>. Chaque ligne montre le coup proposé par le moteur pendant sa recherche, puis "
                  "le résultat ; « Trouvé en » est le moment depuis lequel le moteur garde la bonne solution en tête (s'il en change "
                  "ensuite, le temps repart).",
                  "<strong>Engine → Solution test…</strong>: choose an engine, a collection from this folder and the time per "
                  "position, then <strong>Start</strong>. Each row shows the move the engine proposes while searching, then the "
                  "result; “Found in” is the moment from which the engine keeps the right solution on top (if it changes its mind "
                  "later, the time restarts).")
                + "\n" + P("Le score final donne le nombre de positions résolues et le temps moyen de solution. Double-clic sur une "
                           "ligne : la position s'ouvre dans la page Analyse. La fenêtre peut être fermée pendant le test, qui continue.",
                           "The final score gives the number of positions solved and the average solving time. Double-click a row to "
                           "open the position in the Analysis page. The window can be closed while the test keeps running.", "dim")),
        section("Le fichier", "The file",
                card("Tactics_Marathon_210.hy64", chips(("210 positions", "210 positions"), ("solutions", "solutions", "moves")),
                     "Le <strong>Tactics Marathon</strong> : 210 positions de combinaison tirées de l'<em>Encyclopedia of Chess "
                     "Middlegames</em> (le numéro de chaque position, « 946 ECM », est dans le champ Tournoi). La solution est le premier "
                     "coup de la partie ; une variante au premier coup est une autre solution acceptée, ou un piège à éviter si elle "
                     "est marquée « ? ». Certaines positions donnent la suite complète de la combinaison.",
                     "The <strong>Tactics Marathon</strong>: 210 combination positions from the <em>Encyclopedia of Chess "
                     "Middlegames</em> (each position's number, “946 ECM”, is in the Event field). The solution is the first move of "
                     "the game; a variation on the first move is another accepted solution, or a trap to avoid when marked “?”. Some "
                     "positions give the full combination.",
                     P("<strong>Repère</strong> : Stockfish récent, 1 seconde par position, en résout 169 sur 210. Une partie des "
                       "solutions vient d'analyses humaines anciennes qu'un moteur moderne conteste : un score de 100 % n'est pas "
                       "attendu, c'est l'évolution du score entre deux moteurs ou deux réglages qui compte.",
                       "<strong>Benchmark</strong>: a recent Stockfish at 1 second per position solves 169 of 210. Some solutions come "
                       "from old human analysis that a modern engine disputes: a 100% score is not expected, what matters is how the "
                       "score changes between two engines or two settings.", "dim") + "\n")),
        section("Autres recueils", "Other collections",
                tips(("<strong>EPD</strong> : le format habituel des recueils publiés (Win At Chess, Strategic Test Suite, "
                      "Bratko-Kopec…). Hydra64 lit les opérations <code>bm</code> (meilleur coup, plusieurs possibles), "
                      "<code>am</code> (coup à éviter) et <code>id</code> (nom de la position).",
                      "<strong>EPD</strong>: the usual format of published suites (Win At Chess, Strategic Test Suite, Bratko-Kopec…). "
                      "Hydra64 reads the <code>bm</code> (best move, several allowed), <code>am</code> (avoid move) and "
                      "<code>id</code> (position name) operations."),
                     ("<strong>PGN ou base Hydra64</strong> : une partie par position (en-tête FEN), le premier coup est la "
                      "solution, comme pour le Tactics Marathon.",
                      "<strong>PGN or Hydra64 database</strong>: one game per position (FEN header), the first move being the "
                      "solution, as in the Tactics Marathon."),
                     ("Un recueil dans un format propriétaire (base ChessBase…) se convertit d'abord : <strong>Fichier → Convertir…</strong>",
                      "A collection in a proprietary format (ChessBase database…) is converted first: <strong>File → Convert…</strong>"))),
        section("Origine", "Source",
                origin(("Tactics_Marathon_210.hy64",
                        "Base <em>Marathon</em> livrée avec Fritz (ChessBase), convertie au format Hydra64. Positions de "
                        "l'<em>Encyclopedia of Chess Middlegames</em> (Chess Informant). Le fichier d'origine vient d'un logiciel "
                        "commercial.",
                        "The <em>Marathon</em> database shipped with Fritz (ChessBase), converted to the Hydra64 format. Positions "
                        "from the <em>Encyclopedia of Chess Middlegames</em> (Chess Informant). The original file comes from commercial "
                        "software."))),
    ],
    "databases/engine-tests",
)

# --------------------------------------------------------------------------------------------- resources/eco-opening/
page(
    "resources/eco-opening/README.html",
    ("Ouvertures", "Openings"),
    ("Codes ECO", "ECO codes"), ("et noms d'ouvertures", "and opening names"),
    ("La table qui sert à classer les parties et à nommer l'ouverture affichée sous l'échiquier. Elle est compilée dans le "
     "programme : ces fichiers ne sont pas à livrer à part.",
     "The table used to classify games and to name the opening shown under the board. It is compiled into the program: these "
     "files do not need to be shipped separately."),
    [
        section("Les fichiers", "The files",
                card("a.tsv · b.tsv · c.tsv · d.tsv · e.tsv", chips(("5 fichiers", "5 files"), ("texte tabulé", "tab-separated text")),
                     "Un fichier par volume de l'Encyclopédie des ouvertures (A à E). Une ligne par ouverture : code ECO, nom (en "
                     "anglais), coups en notation SAN. Hydra64 retient, pour une partie, l'ouverture la plus profonde reconnue "
                     "dans sa ligne principale.",
                     "One file per volume of the Encyclopaedia of Chess Openings (A to E). One line per opening: ECO code, name (in "
                     "English), moves in SAN. For a game, Hydra64 keeps the deepest opening recognised in its main line.")),
        section("Origine", "Source",
                origin(("lichess-org/chess-openings",
                        "<a href=\"https://github.com/lichess-org/chess-openings\">github.com/lichess-org/chess-openings</a>, domaine "
                        "public (<strong>CC0 1.0</strong>, texte dans <code>COPYING.txt</code>).",
                        "<a href=\"https://github.com/lichess-org/chess-openings\">github.com/lichess-org/chess-openings</a>, public "
                        "domain (<strong>CC0 1.0</strong>, text in <code>COPYING.txt</code>)."))),
    ],
    "resources/eco-opening",
)

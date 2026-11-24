# Improvements inspired by SCID vs PC

Features of SCID vs PC 4.27 (source: `scid_vs_pc-4.27.tgz`, SourceForge) that Hydra64 did not
have yet, sorted by page. SCID vs PC is under the GPL: the ideas are taken, not the code. Effort:
S = a few hours, M = a day, L = several days. `[x]` = done, `[ ]` = to do.

## Databases page

- [x] **Header search** (`search/header.tcl`) — white / black player / both colours, ranges of Elo,
      dates, ECO, result, number of moves, tournament, site, round. Everything is in the `.hy64i`
      index row: near-instant search. (M)
- [x] **Combining filters** (Search → Reset / Negate) — search within the previous result (AND),
      add to the result (OR), invert the filter. (S)
- [x] **Search by material and patterns** (`search/material.tcl`) — number of pieces of each kind
      (ranges), material difference, predefined patterns (isolated pawn, hanging pawns, Maroczy
      centre, fianchettos, opposite castling, rook sacrifice on c3). (L)
- [x] **Position search types** (`search/board.tcl`) — exact (done), same pawns on the same
      squares, pawns on the same files, same material anywhere. (M)
- [x] **Duplicates** (Twin checker, maintenance) — detection according to criteria (same players,
      same moves, same date, same tournament…), duplicates marked deleted (struck through), then
      compaction; pairwise comparison window. (M)
- [x] **Game flags** (`flags.tcl`) — personal marks (endgame, tactics, white / black opening,
      strategy, critical game…) in the 6 free bits of the index: filter, column, colour in the
      list. (S)
- [x] **Configurable columns** (`gamelist.tcl`, right-click on the header) — site, round, number
      of moves, final material, variations / comments present, flags. (S)
- [x] **Game preview** (Browse) — small window with a board to leaf through a game without loading
      it. (M)
- [x] **Random game, game no.…** (Game → Load Random / Load Number). (S)
- [x] **Opening report** (`optable.tcl`) — statistics of the position by period, popularity, most
      frequent players, best games, typical structures; HTML export. (L)
- [x] **Player profile and report** (`pinfo.tcl`, `preport.tcl`) — score, favourite openings by
      colour, opponents, Elo curve. (M)
- [x] **Filter graphs** (filter graph) — distribution by year / Elo of the filter's games. (M)
- [x] **Crosstable of a tournament of the database** (`crosstab.tcl`). (M)
- [x] **Name correction** (name editor, spellcheck) — rename / merge a player, a tournament, a
      site in the whole database. (M)
- [x] **ECO classification** — recompute the ECO code of all the games. (M)
- [x] **Physical sort of the database** (Sort database) — rewrite the database in the order of a
      sort. (S)
- [x] **Export** — filter as EPD / FEN, HTML, LaTeX; list of the games as text. (M)
- [x] **Bookmarks** — find a game / position of a database again. (S)

## Analysis page

- [x] **Deep analysis** (outside SCID: after Aquarium's IDeA and BanksiaGUI's ECA) — persistent
      tree, minimax, alternatives, projects, several engines, notes, exports. (L)

- [x] **Automatic annotation** (`initAnnotation`, `addAnnotation`) — time per move, side, errors
      only (thresholds) or all non-optimal moves, missed mates, best continuation as a variation,
      evaluations, NAGs ?! ? ??. (M)
- [x] **Batch annotation** (Batch annotate) — a selection of games of a database. (S)
- [x] **Book-exit marker** during annotation ("Last book move"). (S)
- [x] **Add an engine's line to the notation** (AddVariation / AddAllVariations). (S)
- [x] **Exclude a move** (`excludeMove`, UCI `searchmoves`). (S)
- [x] **Lock an engine on a position** (LockEngine). (S)
- [x] **Finish the game** (FinishGame) — the engines play on from the position. (M)
- [x] **Find the novelty** (Find Novelty) — first move never played in a reference database
      (`.hy64p` index). (S)
- [x] **Mark the tactical exercises** (`markExercise`) during annotation. (M)
- [x] **Piece tracking** (piece tracker) — map of the squares occupied by a piece. (M)
- [x] **Endgame tablebases** (tablebase window) — Syzygy result of the position. (M)
- [x] **ECO browser**. (M)

## Play page

- [x] **Coach** (CoachIsWatching) — a move losing more than X is reported, takeback offered. (S)
- [x] **Hint** — arrow of the best move on demand. (S)
- [x] **Pondering** (Ponder). (S)
- [x] **Forced opening** (SpecificOpening) — start from a chosen opening (ECO / line). (M)
- [x] **Fixed depth / nodes** as the engine's limit (FixedDepth, Nodes) — fixed depth done, nodes
      not. (S)
- [x] **Opening training with the tree** (`tree::doTraining`) — the tree answers by weighted
      chance, leaving the tree reported. (M)
- [x] **Mates in N** (`tactics.tcl`) — exercises drawn from a database, checked by the engine. (M)
- [x] **Find the best move** (FindBestMove) — you propose, the engine judges. (M)
- [x] **Calculation of variations** (`calvar.tcl`, Stoyko exercise). (M)
- [x] **Tactical game against Phalanx** (`tacgame.tcl`) — realistic weak levels. (M)
- [ ] **Electronic boards** (DGT / Novag, input engine). (L)

## Tournament page

- [x] **Gauntlet** ("First plays others") and carousel order. (S)
- [x] **Automatic adjudication** — draw if |eval| < threshold for N moves, resignation if eval >
      threshold for N moves (not in SCID, present in cutechess / Fritz). (S)
- [x] **Engine evaluations as comments** (TourComments). (S)
- [x] **Time per move / fixed depth** (TourPer). (S)
- [x] **Crosstable** (`crosstab.tcl`). (M)
- [x] **Elo difference ± margin, LOS** (not in SCID, present in cutechess). (S)
- [x] **Engine pondering** (Ponder). (S)
- [x] **Engines' book** (UseBook) — an opening tree for the first moves. (S)
- [x] **Swiss system**. (M)

## Elsewhere

- [ ] **Correspondence / e-mail game** (correspondence, email manager). (L)
- [ ] **Online play on FICS** (`fics.tcl`). (L)
- [x] **Screenshot of the board as an image** (Board screenshot). (S)

---

# Progress log (to resume after an interruption)

Updated after each finished feature. Each feature done: code, test (`core_tests` or `ui_tests`),
README, English translations (`translations/fill_en.py`), then complete `ctest` twice. Interface
tests: `QT_QPA_PLATFORM=offscreen build/ui_tests` (user folder isolated in `build/ui_tests_home`).
A dialog opened later than immediately is tested with `answerDialogWhenShown` (and not
`answerNextDialog`, which only watches for 50 ms).

Paths below are those of the time; since 22 November 2026 the user folder is `~/hydra64/`
(`analyses`, `exercises`…) and the bundled data are in `databases/` and `resources/eco-opening/`.

## Done (where to look in the code)

- Automatic and batch annotation, leaving the book: `src/ui/annotator.*`,
  `MainWindow::annotateGame / annotateGames / bookOptions`.
- Deep analysis (IDeA): `src/deep/` (core), `src/ui/deepanalysis.*`, `deeppanel.*`,
  `deepdialogs.*`; `.hy64deep` projects in `~/hydra64/analyses`.
- Advanced search, combination, invert, whole database: `src/db/headerquery.*`,
  `Database::searchHeaders`, `GameListPanel::searchHeaders / negateFilter / clearFilter`,
  `src/ui/headersearchdialog.*`.
- Flags (6 user bits of the index): `Database::setUserFlags`, `Hy64Source::setUserFlag`, context
  menu of the list, criterion of the advanced search.
- Configurable columns (Site, Round, Moves): `GameListModel::Column`, header of the list
  (right-click), `gameList/hiddenColumns` in the settings; sort
  `hy64db::ColSite/ColRound/ColMoves`.
- Physical sort: `Database::compact(…, order)`, `MainWindow::sortDatabase`.
- Bookmarks: `MainWindow::bookmarks / addBookmark / openBookmark` (setting `bookmarks`).
- Random game / no. N: `GameListPanel::randomRow / rowOfNumber`.
- Duplicates, name correction: `Database::findTwins / nameUsage / renameName`,
  `MainWindow::twinsDialog / handleTwins / renameDialog / renameInDatabase`.
- Engine panel: lock, excluded moves (`searchmoves`), lines to the notation:
  `EnginePanel::setLocked / excludeMove / linesInsertRequested`, `MainWindow::insertEngineLines`.
- Novelty: `MainWindow::findNovelty` (position index of an open Hydra64 database).
- Play page: coach (`src/ui/coach.*`, `EngineGame::setCoachHook`), hint (`showHint`), fixed depth,
  pondering (`UciEngine::ponder / ponderHit`, `EngineGame`).
- Tournament: gauntlet, time per move / depth, adjudication, comments, Elo ± LOS, crosstable,
  pondering, engines' book; openings as a single choice (panel).
- Solution test (outside SCID): `src/ui/solutiontest*`.
- Player profile: `Database::playerStats`, `src/ui/playerreport.*`, `MainWindow::playerReport`
  (Database menu, right-click on a game).
- Crosstable of a tournament of the database: `Database::eventTable`, `src/ui/eventtable.*`,
  `MainWindow::eventTable / showGameIds`.
- ECO classification: data `data/eco/*.tsv` (lichess-org/chess-openings, CC0, Qt resource
  `:/eco/`), core `src/chess/eco.*` (`Book::load / lookup / classify / nameOf`),
  `Database::classifyEco` (parallel, `replayMainline`), `src/ui/openings.*` (`openingsBook`,
  `openingLabel`), `MainWindow::classifyDatabase / identifyOpening`, `GameListModel::rewriteGames`
  (PGN rewritten once), tooltip of the ECO column, names in the player profile.
- Export of the displayed games (PGN, EPD, HTML): `MainWindow::exportVisibleGames /
  exportVisibleDialog`, `GameListPanel::visibleRows`.
- Game preview: `src/ui/gamepreview.*`, `MainWindow::previewGame` (right-click → Preview…).
- Finish the game: `src/ui/autoplayer.*`, `MainWindow::finishGame` (Game → Finish the game).
- Training (opening with the tree, best move, mates in N): `src/ui/training.*`,
  `MainWindow::trainingDialog` (Game → Training…); collections read by `solutiontest::load`.
- Endgame tablebases: Fathom (`third_party/fathom`, MIT, built as C in the core),
  `src/chess/tablebase.*` (`tb::init / probe`), panel `src/ui/tablebasepanel.*` ("Finales",
  Databases and Analysis pages); Lichess online service as an option (QtNetwork, FEN alone);
  3-piece tables of the tests in `tests/data/syzygy` (SHA-256 checked).
- Swiss system: `Tournament::pairSwissRound` (score groups, no rematch by backtracking, balanced
  colours, bye = 1 point, row without Black), next round paired in `startNextGame`;
  `GameRecord::round`, `swissRound` / `pairing` saved in the JSON.
- Search by material and patterns + position search types: `src/db/materialquery.*`
  (`MaterialQuery`, `MaterialMatcher`), field `HeaderQuery::material`: replay in
  `Database::searchHeaders` (hy64) and `matches` (PGN); "Matériel et motifs" tab of
  `HeaderSearchDialog`, menu Database → Search by material and patterns….
- Filter graphs: `src/ui/filtergraph.*` (`FilterGraphDialog::byYear / byElo`, `BucketChart`),
  `IndexedGameSource::summary` (fast in hy64: `Hy64Source::summary`), `GameListModel::summary`,
  `MainWindow::filterGraph` (Database → Filter graphs…).
- ECO browser: `src/ui/ecobrowser.*`, lines of the ECO book `chess::eco::Book::lines()`,
  `Summary::eco`, `MainWindow::ecoBrowser` (Database → ECO browser…): filter by range
  (`HeaderQuery::ecoFrom/ecoTo`), line played in the Analysis page (`analyseGame`).
- Forced opening: `PlaySettings::openingMoves`, `PlayDialog::openingLine` (ECO code or moves),
  checkbox and ECO browser button in the Play dialog; moves played in `EngineGame::start` (ECO /
  Opening headers).
- Piece tracking: `src/ui/piecetracker.*` (`PieceTracker::track`: specific piece by its starting
  square, castling, capture, en passant; or all the pieces of a type), `HeatBoard` map, Game →
  Piece tracking….
- Tactical exercises: `annotation::Options::markExercises / exercisesFile`, `Summary::exercises`
  (EPD `bm`), `annotation::appendExercises`; checkbox in `AnnotateDialog`, default collection
  `~/hydra64/exercises/exercises.epd` (replayed by the training).
- Calculation of variations: `src/ui/calcvar.*` (`CalcVarDialog`: variations played on a separate
  board, evaluation by symbol, Stockfish judgement MultiPV 3 on the position then 1 per end of
  variation, gap capped at ±3 pawns), Game → Calculation of variations….
- Beginner level (instead of Phalanx, absent): `PlaySettings::beginnerLevel` (1–3),
  `EngineGame::pickBeginnerLine` (tolerated loss 450 / 250 / 120 cp, weighted draw), Elo at the
  1320 floor and MultiPV ≥ 6; "Niveau" list of the Play dialog. Phalanx, if installed, is chosen
  like any engine.
- Opening report: `src/ui/openingreport.*` (`OpeningReport::build`: results, Elo, periods and
  popularity relative to the starting view, players, 10 best games, next moves and pawn structures
  10 half-moves later on the 5,000 strongest games; `toHtml`, link `game:<row>`),
  `MainWindow::openingReport` (position index, otherwise replay of the view), Database → Opening
  report….
- Board as an image: `MainWindow::boardImage / saveBoardImage` (rendering of the `BoardWidget`),
  Game → Copy the board as an image / Save the board as an image….

## Planned sequel (working order)

Medium:
1. ~~Player profile~~ (done).
2. ~~Crosstable of a tournament of the database~~ (done).
3. ~~ECO classification~~ (done).
4. ~~Export of the filter as EPD / HTML~~ (done).
5. ~~Game preview~~ (done).
6. ~~Finish the game~~ (done).
7. ~~Trainings~~ (done).
8. ~~Endgame tablebases~~ (done).
9. ~~Swiss system~~ (done).
12. ~~Position search types~~ (done, with 10).
13. ~~Filter graphs~~ (done).
14. ~~ECO browser~~ (done).
15. ~~Forced opening~~ (done).
16. ~~Piece tracking~~ (done).
17. ~~Tactical exercises~~ (done).
18. ~~Calculation of variations~~ (done).
19. ~~Beginner level~~ (done).

Large:
10. ~~Search by material and patterns~~ (done).
11. ~~Opening report~~ (done).
20. Correspondence / e-mail game; 21. Online play on FICS; 22. Electronic boards (to be seen last:
    external services or hardware).

## In progress

Medium and large items of the plan: done (except 20–22). Still to be decided with the user:
20. Correspondence / e-mail, 21. FICS (external services, account), 22. electronic boards (DGT /
    Novag hardware, impossible to test here).

## After the plan (requests of 16 November 2026)

- **Built-in training collections** (done): `data/training/*.epd`, Qt resources `:/training/…`
  (CMake `training_sets`). 3,600 Lichess exercises (CC0): mates in 1…5 (300/500/500/300/200, with
  `dm N` and `pv`), best move easy/medium/hard (600 each). Selection and conversion:
  `tools/lichess-puzzles/select.py` and `convert.cpp` (the first Lichess move, the opponent's, is
  played; each mate replayed up to the mate). `TestPosition` gains `line` (pv) and `mateIn` (dm);
  `TrainingDialog::builtInCollections(mode)`, "Recueil" list (built-in ones then "Autre
  fichier…", setting `training/collection<mode>`), order drawn at random for the built-in ones,
  `dm` avoids the Stockfish measurement. Test: `ui_tests` "recueils intégrés : 3600 positions, 0
  défaut(s)".
- **"QWidgetWindow … must be a top level window" / frozen Play page** (fixed): cause found thanks
  to `diagnostic.log` (in `~/.config/Hydra64/Hydra64/`). A detached panel (ADS, X11):
  `CFloatingDockContainer` created as a child of the manager then `winId()`; with Qt 6.12 the
  manager and its areas become native, and after `restoreState()` orphan native windows remain (18
  after a few page changes): pieces of other pages drawn on top, warning at each context menu. Fix:
  `cmake/patch-qtads.cmake` (PATCH_COMMAND of FetchContent) creates the floating windows without a
  parent, as ADS already does under Wayland. Test: `ui_tests` "panneau détaché et changements de
  page : 0 fenêtre(s) native(s) enfant(s)" (fails at 18 without the fix).
- **EPD / FEN files opened natively** (done): common reader of the core `src/chess/epd.h/.cpp`
  (`parseLine`, `toPgn`: one game per position, `pv` or `bm` as main line, other `bm` and `am` "?"
  as variations, other operations as a comment, `;` between quotes respected, hmvc / fmvn).
  `GameListModel::loadFile` reads `.epd` / `.fen` (`isEpd()`, read-only: `writeData` refuses, the
  file is never rewritten); Open dialog; conversion `hy64db::EpdSource` (File → Convert…: EPD →
  Hydra64 / PGN). Tests: `core_tests` "EPD : …" (including the 3,600 positions of the built-in
  collections), `ui_tests` "fichier EPD ouvert comme base" and "EPD → base Hydra64 modifiable".
- **Personal exercise collections** (done, request of 16 November 2026: "create my own training
  files, edit them, add to them"):
  1. Editable EPD: `chess::epd::fromGame` / `fromPgn` (game → EPD line: bm / am of the first moves,
     pv = main line, id = Event except "Position n", starting comment = operations, free text as
     c0, hmvc / fmvn), `parseOperations`, `isStandardOpcode`. `GameListModel::writeData` rewrites
     an `.epd` as EPD (then rereads it); `.fen` read-only (`m_fen`). `solutiontest::fromEpd` goes
     through the common reader (";" between quotes), `fromGame` reads `dm` and the continuation
     (`line`).
  2. `src/ui/addexercise.*`: `exercise::Exercise`, `toGame` (FEN header, solution + continuation,
     comment `dm N; c0 "…"`), `mateLength`, `collections()` (the user's exercises folder), `append`
     (EPD / PGN / hy64i, created if needed, or the open model); `AddExerciseDialog`;
     `MainWindow::addToCollection` / `addExerciseDialog` / `addExercise`, Game → Add to the
     collection… (Ctrl+Shift+R). Solution = next move of the notation (the user's choice).
  3. Training: `TrainingDialog::userCollections()` ("Mes recueils : …"), list refreshed on display,
     file order (the built-in ones stay drawn at random).
  Tests: `core_tests` (stable EPD round trip on 1,100 positions, game → EPD), `ui_tests` (editable
  EPD, read-only FEN, addition in the 3 formats reread by the Training, dialog, Ctrl+Shift+R from
  the board). README and Manual.html up to date.
- **Spaced repetition and checking of collections** (done, sequel to the personal collections):
  - `src/ui/trainingprogress.*`: Leitner boxes 0–5 (1, 3, 7, 14, 30 days), `record`, `order` (to
    review, new, others), `stats`, `reset`, file `<AppData>/training-progress.json` (key: path of
    the collection, FEN). `TrainingDialog`: "Révision espacée" checkbox (`training/spaced`,
    ticked), failed one put back `RequeueGap` = 4 positions later (twice at most), statistics of
    the collection under the score, "Remettre à zéro", "Solution" counts as a failure.
  - `src/ui/collectioncheck.*`: `collectioncheck::judge` (pure: dm mates, solution confirmed /
    ambiguous / doubtful, am move played; margin 60 cp, clear gain 400 cp),
    `CollectionCheckDialog` (Stockfish, MultiPV 2, second `searchmoves` search restricted to the
    solutions if the engine prefers another move), Engine → Check a collection…, "Vérifier…" button
    of the Training.
  Tests `ui_tests`: boxes and order, failed one put back and at the head of the next session,
  judgement rules, real Stockfish run (correct / doubtful / shorter mate).
- **Tidying of the data into `databases/`** (done, 16 November 2026): `data/` removed.
  `databases/ouvertures-eco/` (formerly `data/eco`, still embedded as resource `:/eco`; now
  `resources/eco-opening/`), `databases/entrainement/` (became `databases/training/`, see below):
  **Meilleur_coup.hy64i** (1,800, Event "Meilleur coup, facile / moyen / difficile", White "Lichess
  <id>", WhiteElo = rating, Site = link) and **Mats.hy64i** (1,765, Event "Mat en N", `dm N` as a
  comment; 35 Lichess "mateIn5" longer than 5 moves discarded). Making: 
  `tools/lichess-puzzles/to_pgn.py` then `hydra64-convert`. CMake copies the folder next to the
  executable (target `training_bases`); `TrainingDialog::builtInDir()` (next to the executable,
  otherwise `HYDRA64_DATABASES_DIR`); `Collection::category` filters the positions
  (`startPositions(…, category)`); `TestPosition::rating` (WhiteElo) displayed. Tests:
  `core_tests` "bases d'entraînement" (each mate replayed), `ui_tests` "bases intégrées
  (Hydra64)".
  Then, at the user's request: `databases/entrainement/` → **`databases/training/`**, one database
  per category (today single-file: `best_moves/best_move_easy|medium|hard.hy64`,
  `mates/mate_in_1…5.hy64`).
- **Opening under the board** (done, 16 November 2026): `BoardViewer::openingAt(node)` goes back
  from the displayed position towards the start and returns the first one recognised by
  `openingsBook().lookup` (hence the deepest, transpositions included); otherwise the last move as
  before. `ElidedLabel` (cut "…", tooltip: opening + last move); refreshed on `treeChanged` too.
  Test `ui_tests` "ouverture sous l'échiquier".
- **Opening names in French** (done, 16 November 2026): `openingName(english)` (interface
  language, cache) and `frenchOpeningName` in `src/ui/openings.cpp`: table of the families
  (established names + frequent segments), main words (Defense, Variation, Attack, Gambit…) with
  gender, adjectives with agreement (Accepted, Classical, English…), groups (King's Indian, Two
  Knights…), proper names kept. Display only (under the board, ECO tooltip, ECO browser, Play
  dialog, opening report); PGN headers in English. Test `ui_tests` "noms d'ouvertures en
  français". Complete ctest twice OK.

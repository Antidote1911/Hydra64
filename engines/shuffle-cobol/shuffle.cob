       IDENTIFICATION DIVISION.
       PROGRAM-ID. BIG-BRAIN-3000.
       AUTHOR. AN-OCTOPUS-ON-A-KEYBOARD.
      *================================================================
      * SHUFFLE-COBOL: THE "BONUS" EDITION OF SHUFFLE.
      *
      * A UCI CHESS ENGINE THAT PLAYS SILLY MOVES, ALMOST AT RANDOM,
      * WRITTEN IN THE LANGUAGE OF OUR GRANDPARENTS. IT COMPUTES
      * NOTHING, BUT IT DOES SO WITH THE RIGOUR OF A BANK.
      *
      * THE REAL SHUFFLE, THE ONE SHIPPED WITH HYDRA64, IS WRITTEN IN
      * RUST (../shuffle). THIS ONE FOLLOWS THE SAME RULES: SAME
      * WEIGHTS, SAME OPTIONS, SAME UCI ANSWERS. IT IS JUST SLOWER,
      * CHATTIER, AND IT SHOUTS IN CAPITAL LETTERS.
      *
      * NOTABLE DIFFERENCE: COBOL KNOWS NOTHING ABOUT THREADS. WHILE
      * IT SCRATCHES ITS CHIN, IT LISTENS TO NOBODY. A "stop" SENT AT
      * THAT MOMENT WILL BE READ AFTERWARDS.
      *================================================================
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
      *    THE GUI TALKS TO US THROUGH THIS PIPE. WE DO NOT ANSWER
      *    THROUGH IT: WE SHOUT INTO THE ROOM (DISPLAY).
           SELECT THE-INBOUND-PIPE ASSIGN TO KEYBOARD
               ORGANIZATION IS LINE SEQUENTIAL.

       DATA DIVISION.
       FILE SECTION.
       FD THE-INBOUND-PIPE.
       01 WHAT-THE-GUI-SAYS       PIC X(8192).

       WORKING-STORAGE SECTION.
      *----------------------------------------------------------------
      * THE BUSINESS CARD
      *----------------------------------------------------------------
       01 MY-NAME                 PIC X(30)
                                  VALUE "Shuffle-COBOL 0.1".
       01 MY-AUTHOR               PIC X(40)
                                  VALUE "An octopus on a keyboard".

      *----------------------------------------------------------------
      * THE STACK OF PLATES: ONE CHECKERED THING PER FLOOR OF
      * THOUGHT. FLOOR 1 IS THE REAL GAME, THE NEXT ONES ARE SCRAP
      * PAPER, FLOOR 12 IS THE SPARE TYRE.
      * SQUARES NUMBERED FROM 1 (a1) TO 64 (h8). THINGY > 0: WHITE,
      * THINGY < 0: BLACK. 1 PAWN 2 KNIGHT 3 BISHOP 4 ROOK 5 QUEEN
      * 6 KING.
      *----------------------------------------------------------------
       01 THE-STACK-OF-PLATES.
          05 THE-CHECKERED-THING OCCURS 12.
             10 ITS-STATE.
                15 THINGY              PIC S9 OCCURS 64.
                15 WHOSE-TURN-IT-IS    PIC S9.
      *            1 WHITE SHORT, 2 WHITE LONG,
      *            3 BLACK SHORT, 4 BLACK LONG
                15 RIGHT-TO-HIDE       PIC 9  OCCURS 4.
                15 THE-GHOST-PAWN      PIC 99.
                15 THE-BOREDOM         PIC 9(4).
                15 THE-CAPTAINS-AGE    PIC 9(4).
             10 NB-IDEAS               PIC 999.
             10 AN-IDEA OCCURS 256.
                15 I-FROM              PIC 99.
                15 I-TO                PIC 99.
                15 I-PROMO             PIC 9.
       01 THE-SPARE-FLOOR         PIC 99 VALUE 12.

      *----------------------------------------------------------------
      * THE TABLES OF THE LAW (EACH DIGIT IS AN OFFSET + 2)
      *----------------------------------------------------------------
      *    THE 8 WINDS: 4 DIAGONALS (BISHOP) THEN 4 STRAIGHTS (ROOK)
       01 THE-EIGHT-WINDS-TXT     PIC X(16)
                                  VALUE "3331131132122321".
       01 THE-EIGHT-WINDS REDEFINES THE-EIGHT-WINDS-TXT.
          05 A-WIND OCCURS 8.
             10 WIND-F            PIC 9.
             10 WIND-R            PIC 9.
      *    THE 8 FLEA HOPS OF THE KNIGHT
       01 THE-FLEA-HOPS-TXT       PIC X(16)
                                  VALUE "3443413010010314".
       01 THE-FLEA-HOPS REDEFINES THE-FLEA-HOPS-TXT.
          05 A-HOP OCCURS 8.
             10 HOP-F             PIC 9.
             10 HOP-R             PIC 9.
      *    WHOLESALE PRICES: NOTHING, PAWN, KNIGHT, BISHOP, ROOK,
      *    QUEEN, KING (PRICELESS, SO ZERO)
       01 THE-PRICES-TXT          PIC X(21)
                                  VALUE "000100300320500900000".
       01 THE-PRICES REDEFINES THE-PRICES-TXT.
          05 A-PRICE              PIC 9(3) OCCURS 7.
       01 THE-LETTERS             PIC X(8) VALUE "abcdefgh".
       01 THE-DIGITS              PIC X(8) VALUE "12345678".
       01 THE-DRAWINGS            PIC X(7) VALUE ".pnbrqk".
       01 THE-PROMOS              PIC X(6) VALUE " pnbrq".

      *----------------------------------------------------------------
      * THE KNOBS THE GUI IS ALLOWED TO FIDDLE WITH
      *----------------------------------------------------------------
       01 THE-STYLE               PIC X VALUE "S".
          88 STYLE-SILLY          VALUE "S".
          88 STYLE-RANDOM         VALUE "R".
       01 THE-FIXED-SEED          PIC 9(10) VALUE 0.
       01 THE-DELAY               PIC 9(5)  VALUE 300.
       01 THE-MULTIPV             PIC 999   VALUE 1.

      *----------------------------------------------------------------
      * THE MOOD OF THE MOMENT
      *----------------------------------------------------------------
       01 EXCITEMENT-LEVEL        PIC 9(4)V9.
       01 WHAT-I-SEE              PIC X.
          88 OOH-SOMETHING-SHINY  VALUE "O".
       01 THE-GUY-WITH-THE-CROWN  PIC X.
          88 HE-IS-CHILL          VALUE "C".
          88 HE-LOOKS-ANNOYED     VALUE "A".
          88 HE-IS-TOTALLY-BROKEN VALUE "M".
       01 THE-DREAM-PROMOTION     PIC 9.
          88 IT-WANTS-A-QUEEN     VALUE 5.
          88 A-LITTLE-HORSE       VALUE 2.
          88 SOME-OTHER-UNDERPROMOTION VALUES 3 4.
       01 MY-WORST-IDEA           PIC X(5) VALUE "0000".
       01 THE-WAIT                PIC X VALUE "N".
          88 WAITING-FOR-STOP     VALUE "Y".
          88 WAITING-FOR-NOTHING  VALUE "N".
       01 THE-END                 PIC X VALUE "N".
          88 IT-IS-THE-END        VALUE "Y".
       01 THE-DISASTER            PIC X VALUE "N".
          88 IT-IS-A-DISASTER     VALUE "Y".
          88 ALL-IS-WELL          VALUE "N".

      *    WHAT EACH IDEA OF FLOOR 1 MAKES ME FEEL
       01 THE-IMPRESSIONS.
          05 AN-IMPRESSION OCCURS 256.
             10 EXCITEMENT        PIC 9(4)V9.
             10 WHAT-I-SAW        PIC X.
             10 KING-MOOD         PIC X.
      *    THE WHEELBARROW WE DRAW LOTS FROM
       01 THE-WHEELBARROW.
          05 B-NB                 PIC S9(4) COMP-5.
          05 B-PICK               PIC S9(4) COMP-5.
          05 B-I                  PIC S9(4) COMP-5.
          05 B-ITEM OCCURS 256.
             10 B-K               PIC S9(4) COMP-5.
             10 B-W               PIC 9(4)V9.
       01 THE-TOTAL               PIC 9(7)V9.
       01 THE-TARGET              PIC 9(7)V9(6).
       01 THE-LUCK                PIC 9V9(9).
       01 THE-PODIUM.
          05 NB-RANKED            PIC S9(4) COMP-5.
          05 RANKED-K             PIC S9(4) COMP-5 OCCURS 256.

      *----------------------------------------------------------------
      * THE GUI'S WORDS, SLICED INTO ROUNDS
      *----------------------------------------------------------------
       01 THE-LINE                PIC X(8192).
       01 THE-LENGTH              PIC S9(8) COMP-5.
       01 THE-CURSOR              PIC S9(8) COMP-5.
       01 A-WORD                  PIC X(100).
       01 THE-WORDS.
          05 NB-WORDS             PIC S9(4) COMP-5.
          05 THE-WORD             PIC X(100) OCCURS 1700.
       01 WORD-MOVES              PIC S9(4) COMP-5.
       01 WORD-VALUE              PIC S9(4) COMP-5.
       01 WORD-I                  PIC S9(4) COMP-5.
       01 WORD-END                PIC S9(4) COMP-5.
       01 BEAD-START              PIC S9(4) COMP-5.
       01 BEAD-END                PIC S9(4) COMP-5.
       01 BEAD-I                  PIC S9(4) COMP-5.
       01 THE-THREAD              PIC S9(4) COMP-5.
       01 THE-NECKLACE            PIC X(200).
       01 THE-KNOB-NAME           PIC X(200).
       01 THE-VALUE               PIC X(200).
       01 THE-NUMBER              PIC S9(12) COMP-5.
       01 THE-DECODING            PIC X.
          88 IT-IS-A-NUMBER       VALUE "Y".
          88 IT-IS-GIBBERISH      VALUE "N".

      *----------------------------------------------------------------
      * THE FEN GRIMOIRE
      *----------------------------------------------------------------
       01 FEN-PLACEMENT           PIC X(100).
       01 FEN-SIDE                PIC X(100).
       01 FEN-CASTLING            PIC X(100).
       01 FEN-EP                  PIC X(100).
       01 FEN-HALF                PIC X(100).
       01 FEN-FULL                PIC X(100).
       01 F-I                     PIC S9(4) COMP-5.
       01 F-J                     PIC S9(4) COMP-5.
       01 F-K                     PIC S9(4) COMP-5.
       01 F-L                     PIC S9(4) COMP-5.
       01 F-F                     PIC S9(4) COMP-5.
       01 F-R                     PIC S9(4) COMP-5.
       01 F-S                     PIC S9(4) COMP-5.
       01 F-WHITE-KINGS           PIC S9(4) COMP-5.
       01 F-BLACK-KINGS           PIC S9(4) COMP-5.
       01 F-C                     PIC X.
       01 F-DIGIT REDEFINES F-C   PIC 9.
       01 THE-SQUARE-TEXT         PIC XX.
       01 THE-SQUARE              PIC S9(4) COMP-5.
       01 S-F                     PIC S9(4) COMP-5.
       01 S-R                     PIC S9(4) COMP-5.
       01 S-I                     PIC S9(4) COMP-5.
       01 THE-MOVE-TEXT           PIC X(100).
       01 THE-K                   PIC S9(4) COMP-5.
       01 U-FROM                  PIC S9(4) COMP-5.
       01 U-TO                    PIC S9(4) COMP-5.
       01 U-PROMO                 PIC S9(4) COMP-5.
       01 U-I                     PIC S9(4) COMP-5.

      *----------------------------------------------------------------
      * EACH ROUTINE'S LOCKER (COBOL HAS NO STACK, SO EVERY PARAGRAPH
      * BRINGS ITS OWN SOCKS)
      *----------------------------------------------------------------
      *    WHERE-AM-I
       01 X-SQ                    PIC S9(4) COMP-5.
       01 X-T                     PIC S9(4) COMP-5.
       01 X-F                     PIC S9(4) COMP-5.
       01 X-R                     PIC S9(4) COMP-5.
      *    IMAGINE-SILLY-THINGS
       01 G-FLOOR                 PIC S9(4) COMP-5.
       01 G-US                    PIC S9(4) COMP-5.
       01 G-SQ                    PIC S9(4) COMP-5.
       01 G-PIECE                 PIC S9(4) COMP-5.
       01 G-TYPE                  PIC S9(4) COMP-5.
       01 G-F                     PIC S9(4) COMP-5.
       01 G-R                     PIC S9(4) COMP-5.
       01 G-F1                    PIC S9(4) COMP-5.
       01 G-R1                    PIC S9(4) COMP-5.
       01 G-DF                    PIC S9(4) COMP-5.
       01 G-DIR                   PIC S9(4) COMP-5.
       01 G-FIRST                 PIC S9(4) COMP-5.
       01 G-LAST                  PIC S9(4) COMP-5.
       01 G-TARGET                PIC S9(4) COMP-5.
       01 G-CONTENT               PIC S9(4) COMP-5.
       01 G-PROMO                 PIC S9(4) COMP-5.
       01 G-N                     PIC S9(4) COMP-5.
       01 G-T0                    PIC S9(4) COMP-5.
       01 G-T1                    PIC S9(4) COMP-5.
       01 G-RC                    PIC S9(4) COMP-5.
       01 G-BASE                  PIC S9(4) COMP-5.
       01 G-DK                    PIC S9(4) COMP-5.
       01 G-DQ                    PIC S9(4) COMP-5.
      *    WHO-IS-GIVING-ME-THE-EYE
       01 A-FLOOR                 PIC S9(4) COMP-5.
       01 A-SQ                    PIC S9(4) COMP-5.
       01 A-BY                    PIC S9(4) COMP-5.
       01 A-F                     PIC S9(4) COMP-5.
       01 A-R                     PIC S9(4) COMP-5.
       01 A-F1                    PIC S9(4) COMP-5.
       01 A-R1                    PIC S9(4) COMP-5.
       01 A-DIR                   PIC S9(4) COMP-5.
       01 A-TARGET                PIC S9(4) COMP-5.
       01 A-P                     PIC S9(4) COMP-5.
       01 A-T                     PIC S9(4) COMP-5.
       01 A-Q                     PIC S9(4) COMP-5.
       01 A-VERDICT               PIC X.
          88 SOMEONE-IS-STARING   VALUE "Y".
      *    PUSH-THE-THINGY
       01 J-FLOOR                 PIC S9(4) COMP-5.
       01 J-K                     PIC S9(4) COMP-5.
       01 J-NEXT                  PIC S9(4) COMP-5.
       01 J-FROM                  PIC S9(4) COMP-5.
       01 J-TO                    PIC S9(4) COMP-5.
       01 J-PROMO                 PIC S9(4) COMP-5.
       01 J-PIECE                 PIC S9(4) COMP-5.
       01 J-SIGN                  PIC S9(4) COMP-5.
       01 J-TYPE                  PIC S9(4) COMP-5.
       01 J-X                     PIC S9(4) COMP-5.
       01 J-FFROM                 PIC S9(4) COMP-5.
       01 J-RFROM                 PIC S9(4) COMP-5.
       01 J-BASE                  PIC S9(4) COMP-5.
      *    FIND-THE-GUY-WITH-THE-CROWN
       01 K-FLOOR                 PIC S9(4) COMP-5.
       01 K-COLOR                 PIC S9(4) COMP-5.
       01 K-SQ                    PIC S9(4) COMP-5.
       01 K-S                     PIC S9(4) COMP-5.
       01 K-KING                  PIC S9(4) COMP-5.
      *    IS-HE-SHAKING
       01 E-FLOOR                 PIC S9(4) COMP-5.
       01 E-VERDICT               PIC X.
          88 HE-IS-SHAKING        VALUE "Y".
      *    WEED-OUT-THE-SUICIDES
       01 L-FLOOR                 PIC S9(4) COMP-5.
       01 L-NEXT                  PIC S9(4) COMP-5.
       01 L-US                    PIC S9(4) COMP-5.
       01 L-K                     PIC S9(4) COMP-5.
       01 L-W                     PIC S9(4) COMP-5.
      *    WEIGH-THE-LOOT
       01 M-FLOOR                 PIC S9(4) COMP-5.
       01 M-S                     PIC S9(4) COMP-5.
       01 M-P                     PIC S9(4) COMP-5.
       01 M-I                     PIC S9(4) COMP-5.
       01 M-TOTAL                 PIC S9(8) COMP-5.
      *    WRITE-THE-MOVE
       01 W-FLOOR                 PIC S9(4) COMP-5.
       01 W-K                     PIC S9(4) COMP-5.
       01 THE-WRITTEN-MOVE        PIC X(5).
      *    DO-ANYTHING-WITH-CONFIDENCE AND ITS BUDDIES
       01 V-K                     PIC S9(4) COMP-5.
       01 V-TO                    PIC S9(4) COMP-5.
       01 N-I                     PIC S9(4) COMP-5.
       01 THE-SCORE               PIC X(20).
       01 THE-SCRATCHING-TIME     PIC S9(12) COMP-5.
       01 ONE-MILLISECOND         PIC S9(18) COMP-5 VALUE 1000000.
       01 THE-THOUGHT             PIC X.
          88 THINKING-FOREVER     VALUE "Y".
       01 THE-LAZINESS            PIC X.
          88 NOT-THINKING-AT-ALL  VALUE "Y".
       01 T-I                     PIC S9(4) COMP-5.
      *    COUNT-THE-SHEEP
       01 THE-DEPTH               PIC S9(4) COMP-5.
       01 THE-SHEEP               PIC S9(18) COMP-5.
       01 P-CURRENT               PIC S9(4) COMP-5.
       01 P-K                     PIC S9(4) COMP-5 OCCURS 12.
      *    SHOW-THE-CARNAGE
       01 D-R                     PIC S9(4) COMP-5.
       01 D-F                     PIC S9(4) COMP-5.
       01 D-S                     PIC S9(4) COMP-5.
       01 D-P                     PIC S9(4) COMP-5.
       01 D-POS                   PIC S9(4) COMP-5.
       01 D-C                     PIC X.
       01 THE-ROW                 PIC X(20).
      *    THE CLOCK AND THE CALENDAR
       01 TODAYS-DATE             PIC X(21).
       01 THE-DATE-IN-PIECES REDEFINES TODAYS-DATE.
          05 FILLER               PIC X(8).
          05 THE-HOURS            PIC 99.
          05 THE-MINUTES          PIC 99.
          05 THE-SECONDS          PIC 99.
          05 THE-HUNDREDTHS       PIC 99.
          05 FILLER               PIC X(5).
       01 THE-SEED                PIC 9(10).
       01 THE-TICK                PIC S9(12) COMP-5.
       01 THE-START-TICK          PIC S9(12) COMP-5.
       01 THE-DURATION            PIC S9(12) COMP-5.
      *    PRETTY NUMBERS FOR DISPLAY
       01 PRETTY-NUMBER           PIC -(11)9.
       01 PRETTY-1                PIC Z(11)9.
       01 PRETTY-2                PIC Z(11)9.
       01 PRETTY-3                PIC Z(17)9.

       PROCEDURE DIVISION.
      *================================================================
      * THE BIG SHOW: LISTEN, DIGEST, START AGAIN.
      *================================================================
       THE-BIG-SHOW.
           PERFORM WAKE-UP-THE-OCTOPUS
           OPEN INPUT THE-INBOUND-PIPE
           PERFORM UNTIL IT-IS-THE-END
               READ THE-INBOUND-PIPE
                   AT END
                       SET IT-IS-THE-END TO TRUE
                   NOT AT END
                       PERFORM DIGEST-THE-LINE
               END-READ
           END-PERFORM
           CLOSE THE-INBOUND-PIPE
           STOP RUN.

       WAKE-UP-THE-OCTOPUS.
           PERFORM THE-STARTING-POSITION
           PERFORM SOW-THE-LUCK.

       DIGEST-THE-LINE.
           MOVE WHAT-THE-GUI-SAYS TO THE-LINE
           INSPECT THE-LINE REPLACING ALL X"09" BY SPACE
                                      ALL X"0D" BY SPACE
           PERFORM SLICE-INTO-ROUNDS
           IF NB-WORDS = 0
               EXIT PARAGRAPH
           END-IF
           EVALUATE THE-WORD(1)
               WHEN "uci"
                   PERFORM INTRODUCE-ONESELF-POLITELY
               WHEN "isready"
                   DISPLAY "readyok"
               WHEN "ucinewgame"
                   PERFORM THE-STARTING-POSITION
                   PERFORM SOW-THE-LUCK
               WHEN "setoption"
                   PERFORM FIDDLE-WITH-THE-KNOBS
               WHEN "position"
                   PERFORM SET-UP-THE-THINGIES
               WHEN "go"
                   PERFORM DO-ANYTHING-WITH-CONFIDENCE
                      THRU CRY-IN-A-CORNER
               WHEN "stop"
               WHEN "ponderhit"
                   PERFORM SPIT-OUT-THE-PENDING-MOVE
               WHEN "d"
                   PERFORM SHOW-THE-CARNAGE
               WHEN "quit"
                   SET IT-IS-THE-END TO TRUE
               WHEN OTHER
                   DISPLAY "info string unknown command: "
                       FUNCTION TRIM(THE-WORD(1))
           END-EVALUATE.

      *    A LINE GOES IN, WORDS COME OUT. THE SLOT AFTER THE LAST
      *    WORD IS EMPTIED, SO AS NOT TO CHEW ON YESTERDAY'S CRUMBS.
       SLICE-INTO-ROUNDS.
           MOVE 0 TO NB-WORDS
           MOVE SPACES TO THE-WORD(1)
           COMPUTE THE-LENGTH =
               FUNCTION LENGTH(FUNCTION TRIM(THE-LINE TRAILING))
           MOVE 1 TO THE-CURSOR
           PERFORM UNTIL THE-CURSOR > THE-LENGTH
                      OR NB-WORDS >= 1699
               MOVE SPACES TO A-WORD
               UNSTRING THE-LINE DELIMITED BY ALL SPACE
                   INTO A-WORD WITH POINTER THE-CURSOR
               END-UNSTRING
               IF A-WORD NOT = SPACES
                   ADD 1 TO NB-WORDS
                   MOVE A-WORD TO THE-WORD(NB-WORDS)
               END-IF
           END-PERFORM
           MOVE SPACES TO THE-WORD(NB-WORDS + 1).

       INTRODUCE-ONESELF-POLITELY.
           DISPLAY "id name " FUNCTION TRIM(MY-NAME)
           DISPLAY "id author " FUNCTION TRIM(MY-AUTHOR)
           DISPLAY "option name Style type combo default Silly "
                   "var Silly var Random"
           DISPLAY "option name Seed type spin default 0 "
                   "min 0 max 2147483647"
           DISPLAY "option name Move Delay type spin default 300 "
                   "min 0 max 10000"
           DISPLAY "option name MultiPV type spin default 1 "
                   "min 1 max 256"
           DISPLAY "uciok".

      *================================================================
      * THE KNOBS: setoption name <NAME...> value <VALUE...>
      *================================================================
       FIDDLE-WITH-THE-KNOBS.
           MOVE 0 TO WORD-VALUE
           PERFORM VARYING WORD-I FROM 2 BY 1 UNTIL WORD-I > NB-WORDS
               IF THE-WORD(WORD-I) = "value" AND WORD-VALUE = 0
                   MOVE WORD-I TO WORD-VALUE
               END-IF
           END-PERFORM
           MOVE 3 TO BEAD-START
           IF WORD-VALUE > 0
               COMPUTE BEAD-END = WORD-VALUE - 1
           ELSE
               MOVE NB-WORDS TO BEAD-END
           END-IF
           PERFORM STRING-THE-BEADS
           MOVE FUNCTION LOWER-CASE(THE-NECKLACE) TO THE-KNOB-NAME
           MOVE SPACES TO THE-NECKLACE
           IF WORD-VALUE > 0
               COMPUTE BEAD-START = WORD-VALUE + 1
               MOVE NB-WORDS TO BEAD-END
               PERFORM STRING-THE-BEADS
           END-IF
           MOVE THE-NECKLACE TO THE-VALUE
           PERFORM DECIPHER-THE-VALUE
           EVALUATE THE-KNOB-NAME
               WHEN "style"
                   EVALUATE FUNCTION LOWER-CASE(THE-VALUE)
                       WHEN "silly"
                           SET STYLE-SILLY TO TRUE
                       WHEN "random"
                           SET STYLE-RANDOM TO TRUE
                   END-EVALUATE
               WHEN "seed"
                   MOVE 0 TO THE-FIXED-SEED
                   IF IT-IS-A-NUMBER AND THE-NUMBER > 0
                      AND THE-NUMBER <= 2147483647
                       MOVE THE-NUMBER TO THE-FIXED-SEED
                   END-IF
                   PERFORM SOW-THE-LUCK
               WHEN "move delay"
                   IF IT-IS-A-NUMBER AND THE-NUMBER >= 0
                       IF THE-NUMBER > 10000
                           MOVE 10000 TO THE-DELAY
                       ELSE
                           MOVE THE-NUMBER TO THE-DELAY
                       END-IF
                   END-IF
               WHEN "multipv"
                   EVALUATE TRUE
                       WHEN IT-IS-GIBBERISH
                           MOVE 1 TO THE-MULTIPV
                       WHEN THE-NUMBER < 1
                           MOVE 1 TO THE-MULTIPV
                       WHEN THE-NUMBER > 256
                           MOVE 256 TO THE-MULTIPV
                       WHEN OTHER
                           MOVE THE-NUMBER TO THE-MULTIPV
                   END-EVALUATE
      *        Threads, Hash, UCI_Elo...: WE NOD POLITELY.
           END-EVALUATE.

      *    GLUES WORDS BEAD-START TO BEAD-END INTO THE-NECKLACE
       STRING-THE-BEADS.
           MOVE SPACES TO THE-NECKLACE
           MOVE 1 TO THE-THREAD
           PERFORM VARYING BEAD-I FROM BEAD-START BY 1
                   UNTIL BEAD-I > BEAD-END
               IF BEAD-I > BEAD-START
                   STRING " " DELIMITED BY SIZE
                       INTO THE-NECKLACE WITH POINTER THE-THREAD
                   END-STRING
               END-IF
               STRING FUNCTION TRIM(THE-WORD(BEAD-I))
                   DELIMITED BY SIZE
                   INTO THE-NECKLACE WITH POINTER THE-THREAD
               END-STRING
           END-PERFORM.

      *    THE-VALUE -> THE-NUMBER, IF IT REALLY IS A NUMBER
       DECIPHER-THE-VALUE.
           SET IT-IS-GIBBERISH TO TRUE
           MOVE 0 TO THE-NUMBER
           IF THE-VALUE NOT = SPACES
               IF FUNCTION TEST-NUMVAL(THE-VALUE) = 0
                   IF FUNCTION ABS(FUNCTION NUMVAL(THE-VALUE))
                      < 999999999999
                       COMPUTE THE-NUMBER = FUNCTION NUMVAL(THE-VALUE)
                       SET IT-IS-A-NUMBER TO TRUE
                   END-IF
               END-IF
           END-IF.

      *    A FIXED GRAIN OF SAND (Seed) OR WHATEVER TIME IT IS
       SOW-THE-LUCK.
           IF THE-FIXED-SEED > 0
               COMPUTE THE-LUCK = FUNCTION RANDOM(THE-FIXED-SEED)
           ELSE
               MOVE FUNCTION CURRENT-DATE TO TODAYS-DATE
               COMPUTE THE-SEED =
                   FUNCTION NUMVAL(TODAYS-DATE(7:8)) + 1
               COMPUTE THE-LUCK = FUNCTION RANDOM(THE-SEED)
           END-IF.

      *================================================================
      * THE POSITION: position startpos|fen <FEN> [moves ...]
      *================================================================
       SET-UP-THE-THINGIES.
           MOVE ITS-STATE(1) TO ITS-STATE(THE-SPARE-FLOOR)
           SET ALL-IS-WELL TO TRUE
           MOVE 0 TO WORD-MOVES
           PERFORM VARYING WORD-I FROM 2 BY 1 UNTIL WORD-I > NB-WORDS
               IF THE-WORD(WORD-I) = "moves" AND WORD-MOVES = 0
                   MOVE WORD-I TO WORD-MOVES
               END-IF
           END-PERFORM
           IF WORD-MOVES > 0
               COMPUTE WORD-END = WORD-MOVES - 1
           ELSE
               MOVE NB-WORDS TO WORD-END
           END-IF
           EVALUATE THE-WORD(2)
               WHEN "startpos"
                   PERFORM THE-STARTING-POSITION
               WHEN "fen"
                   MOVE SPACES TO FEN-PLACEMENT FEN-SIDE FEN-CASTLING
                                  FEN-EP FEN-HALF FEN-FULL
                   IF WORD-END >= 3
                       MOVE THE-WORD(3) TO FEN-PLACEMENT
                   END-IF
                   IF WORD-END >= 4
                       MOVE THE-WORD(4) TO FEN-SIDE
                   END-IF
                   IF WORD-END >= 5
                       MOVE THE-WORD(5) TO FEN-CASTLING
                   END-IF
                   IF WORD-END >= 6
                       MOVE THE-WORD(6) TO FEN-EP
                   END-IF
                   IF WORD-END >= 7
                       MOVE THE-WORD(7) TO FEN-HALF
                   END-IF
                   IF WORD-END >= 8
                       MOVE THE-WORD(8) TO FEN-FULL
                   END-IF
                   PERFORM READ-THE-FEN-GRIMOIRE
               WHEN OTHER
                   SET IT-IS-A-DISASTER TO TRUE
           END-EVALUATE
           IF ALL-IS-WELL AND WORD-MOVES > 0
               PERFORM VARYING WORD-I FROM WORD-MOVES BY 1
                       UNTIL WORD-I >= NB-WORDS OR IT-IS-A-DISASTER
                   MOVE THE-WORD(WORD-I + 1) TO THE-MOVE-TEXT
                   PERFORM RECOGNIZE-THE-MOVE
                   IF THE-K = 0
                       SET IT-IS-A-DISASTER TO TRUE
                   ELSE
                       MOVE 1 TO J-FLOOR
                       MOVE THE-K TO J-K
                       PERFORM PUSH-THE-THINGY
                       MOVE ITS-STATE(2) TO ITS-STATE(1)
                   END-IF
               END-PERFORM
           END-IF
           IF IT-IS-A-DISASTER
               MOVE ITS-STATE(THE-SPARE-FLOOR) TO ITS-STATE(1)
               DISPLAY "info string invalid position ignored"
           END-IF.

       THE-STARTING-POSITION.
           MOVE "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR"
               TO FEN-PLACEMENT
           MOVE "w" TO FEN-SIDE
           MOVE "KQkq" TO FEN-CASTLING
           MOVE "-" TO FEN-EP
           MOVE "0" TO FEN-HALF
           MOVE "1" TO FEN-FULL
           SET ALL-IS-WELL TO TRUE
           PERFORM READ-THE-FEN-GRIMOIRE.

      *    FEN-PLACEMENT ... FEN-FULL -> ITS-STATE(1), OR DISASTER
       READ-THE-FEN-GRIMOIRE.
           INITIALIZE ITS-STATE(1)
           MOVE 0 TO F-F
           MOVE 7 TO F-R
           COMPUTE F-L =
               FUNCTION LENGTH(FUNCTION TRIM(FEN-PLACEMENT TRAILING))
           PERFORM VARYING F-I FROM 1 BY 1
                   UNTIL F-I > F-L OR IT-IS-A-DISASTER
               MOVE FEN-PLACEMENT(F-I:1) TO F-C
               EVALUATE TRUE
                   WHEN F-C = "/"
                       SUBTRACT 1 FROM F-R
                       MOVE 0 TO F-F
                   WHEN F-C >= "1" AND F-C <= "8"
                       ADD F-DIGIT TO F-F
                   WHEN OTHER
                       MOVE 0 TO F-K
                       PERFORM VARYING F-J FROM 1 BY 1 UNTIL F-J > 6
                           IF FUNCTION LOWER-CASE(F-C)
                              = THE-DRAWINGS(F-J + 1:1)
                               MOVE F-J TO F-K
                           END-IF
                       END-PERFORM
                       IF F-K = 0 OR F-F > 7 OR F-R < 0
                           SET IT-IS-A-DISASTER TO TRUE
                       ELSE
                           COMPUTE F-S = F-R * 8 + F-F + 1
                           IF F-C >= "A" AND F-C <= "Z"
                               MOVE F-K TO THINGY(1, F-S)
                           ELSE
                               COMPUTE THINGY(1, F-S) = 0 - F-K
                           END-IF
                           ADD 1 TO F-F
                       END-IF
               END-EVALUATE
           END-PERFORM
           IF FEN-SIDE = "b"
               MOVE -1 TO WHOSE-TURN-IT-IS(1)
           ELSE
               MOVE 1 TO WHOSE-TURN-IT-IS(1)
           END-IF
           PERFORM VARYING F-I FROM 1 BY 1 UNTIL F-I > 4
               EVALUATE FEN-CASTLING(F-I:1)
                   WHEN "K" MOVE 1 TO RIGHT-TO-HIDE(1, 1)
                   WHEN "Q" MOVE 1 TO RIGHT-TO-HIDE(1, 2)
                   WHEN "k" MOVE 1 TO RIGHT-TO-HIDE(1, 3)
                   WHEN "q" MOVE 1 TO RIGHT-TO-HIDE(1, 4)
               END-EVALUATE
           END-PERFORM
           MOVE FEN-EP(1:2) TO THE-SQUARE-TEXT
           PERFORM DECIPHER-A-SQUARE
           MOVE THE-SQUARE TO THE-GHOST-PAWN(1)
           MOVE FEN-HALF TO THE-VALUE
           PERFORM DECIPHER-THE-VALUE
           IF IT-IS-A-NUMBER AND THE-NUMBER >= 0
              AND THE-NUMBER < 9999
               MOVE THE-NUMBER TO THE-BOREDOM(1)
           END-IF
           MOVE 1 TO THE-CAPTAINS-AGE(1)
           MOVE FEN-FULL TO THE-VALUE
           PERFORM DECIPHER-THE-VALUE
           IF IT-IS-A-NUMBER AND THE-NUMBER >= 0
              AND THE-NUMBER < 9999
               MOVE THE-NUMBER TO THE-CAPTAINS-AGE(1)
           END-IF
      *    WITHOUT ITS TWO KINGS, A CHESSBOARD IS JUST A TABLECLOTH
           MOVE 0 TO F-WHITE-KINGS F-BLACK-KINGS
           PERFORM VARYING F-S FROM 1 BY 1 UNTIL F-S > 64
               IF THINGY(1, F-S) = 6
                   ADD 1 TO F-WHITE-KINGS
               END-IF
               IF THINGY(1, F-S) = -6
                   ADD 1 TO F-BLACK-KINGS
               END-IF
           END-PERFORM
           IF F-WHITE-KINGS = 0 OR F-BLACK-KINGS = 0
               SET IT-IS-A-DISASTER TO TRUE
           END-IF.

      *    "e4" -> 29 ; ANY OLD RUBBISH -> 0
       DECIPHER-A-SQUARE.
           MOVE 0 TO THE-SQUARE S-F S-R
           PERFORM VARYING S-I FROM 1 BY 1 UNTIL S-I > 8
               IF THE-SQUARE-TEXT(1:1) = THE-LETTERS(S-I:1)
                   MOVE S-I TO S-F
               END-IF
               IF THE-SQUARE-TEXT(2:1) = THE-DIGITS(S-I:1)
                   MOVE S-I TO S-R
               END-IF
           END-PERFORM
           IF S-F > 0 AND S-R > 0
               COMPUTE THE-SQUARE = (S-R - 1) * 8 + S-F
           END-IF.

      *    THE-MOVE-TEXT ("e7e8q") -> THE-K, ITS RANK AMONG THE LEGAL
      *    IDEAS OF FLOOR 1 (0 IF IT IS A FORBIDDEN SILLINESS)
       RECOGNIZE-THE-MOVE.
           MOVE 0 TO THE-K
           MOVE THE-MOVE-TEXT(1:2) TO THE-SQUARE-TEXT
           PERFORM DECIPHER-A-SQUARE
           MOVE THE-SQUARE TO U-FROM
           MOVE THE-MOVE-TEXT(3:2) TO THE-SQUARE-TEXT
           PERFORM DECIPHER-A-SQUARE
           MOVE THE-SQUARE TO U-TO
           EVALUATE FUNCTION LOWER-CASE(THE-MOVE-TEXT(5:1))
               WHEN "q" MOVE 5 TO U-PROMO
               WHEN "r" MOVE 4 TO U-PROMO
               WHEN "b" MOVE 3 TO U-PROMO
               WHEN "n" MOVE 2 TO U-PROMO
               WHEN OTHER MOVE 0 TO U-PROMO
           END-EVALUATE
           IF U-FROM = 0 OR U-TO = 0
               EXIT PARAGRAPH
           END-IF
           MOVE 1 TO L-FLOOR
           PERFORM WEED-OUT-THE-SUICIDES
           PERFORM VARYING U-I FROM 1 BY 1
                   UNTIL U-I > NB-IDEAS(1) OR THE-K > 0
               IF I-FROM(1, U-I) = U-FROM AND I-TO(1, U-I) = U-TO
                  AND I-PROMO(1, U-I) = U-PROMO
                   MOVE U-I TO THE-K
               END-IF
           END-PERFORM.

      *================================================================
      * go: THE MOMENT OF GLORY
      *================================================================
       DO-ANYTHING-WITH-CONFIDENCE.
           IF THE-WORD(2) = "perft"
               PERFORM COUNT-THE-SHEEP
               GO TO CRY-IN-A-CORNER
           END-IF
           MOVE 1 TO L-FLOOR
           PERFORM WEED-OUT-THE-SUICIDES
           PERFORM LICK-A-FINGER-AND-GUESS
               VARYING V-K FROM 1 BY 1 UNTIL V-K > NB-IDEAS(1)
           PERFORM ROLL-THE-DIE-IN-THE-DARK
           PERFORM MAKE-SURE-IT-IS-BAD
           IF NB-RANKED = 0
               MOVE "0000" TO MY-WORST-IDEA
           ELSE
               MOVE RANKED-K(1) TO V-K
               MOVE 1 TO W-FLOOR
               MOVE V-K TO W-K
               PERFORM WRITE-THE-MOVE
               MOVE THE-WRITTEN-MOVE TO MY-WORST-IDEA
               MOVE WHAT-I-SAW(V-K) TO WHAT-I-SEE
               MOVE KING-MOOD(V-K) TO THE-GUY-WITH-THE-CROWN
               IF OOH-SOMETHING-SHINY
                   DISPLAY "info string I TAKE!"
               END-IF
               IF HE-IS-TOTALLY-BROKEN
                   DISPLAY "info string I DID THAT ON PURPOSE."
               END-IF
           END-IF
           PERFORM WORK-OUT-SCRATCHING-TIME
           IF THINKING-FOREVER
               SET WAITING-FOR-STOP TO TRUE
               GO TO CRY-IN-A-CORNER
           END-IF
           PERFORM SCRATCH-CHIN
              THE-SCRATCHING-TIME TIMES
           DISPLAY "bestmove " FUNCTION TRIM(MY-WORST-IDEA)
           GO TO CRY-IN-A-CORNER.

       CRY-IN-A-CORNER.
           EXIT.

      *    THE WEIGHT OF IDEA V-K: THE BIGGER IT IS, THE MORE LIKELY
      *    THE IDEA IS TO BE PLAYED. THAT IS ALL THE ENGINE'S BRAINS.
       LICK-A-FINGER-AND-GUESS.
           MOVE 1 TO EXCITEMENT-LEVEL
           MOVE "N" TO WHAT-I-SEE
           SET HE-IS-CHILL TO TRUE
           MOVE I-TO(1, V-K) TO V-TO
           IF THINGY(1, V-TO) NOT = 0
      *        "A FREE PIECE!" (WHETHER IT IS DEFENDED OR NOT)
               MOVE "O" TO WHAT-I-SEE
           END-IF
           MOVE 1 TO J-FLOOR
           MOVE V-K TO J-K
           PERFORM PUSH-THE-THINGY
           MOVE 2 TO E-FLOOR
           PERFORM IS-HE-SHAKING
           IF HE-IS-SHAKING
               SET HE-LOOKS-ANNOYED TO TRUE
               MOVE 2 TO L-FLOOR
               PERFORM WEED-OUT-THE-SUICIDES
               IF NB-IDEAS(2) = 0
                   SET HE-IS-TOTALLY-BROKEN TO TRUE
               END-IF
           END-IF
           MOVE I-PROMO(1, V-K) TO THE-DREAM-PROMOTION
           IF STYLE-SILLY
               IF OOH-SOMETHING-SHINY
                  MULTIPLY 3 BY EXCITEMENT-LEVEL
               END-IF
               IF HE-LOOKS-ANNOYED
                  OR HE-IS-TOTALLY-BROKEN
                  MULTIPLY 2 BY EXCITEMENT-LEVEL
               END-IF
               IF HE-IS-TOTALLY-BROKEN
      *           EVEN SHUFFLE OFTEN SPOTS A MATE IN ONE
                  MULTIPLY 40 BY EXCITEMENT-LEVEL
               END-IF
               EVALUATE TRUE
                   WHEN IT-WANTS-A-QUEEN
                       MULTIPLY 4 BY EXCITEMENT-LEVEL
                   WHEN A-LITTLE-HORSE
      *                AN UNDERPROMOTION NOW AND THEN, FOR STYLE
                       MULTIPLY 0.6 BY EXCITEMENT-LEVEL
                   WHEN SOME-OTHER-UNDERPROMOTION
                       MULTIPLY 0.3 BY EXCITEMENT-LEVEL
               END-EVALUATE
           END-IF
           MOVE EXCITEMENT-LEVEL TO EXCITEMENT(V-K)
           MOVE WHAT-I-SEE TO WHAT-I-SAW(V-K)
           MOVE THE-GUY-WITH-THE-CROWN TO KING-MOOD(V-K).

      *    WEIGHTED DRAW WITHOUT REPLACEMENT: THE FIRST ONE DRAWN IS
      *    PLAYED, THE NEXT ONES MAKE "ANALYSIS" LINES (MultiPV).
       ROLL-THE-DIE-IN-THE-DARK.
           MOVE NB-IDEAS(1) TO B-NB
           PERFORM VARYING B-I FROM 1 BY 1 UNTIL B-I > B-NB
               MOVE B-I TO B-K(B-I)
               MOVE EXCITEMENT(B-I) TO B-W(B-I)
           END-PERFORM
           MOVE 0 TO NB-RANKED
           PERFORM UNTIL B-NB = 0 OR NB-RANKED >= THE-MULTIPV
               MOVE 0 TO THE-TOTAL
               PERFORM VARYING B-I FROM 1 BY 1 UNTIL B-I > B-NB
                   ADD B-W(B-I) TO THE-TOTAL
               END-PERFORM
               COMPUTE THE-LUCK = FUNCTION RANDOM
               COMPUTE THE-TARGET = THE-LUCK * THE-TOTAL
               MOVE B-NB TO B-PICK
               PERFORM VARYING B-I FROM 1 BY 1 UNTIL B-I > B-NB
                   IF THE-TARGET < B-W(B-I)
                       MOVE B-I TO B-PICK
                       EXIT PERFORM
                   END-IF
                   SUBTRACT B-W(B-I) FROM THE-TARGET
               END-PERFORM
               ADD 1 TO NB-RANKED
               MOVE B-K(B-PICK) TO RANKED-K(NB-RANKED)
               MOVE B-K(B-NB) TO B-K(B-PICK)
               MOVE B-W(B-NB) TO B-W(B-PICK)
               SUBTRACT 1 FROM B-NB
           END-PERFORM.

      *    THE "ANALYSIS" LINES: THE SCORE IS JUST THE MATERIAL AFTER
      *    THE MOVE. WE DOUBLE-CHECK THAT IT IS BAD.
       MAKE-SURE-IT-IS-BAD.
           PERFORM VARYING N-I FROM 1 BY 1 UNTIL N-I > NB-RANKED
               MOVE RANKED-K(N-I) TO V-K
               MOVE SPACES TO THE-SCORE
               IF KING-MOOD(V-K) = "M"
                   MOVE "mate 1" TO THE-SCORE
               ELSE
                   MOVE 1 TO J-FLOOR
                   MOVE V-K TO J-K
                   PERFORM PUSH-THE-THINGY
                   MOVE 2 TO M-FLOOR
                   PERFORM WEIGH-THE-LOOT
                   COMPUTE PRETTY-NUMBER = 0 - M-TOTAL
                   STRING "cp " FUNCTION TRIM(PRETTY-NUMBER)
                       DELIMITED BY SIZE INTO THE-SCORE
                   END-STRING
               END-IF
               MOVE 1 TO W-FLOOR
               MOVE V-K TO W-K
               PERFORM WRITE-THE-MOVE
               MOVE N-I TO PRETTY-1
               MOVE NB-IDEAS(1) TO PRETTY-2
               DISPLAY "info depth 1 seldepth 1 multipv "
                   FUNCTION TRIM(PRETTY-1)
                   " score " FUNCTION TRIM(THE-SCORE)
                   " nodes " FUNCTION TRIM(PRETTY-2)
                   " nps 0 time 0 pv "
                   FUNCTION TRIM(THE-WRITTEN-MOVE)
           END-PERFORM
           IF NB-IDEAS(1) = 0
               MOVE 1 TO E-FLOOR
               PERFORM IS-HE-SHAKING
               IF HE-IS-SHAKING
                   DISPLAY "info depth 0 score mate 0"
               ELSE
                   DISPLAY "info depth 0 score cp 0"
               END-IF
           END-IF.

      *    THE FAKE THINKING TIME: Move Delay, WITHOUT GOING OVER
      *    movetime OR 1/40 OF THE CLOCK. depth OR nodes: NONE AT ALL.
       WORK-OUT-SCRATCHING-TIME.
           MOVE THE-DELAY TO THE-SCRATCHING-TIME
           MOVE "N" TO THE-THOUGHT THE-LAZINESS
           PERFORM VARYING T-I FROM 2 BY 1 UNTIL T-I > NB-WORDS
               MOVE THE-WORD(T-I + 1) TO THE-VALUE
               PERFORM DECIPHER-THE-VALUE
               IF THE-NUMBER < 0
                   MOVE 0 TO THE-NUMBER
               END-IF
               EVALUATE THE-WORD(T-I)
                   WHEN "movetime"
                       IF IT-IS-A-NUMBER
                          AND THE-NUMBER < THE-SCRATCHING-TIME
                           MOVE THE-NUMBER TO THE-SCRATCHING-TIME
                       END-IF
                   WHEN "wtime"
                       IF IT-IS-A-NUMBER AND WHOSE-TURN-IT-IS(1) = 1
                          AND THE-NUMBER / 40 < THE-SCRATCHING-TIME
                           COMPUTE THE-SCRATCHING-TIME =
                               THE-NUMBER / 40
                       END-IF
                   WHEN "btime"
                       IF IT-IS-A-NUMBER AND WHOSE-TURN-IT-IS(1) = -1
                          AND THE-NUMBER / 40 < THE-SCRATCHING-TIME
                           COMPUTE THE-SCRATCHING-TIME =
                               THE-NUMBER / 40
                       END-IF
                   WHEN "depth"
                   WHEN "nodes"
                       IF IT-IS-A-NUMBER
                           SET NOT-THINKING-AT-ALL TO TRUE
                       END-IF
                   WHEN "infinite"
                   WHEN "ponder"
                       SET THINKING-FOREVER TO TRUE
               END-EVALUATE
           END-PERFORM
           IF NOT-THINKING-AT-ALL
               MOVE 0 TO THE-SCRATCHING-TIME
           END-IF.

      *    ONE MILLISECOND OF DEEP THOUGHT. REPEAT AS NEEDED.
       SCRATCH-CHIN.
           CALL "CBL_GC_NANOSLEEP" USING ONE-MILLISECOND
           END-CALL.

       SPIT-OUT-THE-PENDING-MOVE.
           IF WAITING-FOR-STOP
               DISPLAY "bestmove " FUNCTION TRIM(MY-WORST-IDEA)
               SET WAITING-FOR-NOTHING TO TRUE
           END-IF.

      *================================================================
      * go perft N: COUNTING SHEEP (AND CHECKING THE RULES)
      *================================================================
       COUNT-THE-SHEEP.
           MOVE 1 TO THE-DEPTH
           MOVE THE-WORD(3) TO THE-VALUE
           PERFORM DECIPHER-THE-VALUE
           IF IT-IS-A-NUMBER AND THE-NUMBER >= 0
               IF THE-NUMBER > 6
                   DISPLAY "info string perft capped at 6: "
                       "I am COBOL, patient but not crazy"
                   MOVE 6 TO THE-DEPTH
               ELSE
                   MOVE THE-NUMBER TO THE-DEPTH
               END-IF
           END-IF
           PERFORM LOOK-AT-THE-CLOCK
           MOVE THE-TICK TO THE-START-TICK
           MOVE 1 TO L-FLOOR
           PERFORM WEED-OUT-THE-SUICIDES
           EVALUATE THE-DEPTH
               WHEN 0
                   MOVE 1 TO THE-SHEEP
               WHEN 1
                   MOVE NB-IDEAS(1) TO THE-SHEEP
               WHEN OTHER
                   MOVE 0 TO THE-SHEEP
                   MOVE 1 TO P-CURRENT
                   MOVE 0 TO P-K(1)
                   PERFORM UNTIL P-CURRENT = 0
                       ADD 1 TO P-K(P-CURRENT)
                       IF P-K(P-CURRENT) > NB-IDEAS(P-CURRENT)
                           SUBTRACT 1 FROM P-CURRENT
                       ELSE
                           MOVE P-CURRENT TO J-FLOOR
                           MOVE P-K(P-CURRENT) TO J-K
                           PERFORM PUSH-THE-THINGY
                           COMPUTE L-FLOOR = P-CURRENT + 1
                           PERFORM WEED-OUT-THE-SUICIDES
                           IF THE-DEPTH - P-CURRENT = 1
                               ADD NB-IDEAS(L-FLOOR) TO THE-SHEEP
                           ELSE
                               ADD 1 TO P-CURRENT
                               MOVE 0 TO P-K(P-CURRENT)
                           END-IF
                       END-IF
                   END-PERFORM
           END-EVALUATE
           PERFORM LOOK-AT-THE-CLOCK
           COMPUTE THE-DURATION = THE-TICK - THE-START-TICK
           IF THE-DURATION < 0
               ADD 86400000 TO THE-DURATION
           END-IF
           MOVE THE-DEPTH TO PRETTY-1
           MOVE THE-DURATION TO PRETTY-2
           MOVE THE-SHEEP TO PRETTY-3
           DISPLAY "info string perft " FUNCTION TRIM(PRETTY-1)
               ": " FUNCTION TRIM(PRETTY-3) " nodes in "
               FUNCTION TRIM(PRETTY-2) " ms".

       LOOK-AT-THE-CLOCK.
           MOVE FUNCTION CURRENT-DATE TO TODAYS-DATE
           COMPUTE THE-TICK =
               ((THE-HOURS * 60 + THE-MINUTES) * 60 + THE-SECONDS)
               * 1000 + THE-HUNDREDTHS * 10.

      *================================================================
      * d: THE CARNAGE, SEEN FROM ABOVE
      *================================================================
       SHOW-THE-CARNAGE.
           PERFORM VARYING D-R FROM 7 BY -1 UNTIL D-R < 0
               MOVE SPACES TO THE-ROW
               MOVE THE-DIGITS(D-R + 1:1) TO THE-ROW(1:1)
               PERFORM VARYING D-F FROM 0 BY 1 UNTIL D-F > 7
                   COMPUTE D-S = D-R * 8 + D-F + 1
                   MOVE THINGY(1, D-S) TO D-P
                   IF D-P < 0
                       COMPUTE D-POS = 1 - D-P
                   ELSE
                       COMPUTE D-POS = 1 + D-P
                   END-IF
                   MOVE THE-DRAWINGS(D-POS:1) TO D-C
                   IF D-P > 0
                       MOVE FUNCTION UPPER-CASE(D-C) TO D-C
                   END-IF
                   COMPUTE D-POS = D-F * 2 + 3
                   MOVE D-C TO THE-ROW(D-POS:1)
               END-PERFORM
               DISPLAY FUNCTION TRIM(THE-ROW TRAILING)
           END-PERFORM
           DISPLAY "  a b c d e f g h"
           IF WHOSE-TURN-IT-IS(1) = 1
               DISPLAY "Side to move: White"
           ELSE
               DISPLAY "Side to move: Black"
           END-IF.

      *================================================================
      * THE RULES OF THE GAME, OR WHAT IS LEFT OF THEM
      *================================================================

      *    X-SQ (1..64) -> FILE X-F AND RANK X-R (0..7)
       WHERE-AM-I.
           COMPUTE X-T = X-SQ - 1
           DIVIDE X-T BY 8 GIVING X-R REMAINDER X-F.

      *    ALL THE IDEAS OF FLOOR G-FLOOR, INCLUDING THE SUICIDAL ONES
      *    (THOSE THAT LEAVE THE KING IN CHECK)
       IMAGINE-SILLY-THINGS.
           MOVE 0 TO NB-IDEAS(G-FLOOR)
           MOVE WHOSE-TURN-IT-IS(G-FLOOR) TO G-US
           PERFORM VARYING G-SQ FROM 1 BY 1 UNTIL G-SQ > 64
               MOVE THINGY(G-FLOOR, G-SQ) TO G-PIECE
               COMPUTE G-TYPE = G-PIECE * G-US
               IF G-TYPE > 0
                   MOVE G-SQ TO X-SQ
                   PERFORM WHERE-AM-I
                   MOVE X-F TO G-F
                   MOVE X-R TO G-R
                   MOVE 0 TO G-PROMO
                   EVALUATE G-TYPE
                       WHEN 1
                           PERFORM PUSH-A-PAWN-AROUND
                       WHEN 2
                           PERFORM MAKE-THE-HORSE-JUMP
                       WHEN 6
                           PERFORM TAKE-THE-KING-FOR-A-WALK
                       WHEN OTHER
                           PERFORM SLIDE-THE-THING
                   END-EVALUATE
               END-IF
           END-PERFORM
           PERFORM TRY-TO-HIDE.

       JOT-DOWN-THE-IDEA.
           IF NB-IDEAS(G-FLOOR) < 256
               ADD 1 TO NB-IDEAS(G-FLOOR)
               MOVE NB-IDEAS(G-FLOOR) TO G-N
               MOVE G-SQ TO I-FROM(G-FLOOR, G-N)
               MOVE G-TARGET TO I-TO(G-FLOOR, G-N)
               MOVE G-PROMO TO I-PROMO(G-FLOOR, G-N)
           END-IF.

      *    A PAWN THAT REACHES THE END CHOOSES ITS COSTUME
       JOT-DOWN-A-PAWN-IDEA.
           COMPUTE G-T0 = G-TARGET - 1
           DIVIDE G-T0 BY 8 GIVING G-RC REMAINDER G-T1
           IF (G-US = 1 AND G-RC = 7) OR (G-US = -1 AND G-RC = 0)
               PERFORM VARYING G-PROMO FROM 5 BY -1 UNTIL G-PROMO < 2
                   PERFORM JOT-DOWN-THE-IDEA
               END-PERFORM
           ELSE
               MOVE 0 TO G-PROMO
               PERFORM JOT-DOWN-THE-IDEA
           END-IF
           MOVE 0 TO G-PROMO.

       PUSH-A-PAWN-AROUND.
           COMPUTE G-R1 = G-R + G-US
           IF G-R1 < 0 OR G-R1 > 7
               EXIT PARAGRAPH
           END-IF
           COMPUTE G-TARGET = G-R1 * 8 + G-F + 1
           IF THINGY(G-FLOOR, G-TARGET) = 0
               PERFORM JOT-DOWN-A-PAWN-IDEA
               IF (G-US = 1 AND G-R = 1) OR (G-US = -1 AND G-R = 6)
                   COMPUTE G-TARGET = (G-R + 2 * G-US) * 8 + G-F + 1
                   IF THINGY(G-FLOOR, G-TARGET) = 0
                       PERFORM JOT-DOWN-THE-IDEA
                   END-IF
               END-IF
           END-IF
           PERFORM VARYING G-DF FROM -1 BY 2 UNTIL G-DF > 1
               COMPUTE G-F1 = G-F + G-DF
               IF G-F1 >= 0 AND G-F1 <= 7
                   COMPUTE G-TARGET = G-R1 * 8 + G-F1 + 1
                   COMPUTE G-CONTENT =
                       THINGY(G-FLOOR, G-TARGET) * G-US
                   IF G-CONTENT < 0
                      OR THE-GHOST-PAWN(G-FLOOR) = G-TARGET
                       PERFORM JOT-DOWN-A-PAWN-IDEA
                   END-IF
               END-IF
           END-PERFORM.

       MAKE-THE-HORSE-JUMP.
           PERFORM VARYING G-DIR FROM 1 BY 1 UNTIL G-DIR > 8
               COMPUTE G-F1 = G-F + HOP-F(G-DIR) - 2
               COMPUTE G-R1 = G-R + HOP-R(G-DIR) - 2
               IF G-F1 >= 0 AND G-F1 <= 7 AND G-R1 >= 0 AND G-R1 <= 7
                   COMPUTE G-TARGET = G-R1 * 8 + G-F1 + 1
                   COMPUTE G-CONTENT =
                       THINGY(G-FLOOR, G-TARGET) * G-US
                   IF G-CONTENT <= 0
                       PERFORM JOT-DOWN-THE-IDEA
                   END-IF
               END-IF
           END-PERFORM.

       TAKE-THE-KING-FOR-A-WALK.
           PERFORM VARYING G-DIR FROM 1 BY 1 UNTIL G-DIR > 8
               COMPUTE G-F1 = G-F + WIND-F(G-DIR) - 2
               COMPUTE G-R1 = G-R + WIND-R(G-DIR) - 2
               IF G-F1 >= 0 AND G-F1 <= 7 AND G-R1 >= 0 AND G-R1 <= 7
                   COMPUTE G-TARGET = G-R1 * 8 + G-F1 + 1
                   COMPUTE G-CONTENT =
                       THINGY(G-FLOOR, G-TARGET) * G-US
                   IF G-CONTENT <= 0
                       PERFORM JOT-DOWN-THE-IDEA
                   END-IF
               END-IF
           END-PERFORM.

      *    BISHOP (DIAGONALS), ROOK (STRAIGHTS), QUEEN (ALL OF THEM,
      *    SHE DARES)
       SLIDE-THE-THING.
           EVALUATE G-TYPE
               WHEN 3
                   MOVE 1 TO G-FIRST
                   MOVE 4 TO G-LAST
               WHEN 4
                   MOVE 5 TO G-FIRST
                   MOVE 8 TO G-LAST
               WHEN OTHER
                   MOVE 1 TO G-FIRST
                   MOVE 8 TO G-LAST
           END-EVALUATE
           PERFORM VARYING G-DIR FROM G-FIRST BY 1
                   UNTIL G-DIR > G-LAST
               COMPUTE G-F1 = G-F + WIND-F(G-DIR) - 2
               COMPUTE G-R1 = G-R + WIND-R(G-DIR) - 2
               PERFORM UNTIL G-F1 < 0 OR G-F1 > 7
                          OR G-R1 < 0 OR G-R1 > 7
                   COMPUTE G-TARGET = G-R1 * 8 + G-F1 + 1
                   COMPUTE G-CONTENT =
                       THINGY(G-FLOOR, G-TARGET) * G-US
                   IF G-CONTENT = 0
                       PERFORM JOT-DOWN-THE-IDEA
                       COMPUTE G-F1 = G-F1 + WIND-F(G-DIR) - 2
                       COMPUTE G-R1 = G-R1 + WIND-R(G-DIR) - 2
                   ELSE
                       IF G-CONTENT < 0
                           PERFORM JOT-DOWN-THE-IDEA
                       END-IF
                       MOVE -1 TO G-F1
                   END-IF
               END-PERFORM
           END-PERFORM.

      *    CASTLING: THE KING HIDES BEHIND HIS ROOK
       TRY-TO-HIDE.
           MOVE 0 TO G-PROMO
           IF G-US = 1
               MOVE 0 TO G-BASE
               MOVE 1 TO G-DK
               MOVE 2 TO G-DQ
           ELSE
               MOVE 56 TO G-BASE
               MOVE 3 TO G-DK
               MOVE 4 TO G-DQ
           END-IF
           COMPUTE G-SQ = G-BASE + 5
           COMPUTE G-T0 = 6 * G-US
           COMPUTE G-T1 = 4 * G-US
           IF THINGY(G-FLOOR, G-SQ) NOT = G-T0
               EXIT PARAGRAPH
           END-IF
           IF RIGHT-TO-HIDE(G-FLOOR, G-DK) = 0
              AND RIGHT-TO-HIDE(G-FLOOR, G-DQ) = 0
               EXIT PARAGRAPH
           END-IF
           MOVE G-FLOOR TO A-FLOOR
           COMPUTE A-BY = 0 - G-US
           MOVE G-SQ TO A-SQ
           PERFORM WHO-IS-GIVING-ME-THE-EYE
           IF SOMEONE-IS-STARING
               EXIT PARAGRAPH
           END-IF
           IF RIGHT-TO-HIDE(G-FLOOR, G-DK) = 1
              AND THINGY(G-FLOOR, G-BASE + 8) = G-T1
              AND THINGY(G-FLOOR, G-BASE + 6) = 0
              AND THINGY(G-FLOOR, G-BASE + 7) = 0
               COMPUTE A-SQ = G-BASE + 6
               PERFORM WHO-IS-GIVING-ME-THE-EYE
               IF NOT SOMEONE-IS-STARING
                   COMPUTE A-SQ = G-BASE + 7
                   PERFORM WHO-IS-GIVING-ME-THE-EYE
                   IF NOT SOMEONE-IS-STARING
                       COMPUTE G-TARGET = G-BASE + 7
                       PERFORM JOT-DOWN-THE-IDEA
                   END-IF
               END-IF
           END-IF
           IF RIGHT-TO-HIDE(G-FLOOR, G-DQ) = 1
              AND THINGY(G-FLOOR, G-BASE + 1) = G-T1
              AND THINGY(G-FLOOR, G-BASE + 2) = 0
              AND THINGY(G-FLOOR, G-BASE + 3) = 0
              AND THINGY(G-FLOOR, G-BASE + 4) = 0
               COMPUTE A-SQ = G-BASE + 4
               PERFORM WHO-IS-GIVING-ME-THE-EYE
               IF NOT SOMEONE-IS-STARING
                   COMPUTE A-SQ = G-BASE + 3
                   PERFORM WHO-IS-GIVING-ME-THE-EYE
                   IF NOT SOMEONE-IS-STARING
                       COMPUTE G-TARGET = G-BASE + 3
                       PERFORM JOT-DOWN-THE-IDEA
                   END-IF
               END-IF
           END-IF.

      *    IS SQUARE A-SQ OF FLOOR A-FLOOR ATTACKED BY SIDE A-BY
      *    (1 WHITE, -1 BLACK)?
       WHO-IS-GIVING-ME-THE-EYE.
           MOVE "N" TO A-VERDICT
           MOVE A-SQ TO X-SQ
           PERFORM WHERE-AM-I
           MOVE X-F TO A-F
           MOVE X-R TO A-R
      *    THE PAWNS (WHITE ATTACKS UPWARDS)
           COMPUTE A-R1 = A-R - A-BY
           IF A-R1 >= 0 AND A-R1 <= 7
               PERFORM VARYING A-DIR FROM -1 BY 2 UNTIL A-DIR > 1
                   COMPUTE A-F1 = A-F + A-DIR
                   IF A-F1 >= 0 AND A-F1 <= 7
                       COMPUTE A-TARGET = A-R1 * 8 + A-F1 + 1
                       IF THINGY(A-FLOOR, A-TARGET) = A-BY
                           SET SOMEONE-IS-STARING TO TRUE
                       END-IF
                   END-IF
               END-PERFORM
           END-IF
           IF SOMEONE-IS-STARING
               EXIT PARAGRAPH
           END-IF
      *    THE KNIGHTS
           COMPUTE A-T = 2 * A-BY
           PERFORM VARYING A-DIR FROM 1 BY 1 UNTIL A-DIR > 8
               COMPUTE A-F1 = A-F + HOP-F(A-DIR) - 2
               COMPUTE A-R1 = A-R + HOP-R(A-DIR) - 2
               IF A-F1 >= 0 AND A-F1 <= 7 AND A-R1 >= 0 AND A-R1 <= 7
                   COMPUTE A-TARGET = A-R1 * 8 + A-F1 + 1
                   IF THINGY(A-FLOOR, A-TARGET) = A-T
                       SET SOMEONE-IS-STARING TO TRUE
                   END-IF
               END-IF
           END-PERFORM
           IF SOMEONE-IS-STARING
               EXIT PARAGRAPH
           END-IF
      *    THE KING ACROSS THE STREET
           COMPUTE A-T = 6 * A-BY
           PERFORM VARYING A-DIR FROM 1 BY 1 UNTIL A-DIR > 8
               COMPUTE A-F1 = A-F + WIND-F(A-DIR) - 2
               COMPUTE A-R1 = A-R + WIND-R(A-DIR) - 2
               IF A-F1 >= 0 AND A-F1 <= 7 AND A-R1 >= 0 AND A-R1 <= 7
                   COMPUTE A-TARGET = A-R1 * 8 + A-F1 + 1
                   IF THINGY(A-FLOOR, A-TARGET) = A-T
                       SET SOMEONE-IS-STARING TO TRUE
                   END-IF
               END-IF
           END-PERFORM
           IF SOMEONE-IS-STARING
               EXIT PARAGRAPH
           END-IF
      *    THE SLIDERS: BISHOP OR QUEEN ON THE DIAGONALS, ROOK OR
      *    QUEEN STRAIGHT AHEAD
           COMPUTE A-Q = 5 * A-BY
           PERFORM VARYING A-DIR FROM 1 BY 1
                   UNTIL A-DIR > 8 OR SOMEONE-IS-STARING
               IF A-DIR <= 4
                   COMPUTE A-T = 3 * A-BY
               ELSE
                   COMPUTE A-T = 4 * A-BY
               END-IF
               COMPUTE A-F1 = A-F + WIND-F(A-DIR) - 2
               COMPUTE A-R1 = A-R + WIND-R(A-DIR) - 2
               PERFORM UNTIL A-F1 < 0 OR A-F1 > 7
                          OR A-R1 < 0 OR A-R1 > 7
                   COMPUTE A-TARGET = A-R1 * 8 + A-F1 + 1
                   MOVE THINGY(A-FLOOR, A-TARGET) TO A-P
                   IF A-P = 0
                       COMPUTE A-F1 = A-F1 + WIND-F(A-DIR) - 2
                       COMPUTE A-R1 = A-R1 + WIND-R(A-DIR) - 2
                   ELSE
                       IF A-P = A-T OR A-P = A-Q
                           SET SOMEONE-IS-STARING TO TRUE
                       END-IF
                       MOVE -1 TO A-F1
                   END-IF
               END-PERFORM
           END-PERFORM.

      *    PLAYS IDEA J-K OF FLOOR J-FLOOR; THE RESULT LANDS ON THE
      *    FLOOR ABOVE.
       PUSH-THE-THINGY.
           COMPUTE J-NEXT = J-FLOOR + 1
           MOVE ITS-STATE(J-FLOOR) TO ITS-STATE(J-NEXT)
           MOVE I-FROM(J-FLOOR, J-K) TO J-FROM
           MOVE I-TO(J-FLOOR, J-K) TO J-TO
           MOVE I-PROMO(J-FLOOR, J-K) TO J-PROMO
           MOVE THINGY(J-NEXT, J-FROM) TO J-PIECE
           IF J-PIECE > 0
               MOVE 1 TO J-SIGN
           ELSE
               MOVE -1 TO J-SIGN
           END-IF
           COMPUTE J-TYPE = J-PIECE * J-SIGN
           IF THE-BOREDOM(J-NEXT) < 9999
               ADD 1 TO THE-BOREDOM(J-NEXT)
           END-IF
           IF J-TYPE = 1 OR THINGY(J-NEXT, J-TO) NOT = 0
               MOVE 0 TO THE-BOREDOM(J-NEXT)
           END-IF
           IF J-TYPE = 1 AND THE-GHOST-PAWN(J-NEXT) = J-TO
      *        EN PASSANT: THE EATEN PAWN IS BESIDE US, NOT UNDERNEATH
               MOVE J-FROM TO X-SQ
               PERFORM WHERE-AM-I
               MOVE X-R TO J-RFROM
               MOVE J-TO TO X-SQ
               PERFORM WHERE-AM-I
               COMPUTE J-X = J-RFROM * 8 + X-F + 1
               MOVE 0 TO THINGY(J-NEXT, J-X)
           END-IF
           MOVE 0 TO THE-GHOST-PAWN(J-NEXT)
           COMPUTE J-X = J-TO - J-FROM
           IF J-TYPE = 1 AND (J-X = 16 OR J-X = -16)
               COMPUTE THE-GHOST-PAWN(J-NEXT) = (J-FROM + J-TO) / 2
           END-IF
           IF J-TYPE = 6
               MOVE J-FROM TO X-SQ
               PERFORM WHERE-AM-I
               MOVE X-F TO J-FFROM
               MOVE X-R TO J-RFROM
               MOVE J-TO TO X-SQ
               PERFORM WHERE-AM-I
               COMPUTE J-X = X-F - J-FFROM
               IF J-X = 2 OR J-X = -2
      *            THE ROOK LEAPS OVER THE KING, LIKE AT A PARADE
                   COMPUTE J-BASE = J-RFROM * 8
                   IF X-F = 6
                       MOVE THINGY(J-NEXT, J-BASE + 8)
                         TO THINGY(J-NEXT, J-BASE + 6)
                       MOVE 0 TO THINGY(J-NEXT, J-BASE + 8)
                   ELSE
                       MOVE THINGY(J-NEXT, J-BASE + 1)
                         TO THINGY(J-NEXT, J-BASE + 4)
                       MOVE 0 TO THINGY(J-NEXT, J-BASE + 1)
                   END-IF
               END-IF
           END-IF
           IF J-PROMO NOT = 0
               COMPUTE THINGY(J-NEXT, J-TO) = J-PROMO * J-SIGN
           ELSE
               MOVE J-PIECE TO THINGY(J-NEXT, J-TO)
           END-IF
           MOVE 0 TO THINGY(J-NEXT, J-FROM)
           MOVE J-FROM TO J-X
           PERFORM REVOKE-THE-RIGHTS
           MOVE J-TO TO J-X
           PERFORM REVOKE-THE-RIGHTS
           IF WHOSE-TURN-IT-IS(J-NEXT) = -1
              AND THE-CAPTAINS-AGE(J-NEXT) < 9999
               ADD 1 TO THE-CAPTAINS-AGE(J-NEXT)
           END-IF
           COMPUTE WHOSE-TURN-IT-IS(J-NEXT) =
               0 - WHOSE-TURN-IT-IS(J-NEXT).

      *    WHOEVER TOUCHES A CORNER OR A KING LOSES THE RIGHT TO CASTLE
       REVOKE-THE-RIGHTS.
           EVALUATE J-X
               WHEN 1
                   MOVE 0 TO RIGHT-TO-HIDE(J-NEXT, 2)
               WHEN 8
                   MOVE 0 TO RIGHT-TO-HIDE(J-NEXT, 1)
               WHEN 5
                   MOVE 0 TO RIGHT-TO-HIDE(J-NEXT, 1)
                   MOVE 0 TO RIGHT-TO-HIDE(J-NEXT, 2)
               WHEN 57
                   MOVE 0 TO RIGHT-TO-HIDE(J-NEXT, 4)
               WHEN 64
                   MOVE 0 TO RIGHT-TO-HIDE(J-NEXT, 3)
               WHEN 61
                   MOVE 0 TO RIGHT-TO-HIDE(J-NEXT, 3)
                   MOVE 0 TO RIGHT-TO-HIDE(J-NEXT, 4)
           END-EVALUATE.

      *    THE SQUARE OF THE K-COLOR KING ON FLOOR K-FLOOR
       FIND-THE-GUY-WITH-THE-CROWN.
           MOVE 0 TO K-SQ
           COMPUTE K-KING = 6 * K-COLOR
           PERFORM VARYING K-S FROM 1 BY 1
                   UNTIL K-S > 64 OR K-SQ > 0
               IF THINGY(K-FLOOR, K-S) = K-KING
                   MOVE K-S TO K-SQ
               END-IF
           END-PERFORM.

      *    IS THE SIDE TO MOVE ON FLOOR E-FLOOR IN CHECK?
       IS-HE-SHAKING.
           MOVE "N" TO E-VERDICT
           MOVE E-FLOOR TO K-FLOOR
           MOVE WHOSE-TURN-IT-IS(E-FLOOR) TO K-COLOR
           PERFORM FIND-THE-GUY-WITH-THE-CROWN
           IF K-SQ > 0
               MOVE E-FLOOR TO A-FLOOR
               MOVE K-SQ TO A-SQ
               COMPUTE A-BY = 0 - K-COLOR
               PERFORM WHO-IS-GIVING-ME-THE-EYE
               IF SOMEONE-IS-STARING
                   SET HE-IS-SHAKING TO TRUE
               END-IF
           END-IF.

      *    ON FLOOR L-FLOOR, KEEPS ONLY THE IDEAS THAT DO NOT LET OUR
      *    KING GET EATEN. EVEN SHUFFLE HAS ITS PRIDE.
       WEED-OUT-THE-SUICIDES.
           MOVE L-FLOOR TO G-FLOOR
           PERFORM IMAGINE-SILLY-THINGS
           COMPUTE L-NEXT = L-FLOOR + 1
           MOVE WHOSE-TURN-IT-IS(L-FLOOR) TO L-US
           MOVE 0 TO L-W
           PERFORM VARYING L-K FROM 1 BY 1
                   UNTIL L-K > NB-IDEAS(L-FLOOR)
               MOVE L-FLOOR TO J-FLOOR
               MOVE L-K TO J-K
               PERFORM PUSH-THE-THINGY
               MOVE L-NEXT TO K-FLOOR
               MOVE L-US TO K-COLOR
               PERFORM FIND-THE-GUY-WITH-THE-CROWN
               IF K-SQ > 0
                   MOVE L-NEXT TO A-FLOOR
                   MOVE K-SQ TO A-SQ
                   COMPUTE A-BY = 0 - L-US
                   PERFORM WHO-IS-GIVING-ME-THE-EYE
                   IF NOT SOMEONE-IS-STARING
                       ADD 1 TO L-W
                       MOVE AN-IDEA(L-FLOOR, L-K)
                         TO AN-IDEA(L-FLOOR, L-W)
                   END-IF
               END-IF
           END-PERFORM
           MOVE L-W TO NB-IDEAS(L-FLOOR).

      *    MATERIAL FROM THE POINT OF VIEW OF THE SIDE TO MOVE, IN
      *    CENTIPAWNS
       WEIGH-THE-LOOT.
           MOVE 0 TO M-TOTAL
           PERFORM VARYING M-S FROM 1 BY 1 UNTIL M-S > 64
               MOVE THINGY(M-FLOOR, M-S) TO M-P
               IF M-P > 0
                   COMPUTE M-I = M-P + 1
                   ADD A-PRICE(M-I) TO M-TOTAL
               END-IF
               IF M-P < 0
                   COMPUTE M-I = 1 - M-P
                   SUBTRACT A-PRICE(M-I) FROM M-TOTAL
               END-IF
           END-PERFORM
           COMPUTE M-TOTAL = M-TOTAL * WHOSE-TURN-IT-IS(M-FLOOR).

      *    IDEA W-K OF FLOOR W-FLOOR, IN UCI NOTATION ("e7e8q")
       WRITE-THE-MOVE.
           MOVE SPACES TO THE-WRITTEN-MOVE
           MOVE I-FROM(W-FLOOR, W-K) TO X-SQ
           PERFORM WHERE-AM-I
           MOVE THE-LETTERS(X-F + 1:1) TO THE-WRITTEN-MOVE(1:1)
           MOVE THE-DIGITS(X-R + 1:1) TO THE-WRITTEN-MOVE(2:1)
           MOVE I-TO(W-FLOOR, W-K) TO X-SQ
           PERFORM WHERE-AM-I
           MOVE THE-LETTERS(X-F + 1:1) TO THE-WRITTEN-MOVE(3:1)
           MOVE THE-DIGITS(X-R + 1:1) TO THE-WRITTEN-MOVE(4:1)
           IF I-PROMO(W-FLOOR, W-K) > 0
               MOVE I-PROMO(W-FLOOR, W-K) TO M-I
               MOVE THE-PROMOS(M-I + 1:1) TO THE-WRITTEN-MOVE(5:1)
           END-IF.

#include "openings.h"

#include "preferences.h"

#include <QFile>
#include <QHash>
#include <QMutex>
#include <QStringList>

const chess::eco::Book& openingsBook()
{
    static const chess::eco::Book book = [] {
        chess::eco::Book b;
        for (const char* f : {"a", "b", "c", "d", "e"}) {
            QFile file(QStringLiteral(":/eco/%1.tsv").arg(QLatin1String(f)));
            if (file.open(QIODevice::ReadOnly)) b.load(file.readAll().toStdString());
        }
        return b;
    }();
    return book;
}

QString openingLabel(const QString& eco)
{
    const std::string name = openingsBook().nameOf(eco.toStdString());
    return name.empty() ? eco : eco + QLatin1Char(' ') + openingName(name);
}

QString openingName(const std::string& english)
{
    const QString name = QString::fromStdString(english);
    if (prefs::language() != QLatin1String("fr")) return name;
    static QMutex mutex;
    static QHash<QString, QString> cache;
    const QMutexLocker lock(&mutex);
    auto it = cache.constFind(name);
    if (it == cache.constEnd()) it = cache.insert(name, frenchOpeningName(name));
    return *it;
}

// ---------------------------------------------------------------- French translation

namespace {

enum Gender { Masc, Fem };
struct Term {
    const char* fr;
    Gender g;
};

// Families (and a few groups): established French names.
const QHash<QString, Term>& families()
{
    static const QHash<QString, Term> map = [] {
        QHash<QString, Term> m;
        const struct {
            const char* en;
            Term t;
        } rows[] = {
            {"Alekhine Defense", {"Défense Alekhine", Fem}},
            {"Amar Opening", {"Ouverture Amar", Fem}},
            {"Amazon Attack", {"Attaque amazone", Fem}},
            {"Amsterdam Attack", {"Attaque d'Amsterdam", Fem}},
            {"Anderssen's Opening", {"Ouverture Anderssen", Fem}},
            {"Australian Defense", {"Défense australienne", Fem}},
            {"Basque Opening", {"Ouverture basque", Fem}},
            {"Bishop's Opening", {"Ouverture du fou", Fem}},
            {"Bogo-Indian Defense", {"Défense bogo-indienne", Fem}},
            {"Catalan Opening", {"Ouverture catalane", Fem}},
            {"Center Game", {"Partie du centre", Fem}},
            {"Creepy Crawly Formation", {"Formation Creepy Crawly", Fem}},
            {"Czech Defense", {"Défense tchèque", Fem}},
            {"Danish Gambit", {"Gambit danois", Masc}},
            {"Dresden Opening", {"Ouverture de Dresde", Fem}},
            {"Dutch Defense", {"Défense hollandaise", Fem}},
            {"East Indian Defense", {"Défense indienne de l'Est", Fem}},
            {"Elephant Gambit", {"Gambit de l'éléphant", Masc}},
            {"English Defense", {"Défense anglaise", Fem}},
            {"English Opening", {"Ouverture anglaise", Fem}},
            {"English Orangutan", {"Orang-outan anglais", Masc}},
            {"Four Knights Game", {"Partie des quatre cavaliers", Fem}},
            {"French Defense", {"Défense française", Fem}},
            {"Hippopotamus Defense", {"Défense hippopotame", Fem}},
            {"Hungarian Opening", {"Ouverture hongroise", Fem}},
            {"Indian Defense", {"Défense indienne", Fem}},
            {"Irish Gambit", {"Gambit irlandais", Masc}},
            {"Italian Game", {"Partie italienne", Fem}},
            {"Kangaroo Defense", {"Défense kangourou", Fem}},
            {"King's Gambit", {"Gambit du roi", Masc}},
            {"King's Indian Attack", {"Attaque est-indienne", Fem}},
            {"King's Indian Defense", {"Défense est-indienne", Fem}},
            {"King's Knight Opening", {"Ouverture du cavalier roi", Fem}},
            {"King's Pawn Game", {"Partie du pion roi", Fem}},
            {"King's Pawn Opening", {"Ouverture du pion roi", Fem}},
            {"Lasker Simul Special", {"Spéciale de simultanée Lasker", Fem}},
            {"Latvian Gambit", {"Gambit letton", Masc}},
            {"Lion Defense", {"Défense du lion", Fem}},
            {"London System", {"Système de Londres", Masc}},
            {"Marienbad System", {"Système de Marienbad", Masc}},
            {"Mexican Defense", {"Défense mexicaine", Fem}},
            {"Modern Defense", {"Défense moderne", Fem}},
            {"Neo-Grünfeld Defense", {"Défense néo-Grünfeld", Fem}},
            {"Nimzo-Indian Defense", {"Défense nimzo-indienne", Fem}},
            {"Old Indian Defense", {"Ancienne défense indienne", Fem}},
            {"Petrov's Defense", {"Défense Petrov", Fem}},
            {"Polish Defense", {"Défense polonaise", Fem}},
            {"Polish Opening", {"Ouverture polonaise", Fem}},
            {"Portuguese Opening", {"Ouverture portugaise", Fem}},
            {"Pseudo Queen's Indian Defense", {"Pseudo-défense ouest-indienne", Fem}},
            {"Pterodactyl Defense", {"Défense ptérodactyle", Fem}},
            {"Queen's Gambit", {"Gambit dame", Masc}},
            {"Queen's Indian Accelerated", {"Ouest-indienne accélérée", Fem}},
            {"Queen's Indian Defense", {"Défense ouest-indienne", Fem}},
            {"Queen's Pawn Game", {"Partie du pion dame", Fem}},
            {"Queen's Pawn", {"Pion dame", Masc}},
            {"Rat Defense", {"Défense du rat", Fem}},
            {"Ruy Lopez", {"Partie espagnole", Fem}},
            {"Saragossa Opening", {"Ouverture de Saragosse", Fem}},
            {"Scandinavian Defense", {"Défense scandinave", Fem}},
            {"Scotch Game", {"Partie écossaise", Fem}},
            {"Semi-Slav Defense", {"Défense semi-slave", Fem}},
            {"Sicilian Defense", {"Défense sicilienne", Fem}},
            {"Slav Defense", {"Défense slave", Fem}},
            {"Slav Indian", {"Slave-indienne", Fem}},
            {"St. George Defense", {"Défense Saint-Georges", Fem}},
            {"Three Knights Opening", {"Ouverture des trois cavaliers", Fem}},
            {"Valencia Opening", {"Ouverture de Valence", Fem}},
            {"Vienna Gambit", {"Gambit viennois", Masc}},
            {"Vienna Game", {"Partie viennoise", Fem}},
            {"Vulture Defense", {"Défense du vautour", Fem}},
            {"Yusupov-Rubinstein System", {"Système Youssoupov-Rubinstein", Masc}},
            {"Zaire Defense", {"Défense zaïroise", Fem}},
            // Frequent segments of the variations
            {"Main Line", {"ligne principale", Fem}},
            {"Main Lines", {"lignes principales", Fem}},
            {"Two Knights Defense", {"défense des deux cavaliers", Fem}},
            {"Two Knights Variation", {"variante des deux cavaliers", Fem}},
            {"Four Knights Variation", {"variante des quatre cavaliers", Fem}},
            {"Three Knights Variation", {"variante des trois cavaliers", Fem}},
            {"Dragon Variation", {"variante du dragon", Fem}},
            {"Accelerated Dragon", {"dragon accéléré", Masc}},
            {"Hyperaccelerated Dragon", {"dragon hyperaccéléré", Masc}},
            {"Berlin Defense", {"défense berlinoise", Fem}},
            {"Marshall Attack", {"attaque Marshall", Fem}},
            {"Fried Liver Attack", {"attaque du foie frit", Fem}},
            {"Poisoned Pawn Variation", {"variante du pion empoisonné", Fem}},
            {"Panov Attack", {"attaque Panov", Fem}},
            {"Stonewall Attack", {"attaque Stonewall", Fem}},
            {"Stonewall Variation", {"variante Stonewall", Fem}},
            {"Hedgehog Variation", {"variante du hérisson", Fem}},
            {"Wing Gambit", {"gambit de l'aile", Masc}},
            {"Smith-Morra Gambit", {"gambit Smith-Morra", Masc}},
            {"Noah's Ark Trap", {"piège de l'arche de Noé", Masc}},
            {"Modern Variations", {"variantes modernes", Fem}},
        };
        for (const auto& r : rows) m.insert(QString::fromUtf8(r.en), r.t);
        return m;
    }();
    return map;
}

// Main word (last word of a segment): French name and gender.
const QHash<QString, Term>& heads()
{
    static const QHash<QString, Term> map = {
        {QStringLiteral("Defense"), {"défense", Fem}},       {QStringLiteral("Defence"), {"défense", Fem}},
        {QStringLiteral("Opening"), {"ouverture", Fem}},     {QStringLiteral("Game"), {"partie", Fem}},
        {QStringLiteral("Gambit"), {"gambit", Masc}},        {QStringLiteral("Countergambit"), {"contre-gambit", Masc}},
        {QStringLiteral("Attack"), {"attaque", Fem}},        {QStringLiteral("System"), {"système", Masc}},
        {QStringLiteral("Variation"), {"variante", Fem}},    {QStringLiteral("Variations"), {"variantes", Fem}},
        {QStringLiteral("Line"), {"ligne", Fem}},            {QStringLiteral("Lines"), {"lignes", Fem}},
        {QStringLiteral("Trap"), {"piège", Masc}},           {QStringLiteral("Formation"), {"formation", Fem}},
        {QStringLiteral("Setup"), {"dispositif", Masc}},     {QStringLiteral("Structure"), {"structure", Fem}},
        {QStringLiteral("Complex"), {"complexe", Masc}},     {QStringLiteral("Bind"), {"étau", Masc}},
        {QStringLiteral("Expansion"), {"expansion", Fem}},   {QStringLiteral("Sacrifice"), {"sacrifice", Masc}},
        {QStringLiteral("Hybrid"), {"hybride", Masc}},       {QStringLiteral("Fianchetto"), {"fianchetto", Masc}},
        {QStringLiteral("Counterattack"), {"contre-attaque", Fem}}, {QStringLiteral("Gambits"), {"gambits", Masc}},
    };
    return map;
}

// Adjectives and complements (masculine, feminine), placed after the noun.
struct Adj {
    const char* m;
    const char* f;
};
const QHash<QString, Adj>& adjectives()
{
    static const QHash<QString, Adj> map = {
        {QStringLiteral("Accepted"), {"accepté", "acceptée"}},
        {QStringLiteral("Declined"), {"refusé", "refusée"}},
        {QStringLiteral("Deferred"), {"différé", "différée"}},
        {QStringLiteral("Delayed"), {"retardé", "retardée"}},
        {QStringLiteral("Accelerated"), {"accéléré", "accélérée"}},
        {QStringLiteral("Reversed"), {"inversé", "inversée"}},
        {QStringLiteral("Improved"), {"amélioré", "améliorée"}},
        {QStringLiteral("Refined"), {"raffiné", "raffinée"}},
        {QStringLiteral("Classical"), {"classique", "classique"}},
        {QStringLiteral("Modern"), {"moderne", "moderne"}},
        {QStringLiteral("Main"), {"principal", "principale"}},
        {QStringLiteral("Closed"), {"fermé", "fermée"}},
        {QStringLiteral("Open"), {"ouvert", "ouverte"}},
        {QStringLiteral("Normal"), {"normal", "normale"}},
        {QStringLiteral("Old"), {"ancien", "ancienne"}},
        {QStringLiteral("New"), {"nouveau", "nouvelle"}},
        {QStringLiteral("Symmetrical"), {"symétrique", "symétrique"}},
        {QStringLiteral("Quiet"), {"calme", "calme"}},
        {QStringLiteral("Sharp"), {"tranchant", "tranchante"}},
        {QStringLiteral("Central"), {"central", "centrale"}},
        {QStringLiteral("Exchange"), {"d'échange", "d'échange"}},
        {QStringLiteral("Advance"), {"d'avance", "d'avance"}},
        {QStringLiteral("Fianchetto"), {"du fianchetto", "du fianchetto"}},
        {QStringLiteral("Countergambit"), {"contre-gambit", "contre-gambit"}},
        {QStringLiteral("Wing"), {"de l'aile", "de l'aile"}},
        {QStringLiteral("Center"), {"du centre", "du centre"}},
        {QStringLiteral("Queenside"), {"de l'aile dame", "de l'aile dame"}},
        {QStringLiteral("Kingside"), {"de l'aile roi", "de l'aile roi"}},
        {QStringLiteral("English"), {"anglais", "anglaise"}},
        {QStringLiteral("French"), {"français", "française"}},
        {QStringLiteral("Sicilian"), {"sicilien", "sicilienne"}},
        {QStringLiteral("Italian"), {"italien", "italienne"}},
        {QStringLiteral("Spanish"), {"espagnol", "espagnole"}},
        {QStringLiteral("Scotch"), {"écossais", "écossaise"}},
        {QStringLiteral("Russian"), {"russe", "russe"}},
        {QStringLiteral("Dutch"), {"hollandais", "hollandaise"}},
        {QStringLiteral("Polish"), {"polonais", "polonaise"}},
        {QStringLiteral("Hungarian"), {"hongrois", "hongroise"}},
        {QStringLiteral("Czech"), {"tchèque", "tchèque"}},
        {QStringLiteral("Swiss"), {"suisse", "suisse"}},
        {QStringLiteral("Austrian"), {"autrichien", "autrichienne"}},
        {QStringLiteral("Swedish"), {"suédois", "suédoise"}},
        {QStringLiteral("Danish"), {"danois", "danoise"}},
        {QStringLiteral("Norwegian"), {"norvégien", "norvégienne"}},
        {QStringLiteral("Icelandic"), {"islandais", "islandaise"}},
        {QStringLiteral("Mexican"), {"mexicain", "mexicaine"}},
        {QStringLiteral("American"), {"américain", "américaine"}},
        {QStringLiteral("Yugoslav"), {"yougoslave", "yougoslave"}},
        {QStringLiteral("Berlin"), {"berlinois", "berlinoise"}},
        {QStringLiteral("Venetian"), {"vénitien", "vénitienne"}},
        {QStringLiteral("Argentine"), {"argentin", "argentine"}},
        {QStringLiteral("Brazilian"), {"brésilien", "brésilienne"}},
        {QStringLiteral("Romanian"), {"roumain", "roumaine"}},
        {QStringLiteral("Bulgarian"), {"bulgare", "bulgare"}},
        {QStringLiteral("Greek"), {"grec", "grecque"}},
        {QStringLiteral("Scandinavian"), {"scandinave", "scandinave"}},
        {QStringLiteral("Refused"), {"refusé", "refusée"}},
        {QStringLiteral("German"), {"allemand", "allemande"}},
        {QStringLiteral("Indian"), {"indien", "indienne"}},
        {QStringLiteral("Slav"), {"slave", "slave"}},
        {QStringLiteral("Semi-Slav"), {"semi-slave", "semi-slave"}},
        {QStringLiteral("Catalan"), {"catalan", "catalane"}},
        {QStringLiteral("Orthodox"), {"orthodoxe", "orthodoxe"}},
        {QStringLiteral("Irregular"), {"irrégulier", "irrégulière"}},
        {QStringLiteral("Knight"), {"du cavalier", "du cavalier"}},
        {QStringLiteral("Bishop"), {"du fou", "du fou"}},
        {QStringLiteral("Rook"), {"de la tour", "de la tour"}},
        {QStringLiteral("Queen"), {"de la dame", "de la dame"}},
        {QStringLiteral("King"), {"du roi", "du roi"}},
        {QStringLiteral("Pawn"), {"du pion", "du pion"}},
    };
    return map;
}

// Groups of words translated as a block (before the rest).
const QList<QPair<QString, Adj>>& phrases()
{
    static const QList<QPair<QString, Adj>> list = {
        {QStringLiteral("King's Knight's"), {"du cavalier roi", "du cavalier roi"}},
        {QStringLiteral("Queen's Knight's"), {"du cavalier dame", "du cavalier dame"}},
        {QStringLiteral("King's English"), {"anglais du roi", "anglaise du roi"}},
        {QStringLiteral("Four Pawns"), {"des quatre pions", "des quatre pions"}},
        {QStringLiteral("Five Pawns"), {"des cinq pions", "des cinq pions"}},
        {QStringLiteral("Two Pawns"), {"des deux pions", "des deux pions"}},
        {QStringLiteral("Two Bishops"), {"des deux fous", "des deux fous"}},
        {QStringLiteral("King's Indian"), {"est-indien", "est-indienne"}},
        {QStringLiteral("Bishop's"), {"du fou", "du fou"}},
        {QStringLiteral("Knight's"), {"du cavalier", "du cavalier"}},
        {QStringLiteral("London"), {"de Londres", "de Londres"}},
        {QStringLiteral("Vienna"), {"viennois", "viennoise"}},
        {QStringLiteral("Queen's Indian"), {"ouest-indien", "ouest-indienne"}},
        {QStringLiteral("Queen's Gambit"), {"du gambit dame", "du gambit dame"}},
        {QStringLiteral("King's Gambit"), {"du gambit du roi", "du gambit du roi"}},
        {QStringLiteral("Two Knights"), {"des deux cavaliers", "des deux cavaliers"}},
        {QStringLiteral("Three Knights"), {"des trois cavaliers", "des trois cavaliers"}},
        {QStringLiteral("Four Knights"), {"des quatre cavaliers", "des quatre cavaliers"}},
        {QStringLiteral("Queen's Knight"), {"du cavalier dame", "du cavalier dame"}},
        {QStringLiteral("King's Knight"), {"du cavalier roi", "du cavalier roi"}},
        {QStringLiteral("Queen's Bishop"), {"du fou dame", "du fou dame"}},
        {QStringLiteral("King's Bishop"), {"du fou roi", "du fou roi"}},
        {QStringLiteral("Queen's Pawn"), {"du pion dame", "du pion dame"}},
        {QStringLiteral("King's Pawn"), {"du pion roi", "du pion roi"}},
    };
    return list;
}

QString capitalized(QString s)
{
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

QString decapitalized(QString s)
{
    if (!s.isEmpty()) s[0] = s[0].toLower();
    return s;
}

// A segment ("Najdorf Variation", "Queen's Gambit Accepted", "with e6"…); `first`: the
// family, with a capital letter.
QString translateSegment(QString seg, bool first, Gender* familyGender)
{
    seg = seg.trimmed();
    if (seg.isEmpty()) return seg;
    if (seg.startsWith(QLatin1String("with "))) return QStringLiteral("avec ") + seg.mid(5);
    // Adjectives at the end of a segment ("Accepted", "Declined"…): added after the translation
    QStringList words = seg.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList trailing;
    static const QStringList trailers = {QStringLiteral("Accepted"), QStringLiteral("Declined"), QStringLiteral("Deferred"),
                                         QStringLiteral("Delayed"), QStringLiteral("Accelerated"), QStringLiteral("Reversed"),
                                         QStringLiteral("Refused")};
    while (words.size() > 1 && trailers.contains(words.last())) trailing.prepend(words.takeLast());
    QString core = words.join(QLatin1Char(' '));

    QString fr;
    Gender gender = Masc;
    if (auto it = families().constFind(core); it != families().constEnd()) {
        fr = first ? QString::fromUtf8(it->fr) : decapitalized(QString::fromUtf8(it->fr));
        gender = it->g;
    } else if (auto h = heads().constFind(words.last()); h != heads().constEnd() && words.size() >= 1) {
        gender = h->g;
        QString rest = words.mid(0, words.size() - 1).join(QLatin1Char(' '));
        QStringList after; // translated complements and adjectives
        for (const auto& [en, adj] : phrases())
            if (rest.contains(en)) {
                after << QString::fromUtf8(gender == Fem ? adj.f : adj.m);
                rest.replace(en, QString());
            }
        QStringList names; // proper names (unchanged, without "'s")
        for (QString w : rest.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (auto a = adjectives().constFind(w); a != adjectives().constEnd()) {
                after << QString::fromUtf8(gender == Fem ? a->f : a->m);
                continue;
            }
            if (w.endsWith(QLatin1String("'s"))) w.chop(2);
            if (!w.isEmpty() && w != QLatin1String("'s")) names << w;
        }
        QStringList parts{QString::fromUtf8(h->fr)};
        parts << names << after;
        fr = parts.join(QLatin1Char(' '));
    } else {
        // Without a known main word: names kept then adjectives translated; a segment that is
        // only an adjective ("Open", "Austrian") agrees with the family.
        if (familyGender) gender = *familyGender;
        QStringList names, after;
        for (const QString& w : words) {
            auto a = adjectives().constFind(w);
            if (a != adjectives().constEnd()) after << QString::fromUtf8(gender == Fem ? a->f : a->m);
            else names << w;
        }
        fr = (names + after).join(QLatin1Char(' '));
    }
    if (first && familyGender) *familyGender = gender;
    for (const QString& t : std::as_const(trailing)) {
        const auto a = adjectives().constFind(t);
        fr += QLatin1Char(' ') + (a != adjectives().constEnd() ? QString::fromUtf8(gender == Fem ? a->f : a->m) : t);
    }
    return first ? capitalized(fr) : fr;
}

} // namespace

QString frenchOpeningName(const QString& english)
{
    // "Family: segment, segment…" → "Famille : segment, segment…"
    const int colon = english.indexOf(QLatin1Char(':'));
    const QString family = colon < 0 ? english : english.left(colon);
    QStringList familyParts = family.split(QStringLiteral(", "));
    QStringList out;
    Gender gender = Fem;
    for (int i = 0; i < familyParts.size(); ++i) out << translateSegment(familyParts[i], i == 0, &gender);
    QString result = out.join(QStringLiteral(", "));
    if (colon >= 0) {
        QStringList segs;
        for (const QString& s : english.mid(colon + 1).split(QLatin1Char(','))) segs << translateSegment(s, false, &gender);
        result += QStringLiteral(" : ") + segs.join(QStringLiteral(", "));
    }
    return result;
}

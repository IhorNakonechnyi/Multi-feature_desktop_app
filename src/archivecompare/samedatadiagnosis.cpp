#include "samedatadiagnosis.h"

#include <algorithm>
#include <optional>

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include "../gerber/excellonparser.h"
#include "../gerber/gerberfiletype.h"
#include "../gerber/gerberparser.h"
#include "../gerber/layerequivalence.h"
#include "../gerber/samedatarefusal.h"
#include "samedatacheck.h"

namespace {

// ---------------------------------------------------------------------------
// Shapes: a statement with every value taken out
// ---------------------------------------------------------------------------

// Every number becomes '#', except the code number of a G or M command (G03,
// M02) and a D01/D02/D03 operation: those are syntax, not data. Aperture
// numbers (D10 and up) are masked too - they say nothing useful here.
QString maskNumbers(const QString &text)
{
    static const QRegularExpression number(QStringLiteral("[+-]?(?:\\d+\\.?\\d*|\\.\\d+)"));
    static const QRegularExpression operationCode(QStringLiteral("^0*[123]$"));
    QString out;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = number.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const qsizetype start = match.capturedStart();
        out += text.mid(last, start - last);
        const QChar before = start > 0 ? text.at(start - 1) : QChar();
        const QString value = match.captured();
        const bool isCommandCode = (before == QLatin1Char('G') || before == QLatin1Char('M')) && value.at(0).isDigit();
        const bool isOperation = before == QLatin1Char('D') && operationCode.match(value).hasMatch();
        out += (isCommandCode || isOperation) ? value : QStringLiteral("#");
        last = match.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

// True when a masked statement is plain syntax: upper-case command letters,
// '#', digits and punctuation, with no run of three or more letters except
// the few command words the formats use. A longer letter run could be a name
// (a part number, a customer), so anything else is not shown.
bool isPlainSyntax(const QString &masked)
{
    static const QRegularExpression allowedChars(QStringLiteral("^[A-Z0-9#+\\-.,:=/%]*$"));
    if (!allowedChars.match(masked).hasMatch()) {
        return false;
    }
    static const QStringList commandWords = {
        QStringLiteral("INCH"), QStringLiteral("METRIC"), QStringLiteral("FMAT"), QStringLiteral("ICI"),
        QStringLiteral("VER"),  QStringLiteral("POS"),    QStringLiteral("NEG"),
    };
    static const QRegularExpression letterRun(QStringLiteral("[A-Z]{3,}"));
    QRegularExpressionMatchIterator it = letterRun.globalMatch(masked);
    while (it.hasNext()) {
        if (!commandWords.contains(it.next().captured())) {
            return false;
        }
    }
    return true;
}

// The shape of a plain (non-%) statement or a drill line. A statement that is
// not plain syntax keeps only its first command letter.
QString plainShape(const QString &statement)
{
    const QString masked = maskNumbers(statement);
    if (isPlainSyntax(masked)) {
        return masked;
    }
    const QChar first = statement.isEmpty() ? QChar() : statement.at(0);
    const QString lead = (first >= QLatin1Char('A') && first <= QLatin1Char('Z')) ? QString(first) : QString();
    return lead + QStringLiteral("... (not plain syntax)");
}

// The shape of a Gerber parameter statement, without its leading '%'.
QString parameterShape(const QString &text)
{
    if (text.isEmpty()) {
        return QStringLiteral("%");
    }
    // A macro body line: only its primitive code (21, 4, ...) or '$'.
    if (text.at(0).isDigit() || text.at(0) == QLatin1Char('$')) {
        static const QRegularExpression code(QStringLiteral("^(\\d+|\\$)"));
        return QStringLiteral("%") + code.match(text).captured(1) + QStringLiteral(",...");
    }
    // X2 attributes: the standard attribute name only (".FileFunction"),
    // never its value. A name that is not a standard dotted one can be a
    // user's own label, so it is hidden too.
    if (text.startsWith(QLatin1String("TF")) || text.startsWith(QLatin1String("TA"))
        || text.startsWith(QLatin1String("TO")) || text.startsWith(QLatin1String("TD"))) {
        const QString name = text.mid(2).section(QLatin1Char(','), 0, 0);
        static const QRegularExpression standardName(QStringLiteral("^\\.[A-Za-z]+$"));
        const QString shownName = standardName.match(name).hasMatch() ? name : QStringLiteral("<custom>");
        const bool hasValue = text.contains(QLatin1Char(','));
        return QStringLiteral("%") + text.left(2) + shownName + (hasValue ? QStringLiteral(",...") : QString());
    }
    if (text.startsWith(QLatin1String("ADD"))) {
        static const QRegularExpression aperture(QStringLiteral("^ADD\\d+([CROP])(,|$)"));
        const QRegularExpressionMatch match = aperture.match(text);
        const QString shape = match.hasMatch() ? match.captured(1) : QStringLiteral("<macro>");
        return QStringLiteral("%ADD#") + shape + (text.contains(QLatin1Char(',')) ? QStringLiteral(",...") : QString());
    }
    if (text.startsWith(QLatin1String("AM"))) {
        return QStringLiteral("%AM<name>");
    }
    // The coordinate format is pure syntax - its digits are digit counts.
    static const QRegularExpression format(QStringLiteral("^FS[LTD]?[AI]X\\d+Y\\d+$"));
    if (format.match(text).hasMatch()) {
        return QStringLiteral("%") + text;
    }
    // Everything else (MO, LP, IP, SR, and directives the parser does not
    // read): the two-letter code, then the rest if it is plain syntax.
    const QString code = text.left(2);
    const QString rest = maskNumbers(text.mid(2));
    return QStringLiteral("%") + code + (isPlainSyntax(rest) ? rest : QStringLiteral("..."));
}

// Gerber only: for each entry of a kept list, whether it sits inside a
// G36..G37 region. G36 and G37 are themselves kept as text, so the list is
// enough to tell.
QVector<bool> regionFlags(const QStringList &kept)
{
    QVector<bool> flags;
    flags.reserve(kept.size());
    bool inside = false;
    for (const QString &line : kept) {
        if (line.startsWith(QLatin1String("G37"))) {
            inside = false;
        }
        flags.append(inside && !line.startsWith(QLatin1String("G36")));
        if (line.startsWith(QLatin1String("G36"))) {
            inside = true;
        }
    }
    return flags;
}

QString regionTag(bool inside)
{
    return inside ? QStringLiteral(" (in region)") : QStringLiteral(" (outside region)");
}

// ---------------------------------------------------------------------------
// The text step
// ---------------------------------------------------------------------------

// Up to three "shape xN" groups from `lines`, most frequent first, plus how
// many more groups there were.
QString groupedShapes(const QStringList &lines, bool isGerber, const QVector<bool> &flags)
{
    if (lines.isEmpty()) {
        return QStringLiteral("none");
    }
    QHash<QString, int> counts;
    QStringList order; // first-seen order, for a stable tie-break
    for (int i = 0; i < lines.size(); ++i) {
        QString shape = SameDataDiagnosis::shapeOf(lines.at(i), isGerber);
        if (isGerber) {
            shape += regionTag(flags.at(i));
        }
        if (!counts.contains(shape)) {
            order << shape;
        }
        ++counts[shape];
    }
    std::stable_sort(order.begin(), order.end(),
                     [&counts](const QString &left, const QString &right) { return counts[left] > counts[right]; });
    QStringList parts;
    for (int i = 0; i < order.size() && i < 3; ++i) {
        parts << QStringLiteral("%1 x%2").arg(order.at(i)).arg(counts.value(order.at(i)));
    }
    QString result = parts.join(QStringLiteral("; "));
    if (order.size() > 3) {
        result += QStringLiteral(" (+%1 more kinds)").arg(order.size() - 3);
    }
    return result;
}

// The lines of `mine` that have no equal partner in `theirs` (as a multiset),
// with their region flags.
void unmatchedLines(const QStringList &mine, const QVector<bool> &mineFlags, const QStringList &theirs,
                    QStringList &outLines, QVector<bool> &outFlags)
{
    QHash<QString, int> available;
    for (const QString &line : theirs) {
        ++available[line];
    }
    for (int i = 0; i < mine.size(); ++i) {
        int &left = available[mine.at(i)];
        if (left > 0) {
            --left;
        } else {
            outLines << mine.at(i);
            outFlags << mineFlags.value(i);
        }
    }
}

QString describeTextDifference(const QStringList &a, const QStringList &b, bool isGerber)
{
    const QVector<bool> flagsA = isGerber ? regionFlags(a) : QVector<bool>(a.size(), false);
    const QVector<bool> flagsB = isGerber ? regionFlags(b) : QVector<bool>(b.size(), false);

    QStringList sortedA = a;
    QStringList sortedB = b;
    std::sort(sortedA.begin(), sortedA.end());
    std::sort(sortedB.begin(), sortedB.end());
    const bool sameSet = sortedA == sortedB;

    int first = 0;
    while (first < a.size() && first < b.size() && a.at(first) == b.at(first)) {
        ++first;
    }
    const auto at = [isGerber](const QStringList &list, const QVector<bool> &flags, int index) {
        if (index >= list.size()) {
            return QStringLiteral("(end of list)");
        }
        QString shape = SameDataDiagnosis::shapeOf(list.at(index), isGerber);
        if (isGerber) {
            shape += regionTag(flags.at(index));
        }
        return shape;
    };

    QStringList lines;
    lines << (sameSet ? QStringLiteral("Same-data check: text lines differ - same lines, different order")
                      : QStringLiteral("Same-data check: text lines differ - different lines"));
    lines << QStringLiteral("  kept lines: A %1, B %2 | first difference at kept line #%3")
                 .arg(a.size())
                 .arg(b.size())
                 .arg(first + 1);
    lines << QStringLiteral("  A: %1    B: %2").arg(at(a, flagsA, first), at(b, flagsB, first));

    if (sameSet) {
        // Which lines are out of place: everything from the first difference
        // that does not sit at the same index in B.
        QStringList moved;
        QVector<bool> movedFlags;
        for (int i = first; i < a.size(); ++i) {
            if (a.at(i) != b.at(i)) {
                moved << a.at(i);
                movedFlags << flagsA.at(i);
            }
        }
        lines << QStringLiteral("  out of place in A: %1 lines - %2")
                     .arg(moved.size())
                     .arg(groupedShapes(moved, isGerber, movedFlags));
    } else {
        QStringList onlyA;
        QVector<bool> onlyFlagsA;
        unmatchedLines(a, flagsA, b, onlyA, onlyFlagsA);
        QStringList onlyB;
        QVector<bool> onlyFlagsB;
        unmatchedLines(b, flagsB, a, onlyB, onlyFlagsB);
        lines << QStringLiteral("  only in A: %1").arg(groupedShapes(onlyA, isGerber, onlyFlagsA));
        lines << QStringLiteral("  only in B: %1").arg(groupedShapes(onlyB, isGerber, onlyFlagsB));
    }
    return lines.join(QLatin1Char('\n'));
}

QString describeRefusal(const char *side, const SameDataRefusal &refusal, bool isGerber)
{
    QString text = QStringLiteral("Same-data check: refused (%1) - %2")
                       .arg(QLatin1String(side), refusal.rule.isEmpty() ? QStringLiteral("unknown rule") : refusal.rule);
    if (refusal.position > 0) {
        text += QStringLiteral("\n  at %1 #%2: %3")
                    .arg(isGerber ? QStringLiteral("statement") : QStringLiteral("line"))
                    .arg(refusal.position)
                    .arg(refusal.statement.isEmpty() ? QStringLiteral("-")
                                                     : SameDataDiagnosis::shapeOf(refusal.statement, isGerber));
    }
    return text;
}

} // namespace

QString SameDataDiagnosis::shapeOf(const QString &statement, bool isGerber)
{
    QString shape;
    if (!isGerber) {
        // A drill comment is free text - its words could be anything.
        shape = statement.startsWith(QLatin1Char(';')) ? QStringLiteral(";...") : plainShape(statement);
    } else if (statement.startsWith(QLatin1Char('%'))) {
        shape = parameterShape(statement.mid(1));
    } else {
        shape = plainShape(statement);
    }
    if (shape.length() > 48) {
        shape = shape.left(45) + QStringLiteral("...");
    }
    return shape;
}

QString SameDataDiagnosis::explain(const QByteArray &bytesA, const QByteArray &bytesB)
{
    const GerberFileType::Drawability kindA = GerberFileType::classify(bytesA);
    const GerberFileType::Drawability kindB = GerberFileType::classify(bytesB);
    if (kindA == GerberFileType::Drawability::NotDrawable || kindB == GerberFileType::Drawability::NotDrawable) {
        return QStringLiteral("Same-data check: not run - a side is not recognised as Gerber or drill data");
    }
    if (kindA != kindB) {
        return QStringLiteral("Same-data check: not run - one side is Gerber, the other drill");
    }
    const bool isGerber = kindA == GerberFileType::Drawability::Gerber;
    const QString textA = QString::fromUtf8(bytesA);
    const QString textB = QString::fromUtf8(bytesB);

    // Step 1 and 2: the non-geometry lists, exactly as isSameData builds them.
    SameDataRefusal refusalA;
    SameDataRefusal refusalB;
    bool drawLeftOutA = false;
    bool drawLeftOutB = false;
    const std::optional<QStringList> restA = isGerber
        ? GerberParser::nonGeometryStatements(textA, &refusalA, &drawLeftOutA)
        : ExcellonParser::nonGeometryLines(textA, &refusalA, &drawLeftOutA);
    const std::optional<QStringList> restB = isGerber
        ? GerberParser::nonGeometryStatements(textB, &refusalB, &drawLeftOutB)
        : ExcellonParser::nonGeometryLines(textB, &refusalB, &drawLeftOutB);
    if (!restA || !restB) {
        QStringList lines;
        if (!restA) {
            lines << describeRefusal("A", refusalA, isGerber);
        }
        if (!restB) {
            lines << describeRefusal("B", refusalB, isGerber);
        }
        return lines.join(QLatin1Char('\n'));
    }
    if (*restA != *restB) {
        return describeTextDifference(*restA, *restB, isGerber);
    }

    // Step 3: the objects.
    Layer layerA;
    Layer layerB;
    if (isGerber) {
        layerA = GerberParser::parseContentToObjects(textA);
        layerB = GerberParser::parseContentToObjects(textB);
    } else {
        try {
            layerA = ExcellonParser::parseContentToObjects(textA);
            layerB = ExcellonParser::parseContentToObjects(textB);
        } catch (const ExcellonParser::AmbiguousFormatError &) {
            return QStringLiteral("Same-data check: text lines match, but the drill coordinate format "
                                  "can't be decoded");
        }
    }
    if (layerA.objects.isEmpty() && layerB.objects.isEmpty()
        && SameDataCheck::sameObjectsOrBothEmpty(layerA, layerB, drawLeftOutA || drawLeftOutB)) {
        return QStringLiteral("Same-data check: all steps pass - same data (both layers draw nothing)");
    }
    if (layerA.objects.isEmpty() || layerB.objects.isEmpty()) {
        return QStringLiteral("Same-data check: text lines match, but a side parsed to no objects (A %1, B %2)")
            .arg(layerA.objects.size())
            .arg(layerB.objects.size());
    }
    if (!LayerEquivalence::sameObjects(layerA, layerB)) {
        return QStringLiteral("Same-data check: text lines match, objects differ\n")
            + LayerEquivalence::describeDifference(layerA, layerB);
    }
    return QStringLiteral("Same-data check: all steps pass - same data");
}

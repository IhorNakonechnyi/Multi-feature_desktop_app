#include "layerequivalence.h"

#include <algorithm>

#include <QByteArray>
#include <QDataStream>
#include <QHash>
#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QVector>

namespace {

// Each object is turned into a byte string ("key") that holds everything
// that affects how it is drawn, and nothing else. Two objects draw the same
// thing exactly when their keys are equal - which turns "same objects in any
// order" into "same keys after sorting", a plain sort-and-compare instead of
// comparing every object with every other one (500,000 objects would be
// 250 billion comparisons that way).

QDataStream &operator<<(QDataStream &out, const PointNm &point)
{
    return out << point.x << point.y;
}

// What an aperture IS, independent of its D-number: its shape and sizes, and
// for a macro the macro's own statements (two differently named macros with
// the same body draw the same thing). A macro's parameters are only scaled to
// mm when drawn, so the file's unit scale is part of its identity too. A code
// with no definition is written as such - both sides must miss the same code.
QByteArray apertureKey(int code, const Layer &layer)
{
    QByteArray key;
    QDataStream out(&key, QIODevice::WriteOnly);
    const auto found = layer.apertures.constFind(code);
    if (found == layer.apertures.constEnd()) {
        out << QStringLiteral("undefined") << code;
        return key;
    }
    const GerberAperture &aperture = found.value();
    out << static_cast<int>(aperture.shape) << aperture.modifiers;
    if (aperture.shape == GerberAperture::Shape::Macro) {
        const auto macro = layer.macros.constFind(aperture.macroName);
        if (macro == layer.macros.constEnd()) {
            out << QStringLiteral("undefined macro") << aperture.macroName;
        } else {
            out << macro.value().statements;
        }
        out << layer.unitScale;
    }
    return key;
}

// Only the fields the object's type actually uses: the model leaves the rest
// at defaults, but step-and-repeat translates every point field, so an unused
// field is not guaranteed to be untouched and must not take part.
QByteArray objectKey(const GraphicObject &object, const QHash<int, QByteArray> &apertureKeys)
{
    QByteArray key;
    QDataStream out(&key, QIODevice::WriteOnly);
    out << static_cast<int>(object.type) << static_cast<int>(object.polarity);
    switch (object.type) {
        case ObjectType::Flash:
            out << object.position << apertureKeys.value(object.apertureCode);
            break;
        case ObjectType::Stroke:
            out << object.position << object.strokeTo << object.isArc;
            if (object.isArc) {
                out << object.arcCenter << object.arcCounterClockwise;
            }
            out << apertureKeys.value(object.apertureCode);
            break;
        case ObjectType::Region:
            // Vertex order is kept: it is the outline itself.
            out << static_cast<qint64>(object.regionPoints.size());
            for (int i = 0; i < object.regionPoints.size(); ++i) {
                out << object.regionPoints.at(i);
                // The edge that reaches this vertex: straight, or an arc with
                // its center and direction (a straight edge has no center).
                const RegionEdge edge = object.regionEdges.value(i);
                out << edge.isArc;
                if (edge.isArc) {
                    out << edge.arcCenter << edge.arcCounterClockwise;
                }
            }
            break;
    }
    return key;
}

// The layer as a list of runs (split at each polarity switch), each run's
// keys sorted so the order inside a run no longer matters.
QVector<QVector<QByteArray>> sortedRuns(const Layer &layer)
{
    QHash<int, QByteArray> apertureKeys;
    for (auto it = layer.apertures.constBegin(); it != layer.apertures.constEnd(); ++it) {
        apertureKeys.insert(it.key(), apertureKey(it.key(), layer));
    }

    QVector<QVector<QByteArray>> runs;
    bool first = true;
    Polarity runPolarity = Polarity::Dark;
    for (const GraphicObject &object : layer.objects) {
        if (first || object.polarity != runPolarity) {
            runs.append(QVector<QByteArray>());
            runPolarity = object.polarity;
            first = false;
        }
        const bool apertureDefined = apertureKeys.contains(object.apertureCode);
        if (!apertureDefined && object.type != ObjectType::Region) {
            apertureKeys.insert(object.apertureCode, apertureKey(object.apertureCode, layer));
        }
        runs.last().append(objectKey(object, apertureKeys));
    }
    for (QVector<QByteArray> &run : runs) {
        std::sort(run.begin(), run.end());
    }
    return runs;
}

} // namespace

bool LayerEquivalence::sameObjects(const Layer &a, const Layer &b)
{
    // Cheap rejection first: different object counts can never be equal.
    if (a.objects.size() != b.objects.size()) {
        return false;
    }
    return sortedRuns(a) == sortedRuns(b);
}

// Diagnostics only (SameDataDiagnosis): the same keys sameObjects
// compares, built the same way as sortedRuns, but each one remembers what
// kind of object it came from so the difference can be counted per kind.
QString LayerEquivalence::describeDifference(const Layer &a, const Layer &b)
{
    struct Tally {
        QHash<QString, int> perKind; // "flash" -> count
        QHash<QByteArray, int> keys; // run number + object key -> count
        QHash<QByteArray, QString> kindOfKey;
        int runs = 0;
    };
    const auto kindOf = [](const GraphicObject &object) {
        switch (object.type) {
            case ObjectType::Flash:
                return QStringLiteral("flash");
            case ObjectType::Stroke:
                return object.isArc ? QStringLiteral("arc") : QStringLiteral("line");
            case ObjectType::Region:
                return QStringLiteral("region");
        }
        return QStringLiteral("?");
    };
    const auto tally = [&kindOf](const Layer &layer) {
        QHash<int, QByteArray> apertureKeys;
        for (auto it = layer.apertures.constBegin(); it != layer.apertures.constEnd(); ++it) {
            apertureKeys.insert(it.key(), apertureKey(it.key(), layer));
        }
        Tally result;
        bool first = true;
        Polarity runPolarity = Polarity::Dark;
        for (const GraphicObject &object : layer.objects) {
            if (first || object.polarity != runPolarity) {
                ++result.runs;
                runPolarity = object.polarity;
                first = false;
            }
            if (!apertureKeys.contains(object.apertureCode) && object.type != ObjectType::Region) {
                apertureKeys.insert(object.apertureCode, apertureKey(object.apertureCode, layer));
            }
            QByteArray key = QByteArray::number(result.runs) + ':' + objectKey(object, apertureKeys);
            const QString kind = kindOf(object);
            ++result.perKind[kind];
            ++result.keys[key];
            result.kindOfKey.insert(key, kind);
        }
        return result;
    };
    const Tally tallyA = tally(a);
    const Tally tallyB = tally(b);

    static const QStringList kinds = {QStringLiteral("flash"), QStringLiteral("line"), QStringLiteral("arc"),
                                      QStringLiteral("region")};
    const auto counts = [](const QHash<QString, int> &perKind) {
        QStringList parts;
        for (const QString &kind : kinds) {
            parts << QStringLiteral("%1 %2").arg(perKind.value(kind)).arg(kind);
        }
        return parts.join(QStringLiteral(", "));
    };
    // Objects of `mine` with no equal partner in `theirs`, counted per kind.
    const auto unmatched = [](const Tally &mine, const Tally &theirs) {
        QHash<QString, int> perKind;
        for (auto it = mine.keys.constBegin(); it != mine.keys.constEnd(); ++it) {
            const int extra = it.value() - theirs.keys.value(it.key());
            if (extra > 0) {
                perKind[mine.kindOfKey.value(it.key())] += extra;
            }
        }
        QStringList parts;
        for (const QString &kind : kinds) {
            if (perKind.value(kind) > 0) {
                parts << QStringLiteral("%1 %2").arg(perKind.value(kind)).arg(kind);
            }
        }
        return parts.isEmpty() ? QStringLiteral("none") : parts.join(QStringLiteral(", "));
    };

    QStringList lines;
    lines << QStringLiteral("  A: %1 (%2 polarity runs)").arg(counts(tallyA.perKind)).arg(tallyA.runs);
    lines << QStringLiteral("  B: %1 (%2 polarity runs)").arg(counts(tallyB.perKind)).arg(tallyB.runs);
    lines << QStringLiteral("  only in A: %1 | only in B: %2").arg(unmatched(tallyA, tallyB), unmatched(tallyB, tallyA));
    return lines.join(QLatin1Char('\n'));
}

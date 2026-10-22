#include "layerequivalence.h"

#include <algorithm>

#include <QByteArray>
#include <QDataStream>
#include <QHash>
#include <QIODevice>
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
            for (const PointNm &vertex : object.regionPoints) {
                out << vertex;
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

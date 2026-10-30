#include "gerberobjectdump.h"

#include <algorithm>

#include <QStringList>

namespace {

QString number(double value)
{
    return QString::number(value, 'g', 17);
}

QString point(const PointNm &p)
{
    return QStringLiteral("%1,%2").arg(p.x).arg(p.y);
}

QString polarityName(Polarity polarity)
{
    return polarity == Polarity::Dark ? QStringLiteral("dark") : QStringLiteral("clear");
}

} // namespace

QByteArray GerberObjectDump::dump(const Layer &layer)
{
    QStringList lines;

    // QHash order is not stable between runs, so sort by key first.
    QList<int> codes = layer.apertures.keys();
    std::sort(codes.begin(), codes.end());
    for (const int code : codes) {
        const GerberAperture &aperture = layer.apertures.value(code);
        QStringList modifiers;
        for (const double modifier : aperture.modifiers) {
            modifiers << number(modifier);
        }
        lines << QStringLiteral("aperture %1 shape %2 [%3] macro '%4'")
                     .arg(code)
                     .arg(static_cast<int>(aperture.shape))
                     .arg(modifiers.join(QLatin1Char(' ')), aperture.macroName);
    }

    QStringList names = layer.macros.keys();
    std::sort(names.begin(), names.end());
    for (const QString &name : names) {
        const GerberMacro &macro = layer.macros.value(name);
        lines << QStringLiteral("macro '%1' '%2' [%3]")
                     .arg(name, macro.name, QStringList(macro.statements.begin(), macro.statements.end()).join(QLatin1Char('|')));
    }

    lines << QStringLiteral("unitScale %1").arg(number(layer.unitScale));

    // Every field of every object, even the ones its type does not use: the
    // dump asks "did the parser produce exactly the same thing?", and an
    // unused field that changes is still a change in what the parser did.
    for (const GraphicObject &object : layer.objects) {
        static const char *const typeNames[] = {"flash", "stroke", "region"};
        QString line = QStringLiteral("%1 %2 ap %3 at %4 to %5 arc %6 center %7 %8")
                           .arg(QLatin1String(typeNames[static_cast<int>(object.type)]),
                                polarityName(object.polarity))
                           .arg(object.apertureCode)
                           .arg(point(object.position), point(object.strokeTo))
                           .arg(object.isArc ? 1 : 0)
                           .arg(point(object.arcCenter),
                                object.arcCounterClockwise ? QStringLiteral("ccw") : QStringLiteral("cw"));
        // Region outline: each vertex with the edge that reaches it. The two
        // lists are written separately, so a length mismatch shows too.
        QStringList vertices;
        for (const PointNm &vertex : object.regionPoints) {
            vertices << point(vertex);
        }
        QStringList edges;
        for (const RegionEdge &edge : object.regionEdges) {
            edges << QStringLiteral("%1/%2/%3")
                         .arg(edge.isArc ? 1 : 0)
                         .arg(point(edge.arcCenter), edge.arcCounterClockwise ? QStringLiteral("ccw")
                                                                              : QStringLiteral("cw"));
        }
        line += QStringLiteral(" points [%1] edges [%2]")
                    .arg(vertices.join(QLatin1Char(' ')), edges.join(QLatin1Char(' ')));
        lines << line;
    }
    return lines.join(QLatin1Char('\n')).toUtf8();
}

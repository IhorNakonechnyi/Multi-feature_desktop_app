// TIMING-DIAG: temporary, multi-threading branch only - delete before merge.
#include "stagetiming.h"

#include <QStringList>

// The process memory counters come from Windows itself (psapi) - the app
// is Windows-only, and this file exists only in the timing build.
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

namespace StageTiming {

namespace {

constexpr double kBytesPerMb = 1024.0 * 1024.0;

QString ms(qint64 value)
{
    return QStringLiteral("%1 ms").arg(value);
}

QString memory(const MemoryUse &use)
{
    if (use.nowBytes < 0 || use.peakBytes < 0) {
        return QStringLiteral("-");
    }
    return QStringLiteral("%1/%2 MB")
        .arg(qRound64(use.nowBytes / kBytesPerMb))
        .arg(qRound64(use.peakBytes / kBytesPerMb));
}

QString megabytes(qint64 bytes)
{
    return QString::number(bytes / kBytesPerMb, 'f', 1);
}

} // namespace

MemoryUse readMemoryUse()
{
    MemoryUse use;
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        use.nowBytes = static_cast<qint64>(counters.WorkingSetSize);
        use.peakBytes = static_cast<qint64>(counters.PeakWorkingSetSize);
    }
    return use;
}

QString formatReport(const std::optional<CompareTimes> &compare, const std::optional<ClickTimes> &click)
{
    const QString dash = QStringLiteral("-");
    QStringList lines;

    lines << QStringLiteral("Last compare");
    if (compare) {
        const CompareTimes &c = *compare;
        const qint64 measured = c.zipMs + c.typesMs + c.checkReadMs + c.checkParseMs + c.overlayClassMs;
        // A real change stops the check early, so only green pairs (fully
        // parsed) give an honest parse speed.
        const QString parseSpeed = c.greenBytes > 0
            ? QStringLiteral("%1 ms/MB").arg(qRound64(c.greenParseMs / (c.greenBytes / kBytesPerMb)))
            : dash;
        lines << QStringLiteral("Total: ") + ms(c.totalMs)
              << QStringLiteral("Zip compare: ") + ms(c.zipMs)
              << QStringLiteral("File types: ") + ms(c.typesMs)
              << QStringLiteral("Check reads: ") + ms(c.checkReadMs)
              << QStringLiteral("Check parse: ") + ms(c.checkParseMs)
              << QStringLiteral("Parse speed: ") + parseSpeed
              << QStringLiteral("Overlay classification: ") + ms(c.overlayClassMs)
              << QStringLiteral("Everything else: ") + ms(c.totalMs - measured)
              << QStringLiteral("Memory now/peak: ") + memory(c.memory)
              << QStringLiteral("Pairs checked: %1, turned green: %2, MB parsed: %3")
                     .arg(c.pairsChecked)
                     .arg(c.pairsGreen)
                     .arg(megabytes(c.parsedBytes));
    } else {
        for (const char *name : {"Total", "Zip compare", "File types", "Check reads", "Check parse", "Parse speed",
                                 "Overlay classification", "Everything else", "Memory now/peak"}) {
            lines << QLatin1String(name) + QStringLiteral(": -");
        }
        lines << QStringLiteral("Pairs checked: -, turned green: -, MB parsed: -");
    }

    lines << QStringLiteral("Last click");
    if (click) {
        const ClickTimes &c = *click;
        const QString render = c.renderMs >= 0
            ? QStringLiteral("%1 @ %2x%3 px").arg(ms(c.renderMs)).arg(c.renderPx.width()).arg(c.renderPx.height())
            : dash;
        lines << QStringLiteral("Read: ") + ms(c.readMs)
              << QStringLiteral("Parse: ") + ms(c.parseMs)
              << QStringLiteral("Render: ") + render
              << QStringLiteral("Memory now/peak: ") + memory(c.memory);
    } else {
        for (const char *name : {"Read", "Parse", "Render", "Memory now/peak"}) {
            lines << QLatin1String(name) + QStringLiteral(": -");
        }
    }

    return lines.join(QLatin1Char('\n'));
}

} // namespace StageTiming

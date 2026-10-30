// TIMING-DIAG: temporary, multi-threading branch only - delete before merge.
#include "stagetiming.h"

#include <QSettings>
#include <QStringList>
#include <QThread>

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
    return value >= 0 ? QStringLiteral("%1 ms").arg(value) : QStringLiteral("-");
}

QString mb(qint64 bytes)
{
    // One decimal: a click's memory change can be well under 1 MB.
    return bytes >= 0 ? QString::number(bytes / kBytesPerMb, 'f', 1) : QStringLiteral("-");
}

QString memory(const MemoryUse &use)
{
    if (use.nowBytes < 0 || use.peakBytes < 0) {
        return QStringLiteral("-");
    }
    return QStringLiteral("%1/%2 MB").arg(mb(use.nowBytes), mb(use.peakBytes));
}

QString megabytes(qint64 bytes)
{
    return QString::number(bytes / kBytesPerMb, 'f', 1);
}

QString side(const SideCounts &counts)
{
    return QStringLiteral("%1 files, %2 rows shown, %3 Gerber/drill files, %4 MB Gerber/drill")
        .arg(counts.files)
        .arg(counts.rowsShown)
        .arg(counts.gerberFiles)
        .arg(megabytes(counts.gerberBytes));
}

// The PC the numbers came from, so runs on different machines can be told
// apart. Read once: none of it changes while the app runs.
QString pcLine()
{
    static const QString line = [] {
        const QSettings cpu(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
                            QSettings::NativeFormat);
        const QString cpuName = cpu.value(QStringLiteral("ProcessorNameString")).toString().simplified();
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        const QString ram = GlobalMemoryStatusEx(&status)
            ? QString::number(status.ullTotalPhys / (kBytesPerMb * 1024.0), 'f', 1) + QStringLiteral(" GB RAM")
            : QStringLiteral("RAM -");
        return QStringLiteral("%1, %2 threads, %3")
            .arg(cpuName.isEmpty() ? QStringLiteral("CPU -") : cpuName)
            .arg(QThread::idealThreadCount())
            .arg(ram);
    }();
    return line;
}

} // namespace

TreeTimes &treeTimes()
{
    static TreeTimes times;
    return times;
}

CheckStages &checkStages()
{
    static CheckStages stages;
    return stages;
}

MemoryUse readMemoryUse()
{
    MemoryUse use;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters))) {
        use.nowBytes = static_cast<qint64>(counters.WorkingSetSize);
        use.peakBytes = static_cast<qint64>(counters.PeakWorkingSetSize);
        use.privateBytes = static_cast<qint64>(counters.PrivateUsage);
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
        const qint64 checkOther = c.checkTotalMs - c.checkReadMs - c.checkParseMs;
        const qint64 treesMs = c.trees.buildMs + c.trees.expandMs + c.trees.widthMs;
        const qint64 measured = c.zipMs + c.typesMs + c.checkTotalMs + c.summaryMs + treesMs + c.overlayClassMs
            + c.pairListMs;
        // A real change stops the check early, so only green pairs (fully
        // parsed) give an honest parse speed.
        const QString parseSpeed = c.greenBytes > 0
            ? QStringLiteral("%1 ms/MB").arg(qRound64(c.greenParseMs / (c.greenBytes / kBytesPerMb)))
            : dash;
        const QString slowest = c.slowestPairMs >= 0
            ? QStringLiteral("%1, %2 MB").arg(ms(c.slowestPairMs), megabytes(c.slowestPairBytes))
            : dash;
        // Peak working set only ever grows, so a peak that grew during this
        // run is this run's peak; one that did not means this run stayed
        // under an earlier run's peak.
        QString runPeak = dash;
        if (c.memory.peakBytes >= 0 && c.peakBeforeBytes >= 0) {
            runPeak = c.memory.peakBytes > c.peakBeforeBytes
                ? mb(c.memory.peakBytes) + QStringLiteral(" MB")
                : QStringLiteral("<= %1 MB (an earlier run was higher)").arg(mb(c.peakBeforeBytes));
        }
        lines << QStringLiteral("Total: ") + ms(c.totalMs)
              << QStringLiteral("Until painted: ") + ms(c.untilPaintedMs)
              << QStringLiteral("Zip compare: ") + ms(c.zipMs)
              << QStringLiteral("File types: ") + ms(c.typesMs)
              << QStringLiteral("Check reads: ") + ms(c.checkReadMs)
              << QStringLiteral("Check parse: ") + ms(c.checkParseMs)
              << QStringLiteral("Parse split (text/settings/shapes/compare): %1/%2/%3/%4 ms")
                     .arg(qRound64(c.check.textNs / 1e6))
                     .arg(qRound64(c.check.settingsNs / 1e6))
                     .arg(qRound64(c.check.shapesNs / 1e6))
                     .arg(qRound64(c.check.compareNs / 1e6))
              << QStringLiteral("Result fingerprint: %1 (hashing %2 ms, inside Check parse)")
                     .arg(c.check.fingerprint.isEmpty() ? dash
                                                        : QString::fromLatin1(c.check.fingerprint.toHex().left(16)))
                     .arg(qRound64(c.check.fingerprintNs / 1e6))
              << QStringLiteral("Fast path: hits %1 of %2, %3 ms; parsing on hits %4 ms (saved in normal build); "
                                "disagreements %5; greens missed %6")
                     .arg(c.check.fastHits)
                     .arg(c.pairsChecked)
                     .arg(qRound64(c.check.fastNs / 1e6))
                     .arg(qRound64(c.check.parsingOnHitsNs / 1e6))
                     .arg(c.check.disagreements)
                     .arg(c.check.greensMissed)
              << QStringLiteral("Fast path misses on greens: %1, of them same without noise lines: %2 (detail %3 ms)")
                     .arg(c.check.greensMissed)
                     .arg(c.check.missesSameWithoutNoise)
                     .arg(qRound64(c.check.missDetailNs / 1e6));
        for (const QString &miss : c.check.missLines) {
            lines << QStringLiteral("  miss: ") + miss;
        }
        // This build also parses every fast hit, hashes the fingerprint and
        // explains the misses; the normal build does none of that.
        lines << QStringLiteral("Check parse in normal build: about ")
                + ms(c.checkParseMs
                     - qRound64((c.check.parsingOnHitsNs + c.check.fingerprintNs + c.check.missDetailNs) / 1e6))
              << QStringLiteral("Check other: ") + ms(checkOther)
              << QStringLiteral("Summary: ") + ms(c.summaryMs)
              << QStringLiteral("Trees build: ") + ms(c.trees.buildMs)
              << QStringLiteral("Trees expand: ") + ms(c.trees.expandMs)
              << QStringLiteral("Trees column width: ") + ms(c.trees.widthMs)
              << QStringLiteral("Tree rebuilds: %1").arg(c.trees.builds)
              << QStringLiteral("Overlay classification: ") + ms(c.overlayClassMs)
              << QStringLiteral("Pair list: ") + ms(c.pairListMs)
              << QStringLiteral("Everything else: ") + ms(c.totalMs - measured)
              << QStringLiteral("Parse speed: ") + parseSpeed
              << QStringLiteral("Slowest pair: ") + slowest
              << QStringLiteral("Pairs differing: %1, checked: %2, turned green: %3, drawable: %4, MB parsed: %5")
                     .arg(c.differingPairs)
                     .arg(c.pairsChecked)
                     .arg(c.pairsGreen)
                     .arg(c.drawablePairs)
                     .arg(megabytes(c.parsedBytes))
              << QStringLiteral("Archive A: ") + side(c.sideA)
              << QStringLiteral("Archive B: ") + side(c.sideB)
              << QStringLiteral("Since previous pick: ") + ms(c.sincePreviousPickMs)
              << QStringLiteral("Memory now/peak: ") + memory(c.memory)
              << QStringLiteral("Peak this run: ") + runPeak
              << QStringLiteral("Private start/after check/end: %1/%2/%3 MB")
                     .arg(mb(c.privateStartBytes), mb(c.privateAfterCheckBytes), mb(c.memory.privateBytes));
    } else {
        for (const char *name : {"Total", "Until painted", "Zip compare", "File types", "Check reads", "Check parse",
                                 "Parse split (text/settings/shapes/compare)", "Result fingerprint", "Fast path", "Fast path misses on greens", "Check parse in normal build", "Check other", "Summary", "Trees build", "Trees expand", "Trees column width",
                                 "Tree rebuilds", "Overlay classification", "Pair list", "Everything else",
                                 "Parse speed", "Slowest pair", "Pairs", "Archive A", "Archive B",
                                 "Since previous pick", "Memory now/peak", "Peak this run",
                                 "Private start/after check/end"}) {
            lines << QLatin1String(name) + QStringLiteral(": -");
        }
    }

    lines << QStringLiteral("Last click");
    if (click) {
        const ClickTimes &c = *click;
        const auto render = [&dash](qint64 renderMs, const QSize &px) {
            return renderMs >= 0 ? QStringLiteral("%1 @ %2x%3 px").arg(ms(renderMs)).arg(px.width()).arg(px.height())
                                 : dash;
        };
        lines << QStringLiteral("Read: ") + ms(c.readMs)
              << QStringLiteral("Parse: ") + ms(c.parseMs)
              << QStringLiteral("Render: ") + render(c.renderMs, c.renderPx)
              << QStringLiteral("Slowest render since click: ") + render(c.slowestRenderMs, c.slowestRenderPx)
              << QStringLiteral("Files: %1 MB, parsed objects: %2").arg(megabytes(c.fileBytes)).arg(c.objects)
              << QStringLiteral("Memory now/peak: ") + memory(c.memory)
              << QStringLiteral("Private before read/after parse: %1/%2 MB")
                     .arg(mb(c.privateBeforeBytes), mb(c.privateAfterParseBytes));
    } else {
        for (const char *name : {"Read", "Parse", "Render", "Slowest render since click", "Files", "Memory now/peak",
                                 "Private before read/after parse"}) {
            lines << QLatin1String(name) + QStringLiteral(": -");
        }
    }

    lines << QStringLiteral("PC: ") + pcLine();

    return lines.join(QLatin1Char('\n'));
}

} // namespace StageTiming

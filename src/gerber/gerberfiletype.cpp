#include "gerberfiletype.h"

#include <QByteArrayList>

namespace {

// A genuine Excellon drill header conventionally opens with a bare "M48"
// token line, which no RS-274X Gerber layer would ever contain (Gerber's
// M-codes are M00/M01/M02, never M48). Checked as a whole trimmed line, not
// a substring search, so a comment mentioning "M48" in passing (e.g. a part
// number) can't trigger a false match.
bool headHasBareM48Line(const QByteArray &head)
{
    const QByteArrayList lines = head.split('\n');
    for (const QByteArray &line : lines) {
        // trimmed() strips a trailing '\r' too, so CRLF-terminated files
        // are handled without a separate split step.
        if (line.trimmed().compare("M48", Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

GerberFileType::Drawability GerberFileType::classify(const QByteArray &head)
{
    const QByteArray sniffed = head.left(static_cast<int>(kClassifySniffBytes));

    // Excellon is tested first: a drill file's tool list may legitimately
    // contain a bare "%" line, which would otherwise look like a truncated
    // Gerber "%FS"/"%MO" parameter block. Getting this order backwards
    // would misclassify drill files.
    if (headHasBareM48Line(sniffed)) {
        return Drawability::ExcellonDrill;
    }

    // Plain byte search, not a line split - %FS/%MO can appear anywhere in
    // a parameter block, not necessarily as a whole line by itself.
    if (sniffed.contains("%FS") || sniffed.contains("%MO")) {
        return Drawability::Gerber;
    }

    return Drawability::NotDrawable;
}

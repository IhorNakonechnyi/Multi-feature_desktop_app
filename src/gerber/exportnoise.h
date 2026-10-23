#pragma once

#include <QChar>
#include <QString>
#include <QStringList>

// "Export noise": the handful of lines a CAD/CAM tool rewrites on EVERY
// export even when the board itself is untouched - who exported it, when,
// from which job - plus a drill display colour, and one X2 value that merely
// restates a default (FilePolarity,Positive). Re-saving a project and exporting again changes only
// these lines, which changes every file's CRC and turns a whole archive
// yellow. The same-data check (SameDataCheck) skips exactly these lines and
// nothing else.
//
// The list is deliberately short and built in (the user chose a built-in
// list over a user-editable one): every entry here is a line the app will
// IGNORE, so an over-broad entry could hide a real change. A word missing
// from the list only costs a false yellow, never a missed change - so when
// in doubt, leave it out. Add words only after seeing them in a real
// re-export. Never add real project, customer or supplier names here (see
// CLAUDE.md's confidentiality rules) - only the generic labels tools print.
//
// Header-only on purpose: both parsers use it, and every test target that
// already compiles a parser picks it up without a CMake change.
namespace ExportNoise {

// Free-text comment labels seen to change on a plain re-export. Only drill
// files use these: Gerber skips all its G04 comments, but an Excellon ';'
// comment can carry the coordinate format, so drill comments are judged
// line by line. Matched at the START of the comment text, case-insensitively,
// as a whole word - so ";DATE: ..." is noise, but "; updated pads" (contains
// "date" mid-word) or ";Layer date ..." (word not first) are not.
inline const QStringList &volatileCommentWords()
{
    static const QStringList words = {
        QStringLiteral("job"),
        QStringLiteral("user"),
        QStringLiteral("date"),
        // KiCad's banner, "; DRILL file {KiCad 9.0.5} date 2025-...": its
        // date changes on every export but its first word is DRILL, not date.
        QStringLiteral("drill file"),
        // Altium's drill header ";Layer_Color=9474304": the colour the layer
        // is shown in on screen. It never moves a hole, and it differs
        // between saves of the same board.
        QStringLiteral("layer_color"),
    };
    return words;
}

// Standard Gerber X2 file attributes (Ucamco Gerber Layer Format spec, section
// 5) whose value changes on every export without changing the image. Other
// X2 attributes - .FileFunction, .FilePolarity, .Part, ... - describe what the
// layer IS, so a change there is a real change and they are NOT listed.
inline const QStringList &volatileFileAttributes()
{
    static const QStringList names = {
        QStringLiteral(".CreationDate"),
        QStringLiteral(".GenerationSoftware"),
        QStringLiteral(".ProjectId"),
        QStringLiteral(".MD5"), // a checksum of the file itself - changes whenever any byte does
        // Only a label tying the files of one export together ("these share a
        // coordinate system"); it says nothing about this file's image. Altium
        // writes a fresh GUID here on every export.
        QStringLiteral(".SameCoordinates"),
    };
    return names;
}

// True when `text` starts with `word`, case-insensitively, and the word is not
// just the beginning of a longer word ("date" matches "date:" and "DATE 2026",
// not "dated" or "date_code" - an underscore joins words, and a date code can
// be a real manufacturing marking, so it must not be swept up as noise).
inline bool startsWithWord(const QString &text, const QString &word)
{
    if (!text.startsWith(word, Qt::CaseInsensitive)) {
        return false;
    }
    if (text.length() == word.length()) {
        return true;
    }
    const QChar next = text.at(word.length());
    return !next.isLetterOrNumber() && next != QLatin1Char('_');
}

// `statement` is an X2 attribute statement without its '%' / '*' wrapper,
// e.g. "TF.CreationDate,2026-10-20T10:00:00+02:00". Only TF (file) attributes
// are judged here. Aperture attributes (TA) are always kept; object labels
// (TO/TD) are skipped as a whole by GerberParser::nonGeometryStatements.
inline bool isVolatileAttribute(const QString &statement)
{
    if (!statement.startsWith(QLatin1String("TF"))) {
        return false;
    }
    const QString rest = statement.mid(2);
    for (const QString &name : volatileFileAttributes()) {
        if (rest.startsWith(name, Qt::CaseInsensitive)
            && (rest.length() == name.length() || rest.at(name.length()) == QLatin1Char(','))) {
            return true;
        }
    }
    return false;
}

// True only for the exact attribute "TF.FilePolarity,Positive" (value matched
// case-insensitively). Positive is the Gerber default, so a file that states it
// and a file that leaves it out describe the same image: newer Altium writes
// it, older Altium does not. Anything else - ",Negative", extra fields - is
// not the default and stays compared (",Negative" has one exception, see
// isNegativeFilePolarity). This is not "noise" in the sense of
// isVolatileAttribute (FilePolarity is deliberately not on that list): only
// this one value is skipped, and only because it equals "absent".
inline bool isDefaultFilePolarity(const QString &statement)
{
    static const QLatin1String prefix("TF.FilePolarity,");
    if (!statement.startsWith(prefix, Qt::CaseInsensitive)) {
        return false;
    }
    return statement.mid(prefix.size()).trimmed().compare(QLatin1String("Positive"), Qt::CaseInsensitive) == 0;
}

// True only for the exact attribute "TF.FilePolarity,Negative". Skipped only
// in a solder mask file (see isSolderMaskFileFunction): a solder mask draws
// its openings by convention, so the label restates that - newer Altium
// writes it, older Altium does not. On any other layer, Negative turns the
// drawn shapes into holes (a copper plane), so there it stays compared.
inline bool isNegativeFilePolarity(const QString &statement)
{
    static const QLatin1String prefix("TF.FilePolarity,");
    if (!statement.startsWith(prefix, Qt::CaseInsensitive)) {
        return false;
    }
    return statement.mid(prefix.size()).trimmed().compare(QLatin1String("Negative"), Qt::CaseInsensitive) == 0;
}

// True when the statement is "TF.FileFunction,Soldermask" with or without
// further fields (",Top", ",Bot"), and not a longer word.
inline bool isSolderMaskFileFunction(const QString &statement)
{
    static const QLatin1String prefix("TF.FileFunction,Soldermask");
    if (!statement.startsWith(prefix, Qt::CaseInsensitive)) {
        return false;
    }
    return statement.length() == prefix.size() || statement.at(prefix.size()) == QLatin1Char(',');
}

// `commentBody` is a comment's text with its marker already removed - what
// follows "G04" in a Gerber file or ";" in an Excellon file. Some tools hide
// X2 attributes inside comments for older readers ("G04 #@! TF.CreationDate,
// ..."), so that form is judged exactly like the real attribute.
inline bool isVolatileComment(const QString &commentBody)
{
    QString body = commentBody.trimmed();
    if (body.startsWith(QLatin1String("#@!"))) {
        return isVolatileAttribute(body.mid(3).trimmed());
    }
    for (const QString &word : volatileCommentWords()) {
        if (startsWithWord(body, word)) {
            return true;
        }
    }
    return false;
}

} // namespace ExportNoise

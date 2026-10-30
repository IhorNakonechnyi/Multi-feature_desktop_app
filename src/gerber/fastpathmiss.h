#pragma once

#include <QString>

// Why GerberParser::sameStatementsApartFromNoise or ExcellonParser::
// sameLinesApartFromNoise returned false. Filled only when the caller passes
// one in - the same-data check itself never does; only the timing build does,
// for pairs the parsing check then found to be the same data, to learn what
// kept the fast path from proving them.
//
// The statements are the RAW text (a Gerber '%' statement written as "%...",
// as nonGeometryStatements writes it), so they can hold real data.
// SameDataDiagnosis::shapeOf turns them into value-free shapes before they
// are shown - never display them as they are.
struct FastPathMiss {
    enum class Reason {
        None,         // the fast path proved the pair - nothing to explain
        InvalidBytes, // a file holds U+FFFD; nothing else is filled in
        CutOff,       // a file is cut off mid-statement
        Differs,      // a pair of statements differs (and is not noise of one kind)
    };
    Reason reason = Reason::None;

    // Statements (Gerber) or non-empty lines (drill) in each file, and how
    // many of them are noise the fast path may skip over.
    int countA = 0;
    int countB = 0;
    int noiseA = 0;
    int noiseB = 0;

    // The first pair the fast path stopped at, by position (1-based). An
    // empty statement there means that file had already ended.
    int position = 0;
    QString statementA;
    QString statementB;

    // The same comparison with every noise statement dropped from both files
    // first: would a fast path that skips noise, instead of pairing it by
    // position, have proved the pair? If not, the first pair that differs then.
    bool sameWithoutNoise = false;
    int positionWithoutNoise = 0;
    QString withoutNoiseA;
    QString withoutNoiseB;
};

#include "gerberexpression.h"

namespace {

// A small recursive-descent evaluator for Gerber macro arithmetic:
//   expression := term (('+'|'-') term)*
//   term       := factor (('x'|'X'|'/') factor)*
//   factor     := number | '$' digits | '(' expression ')' | '-' factor
// This is deliberately just enough grammar for what macros actually use -
// no functions, no comparisons, nothing beyond +-x/ and grouping.
class Evaluator {
public:
    Evaluator(const QString &text, const QHash<int, double> &variables)
        : text_(text), variables_(variables)
    {
    }

    double parseExpression()
    {
        double value = parseTerm();
        while (pos_ < text_.length() && (peek() == QLatin1Char('+') || peek() == QLatin1Char('-'))) {
            const QChar op = next();
            const double rhs = parseTerm();
            value = (op == QLatin1Char('+')) ? value + rhs : value - rhs;
        }
        return value;
    }

private:
    double parseTerm()
    {
        double value = parseFactor();
        while (pos_ < text_.length()
               && (peek() == QLatin1Char('x') || peek() == QLatin1Char('X') || peek() == QLatin1Char('/'))) {
            const QChar op = next();
            const double rhs = parseFactor();
            value = (op == QLatin1Char('/')) ? value / rhs : value * rhs;
        }
        return value;
    }

    double parseFactor()
    {
        if (peek() == QLatin1Char('-')) {
            next();
            return -parseFactor();
        }
        if (peek() == QLatin1Char('(')) {
            next();
            const double value = parseExpression();
            if (peek() == QLatin1Char(')')) {
                next();
            }
            return value;
        }
        if (peek() == QLatin1Char('$')) {
            next();
            const int start = pos_;
            while (pos_ < text_.length() && text_.at(pos_).isDigit()) {
                ++pos_;
            }
            const int variableNumber = text_.mid(start, pos_ - start).toInt();
            return variables_.value(variableNumber, 0.0);
        }
        const int start = pos_;
        while (pos_ < text_.length() && (text_.at(pos_).isDigit() || text_.at(pos_) == QLatin1Char('.'))) {
            ++pos_;
        }
        return text_.mid(start, pos_ - start).toDouble();
    }

    QChar peek() const { return pos_ < text_.length() ? text_.at(pos_) : QChar(); }
    QChar next() { return text_.at(pos_++); }

    QString text_;
    const QHash<int, double> &variables_;
    int pos_ = 0;
};

} // namespace

double GerberExpression::evaluate(const QString &expression, const QHash<int, double> &variables)
{
    Evaluator evaluator(expression.trimmed(), variables);
    return evaluator.parseExpression();
}

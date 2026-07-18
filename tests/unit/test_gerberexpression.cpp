#include <QtTest/QtTest>

#include "gerber/gerberexpression.h"

class TestGerberExpression : public QObject {
    Q_OBJECT

private slots:
    void evaluatesNumericLiteral();
    void evaluatesVariableSubstitution();
    void evaluatesMultiplication();
    void evaluatesAdditionAndSubtraction();
    void evaluatesParentheses();
    void evaluatesOperatorPrecedence();
    void evaluatesDivision();
    void evaluatesUnaryMinus();
    void evaluatesUndefinedVariableAsZero();
};

void TestGerberExpression::evaluatesNumericLiteral()
{
    QCOMPARE(GerberExpression::evaluate("22.5", {}), 22.5);
}

void TestGerberExpression::evaluatesVariableSubstitution()
{
    const QHash<int, double> variables = {{1, 0.5}};
    QVERIFY(qAbs(GerberExpression::evaluate("$1", variables) - 0.5) < 1e-9);
}

void TestGerberExpression::evaluatesMultiplication()
{
    // A synthetic scaling expression: "2.0X$1"
    const QHash<int, double> variables = {{1, 0.5}};
    QVERIFY(qAbs(GerberExpression::evaluate("2.0X$1", variables) - (2.0 * 0.5)) < 1e-9);
}

void TestGerberExpression::evaluatesAdditionAndSubtraction()
{
    QCOMPARE(GerberExpression::evaluate("3+4-1", {}), 6.0);
}

void TestGerberExpression::evaluatesParentheses()
{
    QCOMPARE(GerberExpression::evaluate("(1+2)X3", {}), 9.0);
}

void TestGerberExpression::evaluatesOperatorPrecedence()
{
    QCOMPARE(GerberExpression::evaluate("2+3X4", {}), 14.0);
}

void TestGerberExpression::evaluatesDivision()
{
    QCOMPARE(GerberExpression::evaluate("10/4", {}), 2.5);
    QCOMPARE(GerberExpression::evaluate("8/2", {}), 4.0);
}

void TestGerberExpression::evaluatesUnaryMinus()
{
    QCOMPARE(GerberExpression::evaluate("-5+3", {}), -2.0);
    QCOMPARE(GerberExpression::evaluate("3X-2", {}), -6.0);
}

void TestGerberExpression::evaluatesUndefinedVariableAsZero()
{
    QCOMPARE(GerberExpression::evaluate("$5", {}), 0.0);
    QCOMPARE(GerberExpression::evaluate("$5+10", {}), 10.0);
}

QTEST_MAIN(TestGerberExpression)
#include "test_gerberexpression.moc"

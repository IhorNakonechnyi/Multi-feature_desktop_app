#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QtTest>

// Guards the single claim this application makes to anyone reviewing it before
// deployment: the shipped executable links no networking library at all.
//
// This is not a claim about the source code - a grep proves that, and it is
// documented in docs/privacy-verification.md. It is a claim about the built
// artifact, and it has been wrong before: linking Qt6::Pdf silently dragged
// Qt6Network.dll, a tls/ plugin directory and a networkinformation/ plugin
// directory into the deployed folder, because windeployqt follows transitive
// dependencies. Nothing in src/ referenced any of it. The only way that gets
// caught early is by inspecting the binary, so that is what this test does.
//
// HOW IT WORKS, and what it does and does not prove.
//
// Every DLL a PE executable imports has its name stored in the file as plain
// ASCII. So if a forbidden name does not appear anywhere in the file's bytes,
// the executable certainly does not import it. That direction is airtight:
// this test cannot produce a false pass.
//
// The reverse is not airtight - a name could in principle appear in the file
// as ordinary data without being imported, which would fail the test without
// a real dependency existing. That trade is deliberate. A conservative check
// that occasionally needs a human to look is worth more here than a precise
// import-table walk that is three times the code and can itself be wrong.
// If this ever fails spuriously, read the failure message: it names the exact
// string that matched, and `dumpbin /dependents` settles it in one command.
class TestNoNetworkDependency : public QObject {
    Q_OBJECT

private slots:
    void executableImportsNoNetworkLibrary();
};

void TestNoNetworkDependency::executableImportsNoNetworkLibrary()
{
    // The application executable is built into the same directory as this
    // test executable, and CMake makes this target depend on it, so by the
    // time this runs the file exists.
    const QString exePath =
        QCoreApplication::applicationDirPath() + QStringLiteral("/PcbSupplierWorkflow.exe");

    QVERIFY2(QFileInfo::exists(exePath),
             qPrintable(QStringLiteral("Application executable not found at %1 - this test is "
                                       "meaningless without it, so it fails rather than skips.")
                            .arg(exePath)));

    QFile exe(exePath);
    QVERIFY2(exe.open(QIODevice::ReadOnly), qPrintable(exe.errorString()));
    const QByteArray bytes = exe.readAll();
    exe.close();

    QVERIFY2(!bytes.isEmpty(), "Executable is empty");

    // Qt6Network is the one that actually happened, via Qt6::Pdf. The Windows
    // sockets and HTTP libraries are here because they are what any hand-rolled
    // or third-party networking would reach for instead. Qt6Pdf is listed not
    // because it is networking, but because it is the transitive route that
    // brought Qt6Network in - catching the cause gives a clearer failure than
    // catching the symptom.
    const QStringList forbidden = {
        QStringLiteral("Qt6Network.dll"),  QStringLiteral("Qt6Networkd.dll"),
        QStringLiteral("Qt6Pdf.dll"),      QStringLiteral("Qt6Pdfd.dll"),
        QStringLiteral("WS2_32.dll"),      QStringLiteral("WSOCK32.dll"),
        QStringLiteral("WININET.dll"),     QStringLiteral("WINHTTP.dll"),
        QStringLiteral("URLMON.dll"),
    };

    QStringList found;
    for (const QString &name : forbidden) {
        // DLL names are not case-normalised in the import table, so compare
        // case-insensitively rather than trusting a particular spelling.
        if (bytes.toLower().contains(name.toLower().toLatin1())) {
            found << name;
        }
    }

    QVERIFY2(found.isEmpty(),
             qPrintable(QStringLiteral(
                            "%1 references a networking library: %2. This breaks the zero-network "
                            "claim in docs/privacy-verification.md. Most likely a Qt module that "
                            "transitively depends on Qt6::Network was added to "
                            "target_link_libraries in CMakeLists.txt - check there first.")
                            .arg(QDir::toNativeSeparators(exePath), found.join(QStringLiteral(", ")))));
}

QTEST_MAIN(TestNoNetworkDependency)
#include "test_nonetworkdependency.moc"

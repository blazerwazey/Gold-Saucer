#include "GsLog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QtGlobal>

namespace {

QFile*             g_file    = nullptr;
QTextStream*       g_stream  = nullptr;
QMutex             g_mutex;
QtMessageHandler   g_previous = nullptr;

// 4 MB, then one generation of history. A full field pass writes a few hundred
// KB, so this keeps several runs without growing without bound on a machine
// that is never restarted.
constexpr qint64 kMaxBytes = 4 * 1024 * 1024;

QString chooseLogPath()
{
    // AppData first: always writable, and survives the user copying the tool
    // out of a read-only Program Files install.
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty() && QDir().mkpath(appData))
        return appData + "/goldsaucer.log";

    const QString temp = QDir::tempPath();
    if (!temp.isEmpty())
        return temp + "/goldsaucer.log";

    return QString();
}

void writeLine(const QString& line)
{
    if (!g_stream)
        return;
    *g_stream << line << '\n';
    g_stream->flush();          // crash-safe: never buffer a diagnostic
}

void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    {
        QMutexLocker lock(&g_mutex);
        const char* tag = "D";
        switch (type) {
        case QtDebugMsg:    tag = "D"; break;
        case QtInfoMsg:     tag = "I"; break;
        case QtWarningMsg:  tag = "W"; break;
        case QtCriticalMsg: tag = "C"; break;
        case QtFatalMsg:    tag = "F"; break;
        }
        QString line = QStringLiteral("[%1] %2  %3")
                           .arg(QDateTime::currentDateTime().toString("hh:mm:ss.zzz"),
                                QLatin1String(tag), msg);
        // Only worth the noise for the messages you would actually go looking
        // for a source location on.
        if (ctx.file && (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg))
            line += QStringLiteral("   [%1:%2]")
                        .arg(QFileInfo(QString::fromUtf8(ctx.file)).fileName())
                        .arg(ctx.line);
        writeLine(line);
    }
    // Keep the debugger/IDE output working as before.
    if (g_previous)
        g_previous(type, ctx, msg);
}

}  // namespace

namespace GsLog {

void init()
{
    QMutexLocker lock(&g_mutex);
    if (g_file)
        return;

    const QString logPath = chooseLogPath();
    if (logPath.isEmpty())
        return;

    // Rotate before opening, so a single enormous session cannot bury the
    // history that explains it.
    if (QFileInfo(logPath).size() > kMaxBytes) {
        QFile::remove(logPath + ".1");
        QFile::rename(logPath, logPath + ".1");
    }

    g_file = new QFile(logPath);
    if (!g_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete g_file;
        g_file = nullptr;
        return;
    }
    g_stream = new QTextStream(g_file);

    writeLine(QString());
    writeLine(QStringLiteral("================================================"));
    writeLine(QStringLiteral("Gold Saucer %1 started %2")
                  .arg(QCoreApplication::applicationVersion(),
                       QDateTime::currentDateTime().toString(Qt::ISODate)));
    writeLine(QStringLiteral("OS   : %1 (%2)")
                  .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()));
    writeLine(QStringLiteral("Qt   : %1").arg(QLatin1String(qVersion())));
    writeLine(QStringLiteral("Exe  : %1").arg(QCoreApplication::applicationFilePath()));
    writeLine(QStringLiteral("Log  : %1").arg(logPath));
    writeLine(QStringLiteral("================================================"));

    g_previous = qInstallMessageHandler(handler);
}

QString path()
{
    QMutexLocker lock(&g_mutex);
    return g_file ? QFileInfo(*g_file).absoluteFilePath() : QString();
}

void note(const QString& line)
{
    QMutexLocker lock(&g_mutex);
    writeLine(QStringLiteral("[%1] .  %2")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss.zzz"), line));
}

void banner(const QString& title)
{
    QMutexLocker lock(&g_mutex);
    writeLine(QString());
    writeLine(QStringLiteral("---- %1 ----").arg(title));
}

}  // namespace GsLog

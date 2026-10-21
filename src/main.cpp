/*
 * SPDX-FileCopyrightText: 2026 hyprtacle contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "HyprlandInfo.h"
#include "Logging.h"
#include "ScreenshotService.h"

#include <QCommandLineParser>
#include <QTextStream>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QLoggingCategory>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <csignal>
#include <fcntl.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace
{
const auto s_serviceName = u"org.kde.KWin.ScreenShot2"_s;
const auto s_objectPath = u"/org/kde/KWin/ScreenShot2"_s;

// The default Qt message handler does not print anything in some builds, so
// we install our own and write to standard error.
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const char *level = nullptr;
    switch (type) {
    case QtDebugMsg:
        level = "debug";
        break;
    case QtInfoMsg:
        level = "info";
        break;
    case QtWarningMsg:
        level = "warning";
        break;
    case QtCriticalMsg:
        level = "critical";
        break;
    case QtFatalMsg:
        level = "fatal";
        break;
    }

    QTextStream stream(stderr);
    stream << "hyprtacle: " << level << ": " << message;
    if (context.file) {
        stream << " (" << context.file << ':' << context.line << ')';
    }
    stream << Qt::endl;
}

// Self pipe used to turn signals into something the event loop can handle.
int s_signalPipe[2] = {-1, -1};

void unixSignalHandler(int)
{
    const char byte = 1;
    if (s_signalPipe[1] != -1) {
        const ssize_t ignored = ::write(s_signalPipe[1], &byte, 1);
        Q_UNUSED(ignored)
    }
}
}

int main(int argc, char **argv)
{
    // A client that goes away while we are writing the screenshot to the pipe
    // must not take the service down with it.
    ::signal(SIGPIPE, SIG_IGN);

    qInstallMessageHandler(messageHandler);

    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hyprtacled"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Provides the org.kde.KWin.ScreenShot2 D-Bus interface on Hyprland,\n"
                       "so that screenshot tools built for KWin can take pictures."));
    parser.addHelpOption();
    parser.addVersionOption();

    // Note: option names must not start with a dash, Qt strips the dashes for us.
    // "v" is already taken by the version option.
    const QCommandLineOption verboseOption({u"verbose"_s}, QStringLiteral("Show debug output."));
    parser.addOption(verboseOption);

    const QCommandLineOption forceOption({u"f"_s, u"force"_s},
                                          QStringLiteral("Run even when the session does not look like a Hyprland session."));
    parser.addOption(forceOption);

    parser.process(application);

    if (parser.isSet(verboseOption)) {
        QLoggingCategory::setFilterRules(QStringLiteral("hyprtacle=true\nhyprtacle.debug=true"));
    }

    if (!HyprlandInfo::isHyprlandSession() && !parser.isSet(forceOption)) {
        qCInfo(lcHyprtacle) << "Not a Hyprland session, refusing to take over the KWin screenshot interface.";
        return 0;
    }

    if (QStandardPaths::findExecutable(QStringLiteral("grim")).isEmpty()) {
        qCWarning(lcHyprtacle) << "grim was not found in PATH, screenshots will not work.";
    }

    if (::pipe(s_signalPipe) == -1) {
        qCWarning(lcHyprtacle) << "Failed to create the signal pipe";
    } else {
        auto *notifier = new QSocketNotifier(s_signalPipe[0], QSocketNotifier::Read, &application);
        QObject::connect(notifier, &QSocketNotifier::activated, &application, [&notifier] {
            notifier->setEnabled(false);
            char byte = 0;
            const ssize_t ignored = ::read(s_signalPipe[0], &byte, 1);
            Q_UNUSED(ignored)
            QCoreApplication::quit();
        });

        struct sigaction action = {};
        action.sa_handler = unixSignalHandler;
        sigemptyset(&action.sa_mask);
        ::sigaction(SIGINT, &action, nullptr);
        ::sigaction(SIGTERM, &action, nullptr);
    }

    auto *service = new ScreenshotService(&application);

    auto connection = QDBusConnection::sessionBus();
    if (!connection.registerObject(s_objectPath, service)) {
        qCCritical(lcHyprtacle) << "Failed to register the D-Bus object" << s_objectPath;
        return 1;
    }
    if (!connection.registerService(s_serviceName)) {
        // Most likely KWin is running, in which case it owns the interface
        // and there is nothing for us to do.
        qCInfo(lcHyprtacle) << "Could not take over" << s_serviceName << "- another service already owns it.";
        connection.unregisterObject(s_objectPath);
        return 0;
    }

    qCInfo(lcHyprtacle) << "Serving" << s_serviceName << "on" << s_objectPath;

    const int result = QCoreApplication::exec();

    connection.unregisterService(s_serviceName);
    connection.unregisterObject(s_objectPath);

    if (s_signalPipe[0] != -1) {
        ::close(s_signalPipe[0]);
    }
    if (s_signalPipe[1] != -1) {
        ::close(s_signalPipe[1]);
    }

    return result;
}

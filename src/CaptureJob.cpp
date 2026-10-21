/*
 * SPDX-FileCopyrightText: 2026 hyprtacle contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CaptureJob.h"

#include "HyprlandInfo.h"
#include "Logging.h"
#include "PipeWriter.h"

#include <QDBusConnection>
#include <QProcess>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QVariantMap>

#include <unistd.h>

using namespace Qt::StringLiterals;

namespace
{
// How long grim is allowed to take.
constexpr int s_captureTimeoutMs = 5000;

const auto s_errorInvalidScreen = u"org.kde.KWin.ScreenShot2.Error.InvalidScreen"_s;
const auto s_errorInvalidArea = u"org.kde.KWin.ScreenShot2.Error.InvalidArea"_s;
const auto s_errorNoActiveWindow = u"org.kde.KWin.ScreenShot2.Error.NoActiveWindow"_s;
const auto s_errorInvalidWindow = u"org.kde.KWin.ScreenShot2.Error.InvalidWindow"_s;
const auto s_errorFailed = u"org.kde.KWin.ScreenShot2.Error.Failed"_s;
}

CaptureJob::CaptureJob(QDBusMessage replyMessage, int fileDescriptor, Request request, QObject *parent)
    : QObject(parent)
    , m_replyMessage(std::move(replyMessage))
    , m_fileDescriptor(fileDescriptor)
    , m_request(std::move(request))
{
}

void CaptureJob::start()
{
    switch (m_request.kind) {
    case Request::Kind::Screen: {
        const auto all = HyprlandInfo::monitors();
        if (!all) {
            fail(s_errorInvalidScreen, u"Failed to query the list of monitors"_s);
            return;
        }

        std::optional<HyprlandMonitor> monitor;
        for (const auto &candidate : *all) {
            if (candidate.name == m_request.screenName) {
                monitor = candidate;
                break;
            }
        }
        if (!monitor && all->size() == 1) {
            // The client asked for a name we do not know, but there is only one
            // output, so there is no ambiguity.
            monitor = all->constFirst();
            qCWarning(lcHyprtacle) << "Unknown screen" << m_request.screenName << "- using" << monitor->name << "instead";
        }
        if (!monitor) {
            fail(s_errorInvalidScreen, u"Invalid screen requested: "_s + m_request.screenName);
            return;
        }
        if (monitor->disabled) {
            fail(s_errorInvalidScreen, u"The screen is disabled: "_s + monitor->name);
            return;
        }

        m_screenName = monitor->name;
        startGrim({u"-t"_s, u"png"_s, u"-o"_s, monitor->name, u"-"_s}, monitor->scale, monitor->logicalGeometry.width());
        return;
    }
    case Request::Kind::ActiveScreen: {
        const auto monitor = HyprlandInfo::focusedMonitor();
        if (!monitor) {
            fail(s_errorInvalidScreen, u"No active screen"_s);
            return;
        }

        m_screenName = monitor->name;
        startGrim({u"-t"_s, u"png"_s, u"-o"_s, monitor->name, u"-"_s}, monitor->scale, monitor->logicalGeometry.width());
        return;
    }
    case Request::Kind::InteractiveScreen: {
        const auto position = HyprlandInfo::cursorPosition();
        const auto monitor = position ? HyprlandInfo::monitorAt(*position) : HyprlandInfo::focusedMonitor();
        if (!monitor) {
            fail(s_errorInvalidScreen, u"No active screen"_s);
            return;
        }

        m_screenName = monitor->name;
        startGrim({u"-t"_s, u"png"_s, u"-o"_s, monitor->name, u"-"_s}, monitor->scale, monitor->logicalGeometry.width());
        return;
    }
    case Request::Kind::Area: {
        if (m_request.area.width() <= 0 || m_request.area.height() <= 0) {
            fail(s_errorInvalidArea, u"Invalid area requested"_s);
            return;
        }

        const auto geometry = QStringLiteral("%1,%2 %3x%4")
                                  .arg(m_request.area.x())
                                  .arg(m_request.area.y())
                                  .arg(m_request.area.width())
                                  .arg(m_request.area.height());
        startGrim({u"-t"_s, u"png"_s, u"-g"_s, geometry, u"-"_s}, 0.0, m_request.area.width());
        return;
    }
    case Request::Kind::ActiveWindow: {
        const auto window = HyprlandInfo::activeWindow();
        if (!window) {
            fail(s_errorNoActiveWindow, u"No active window"_s);
            return;
        }
        if (window->geometry.width() <= 0 || window->geometry.height() <= 0) {
            fail(s_errorInvalidWindow, u"The active window has no visible geometry"_s);
            return;
        }

        const auto geometry = QStringLiteral("%1,%2 %3x%4")
                                  .arg(window->geometry.x())
                                  .arg(window->geometry.y())
                                  .arg(window->geometry.width())
                                  .arg(window->geometry.height());
        startGrim({u"-t"_s, u"png"_s, u"-g"_s, geometry, u"-"_s}, 0.0, window->geometry.width());
        return;
    }
    case Request::Kind::InteractiveWindow: {
        const auto position = HyprlandInfo::cursorPosition();
        if (!position) {
            fail(s_errorNoActiveWindow, u"Failed to query the cursor position"_s);
            return;
        }
        const auto window = HyprlandInfo::windowAt(*position);
        if (!window) {
            fail(s_errorNoActiveWindow, u"No window under the cursor"_s);
            return;
        }
        if (window->geometry.width() <= 0 || window->geometry.height() <= 0) {
            fail(s_errorInvalidWindow, u"The window has no visible geometry"_s);
            return;
        }

        const auto geometry = QStringLiteral("%1,%2 %3x%4")
                                  .arg(window->geometry.x())
                                  .arg(window->geometry.y())
                                  .arg(window->geometry.width())
                                  .arg(window->geometry.height());
        startGrim({u"-t"_s, u"png"_s, u"-g"_s, geometry, u"-"_s}, 0.0, window->geometry.width());
        return;
    }
    }
}

void CaptureJob::startGrim(const QStringList &arguments, qreal expectedScale, qreal expectedLogicalWidth)
{
    const auto grim = QStandardPaths::findExecutable(QStringLiteral("grim"));
    if (grim.isEmpty()) {
        fail(s_errorFailed, u"grim was not found in PATH"_s);
        return;
    }

    m_scale = expectedScale;
    m_requestedLogicalWidth = expectedLogicalWidth;

    m_process = new QProcess(this);
    m_process->setProgram(grim);
    m_process->setArguments(arguments);

    connect(m_process, &QProcess::finished, this, &CaptureJob::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &CaptureJob::onProcessError);

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(s_captureTimeoutMs);
    connect(m_timeout, &QTimer::timeout, this, &CaptureJob::onTimeout);
    m_timeout->start();

    qCDebug(lcHyprtacle) << "Running" << grim << arguments;

    m_process->start();
}

void CaptureJob::onProcessError()
{
    fail(s_errorFailed, u"Failed to run grim: "_s + m_process->errorString());
}

void CaptureJob::onTimeout()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
    fail(s_errorFailed, u"Timed out while taking the screenshot"_s);
}

void CaptureJob::onProcessFinished()
{
    m_timeout->stop();

    if (m_process->exitStatus() != QProcess::NormalExit || m_process->exitCode() != 0) {
        const auto errorOutput = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        fail(s_errorFailed, errorOutput.isEmpty() ? u"grim failed"_s : u"grim failed: "_s + errorOutput);
        return;
    }

    const auto data = m_process->readAllStandardOutput();
    QImage image;
    if (!image.loadFromData(data, "PNG")) {
        fail(s_errorFailed, u"Failed to decode the screenshot"_s);
        return;
    }

    // Spectacle expects tightly packed 32 bit pixel data.
    image = image.convertToFormat(QImage::Format_RGB32);

    if (m_scale <= 0.0 && m_requestedLogicalWidth > 0.0) {
        // Area captures are scaled by the compositor, so derive the ratio from
        // what we actually got.
        m_scale = static_cast<qreal>(image.width()) / m_requestedLogicalWidth;
    }
    image.setDevicePixelRatio(m_scale);

    succeed(image, m_screenName);
}

void CaptureJob::fail(const QString &errorName, const QString &errorMessage)
{
    qCWarning(lcHyprtacle) << "Screenshot request failed:" << errorMessage;

    if (m_fileDescriptor != -1) {
        ::close(m_fileDescriptor);
        m_fileDescriptor = -1;
    }

    QDBusConnection::sessionBus().send(m_replyMessage.createErrorReply(errorName, errorMessage));
}

void CaptureJob::succeed(const QImage &image, const QString &screenName)
{
    QVariantMap results;
    results.insert(u"type"_s, u"raw"_s);
    results.insert(u"format"_s, static_cast<quint32>(image.format()));
    results.insert(u"width"_s, static_cast<quint32>(image.width()));
    results.insert(u"height"_s, static_cast<quint32>(image.height()));
    results.insert(u"stride"_s, static_cast<quint32>(image.bytesPerLine()));
    results.insert(u"scale"_s, image.devicePixelRatio());
    if (!screenName.isEmpty()) {
        results.insert(u"screen"_s, screenName);
    }

    // The reply has to go out before the pixel data: the client only starts
    // reading the pipe once it has the reply, and the pipe cannot hold a
    // whole screenshot.
    qCDebug(lcHyprtacle) << "Replying with the screenshot metadata" << results;
    QDBusConnection::sessionBus().send(m_replyMessage.createReply(results));

    auto *writer = new PipeWriter(m_fileDescriptor, QByteArray(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()));
    m_fileDescriptor = -1;
    QThreadPool::globalInstance()->start(writer);
}

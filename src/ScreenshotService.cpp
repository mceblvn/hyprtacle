/*
 * SPDX-FileCopyrightText: 2026 hyprtacle contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ScreenshotService.h"

#include "Logging.h"

#include <QDBusConnection>

#include "screenshot2adaptor.h"

#include <fcntl.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace
{
const auto s_errorInvalidWindow = u"org.kde.KWin.ScreenShot2.Error.InvalidWindow"_s;
const auto s_errorInvalidArea = u"org.kde.KWin.ScreenShot2.Error.InvalidArea"_s;
const auto s_errorInvalidScreen = u"org.kde.KWin.ScreenShot2.Error.InvalidScreen"_s;
const auto s_errorFileDescriptor = u"org.kde.KWin.ScreenShot2.Error.FileDescriptor"_s;
const auto s_errorFailed = u"org.kde.KWin.ScreenShot2.Error.Failed"_s;
}

ScreenshotService::ScreenshotService(QObject *parent)
    : QObject(parent)
{
    // The generated adaptor exposes the KWin interface on this object.
    new ScreenShot2Adaptor(this);
}

ScreenshotService::~ScreenshotService() = default;

uint ScreenshotService::version() const
{
    // Version 2 of the KWin interface: it adds CaptureActiveScreen and
    // CaptureActiveWindow. Version 3 would add CaptureWorkspace, which we do
    // not implement.
    return 2;
}

QVariantMap ScreenshotService::startCapture(const QDBusUnixFileDescriptor &pipe, CaptureJob::Request request)
{
    if (!calledFromDBus()) {
        qCWarning(lcHyprtacle) << "The screenshot service was called outside of D-Bus";
        return {};
    }

    // The reply is sent by the capture job once the picture is ready.
    const auto replyMessage = message();
    setDelayedReply(true);
    qCDebug(lcHyprtacle) << "Screenshot request, delayed reply:" << isDelayedReply() << "member:" << replyMessage.member();

    // The file descriptor is only valid for the duration of the call, so we
    // need our own copy of it.
    const int fileDescriptor = ::fcntl(pipe.fileDescriptor(), F_DUPFD_CLOEXEC, 0);
    if (fileDescriptor == -1) {
        QDBusConnection::sessionBus().send(
            replyMessage.createErrorReply(s_errorFileDescriptor, u"No valid file descriptor"_s));
        return {};
    }

    auto *job = new CaptureJob(replyMessage, fileDescriptor, std::move(request), this);
    job->start();

    return {};
}

QVariantMap ScreenshotService::CaptureScreen(const QString &name, const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    Q_UNUSED(options)

    CaptureJob::Request request;
    request.kind = CaptureJob::Request::Kind::Screen;
    request.screenName = name;

    return startCapture(pipe, std::move(request));
}

QVariantMap ScreenshotService::CaptureActiveScreen(const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    Q_UNUSED(options)

    CaptureJob::Request request;
    request.kind = CaptureJob::Request::Kind::ActiveScreen;

    return startCapture(pipe, std::move(request));
}

QVariantMap ScreenshotService::CaptureArea(int x, int y, uint width, uint height, const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    Q_UNUSED(options)

    if (width == 0 || height == 0) {
        if (!calledFromDBus()) {
            return {};
        }
        const auto replyMessage = message();
        setDelayedReply(true);
        QDBusConnection::sessionBus().send(replyMessage.createErrorReply(s_errorInvalidArea, u"Invalid area requested"_s));
        return {};
    }

    CaptureJob::Request request;
    request.kind = CaptureJob::Request::Kind::Area;
    request.area = QRect(x, y, static_cast<int>(width), static_cast<int>(height));

    return startCapture(pipe, std::move(request));
}

QVariantMap ScreenshotService::CaptureActiveWindow(const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    Q_UNUSED(options)

    CaptureJob::Request request;
    request.kind = CaptureJob::Request::Kind::ActiveWindow;

    return startCapture(pipe, std::move(request));
}

QVariantMap ScreenshotService::CaptureInteractive(uint kind, const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    Q_UNUSED(options)

    CaptureJob::Request request;
    switch (kind) {
    case 0:
        request.kind = CaptureJob::Request::Kind::InteractiveWindow;
        break;
    case 1:
        request.kind = CaptureJob::Request::Kind::InteractiveScreen;
        break;
    default:
        if (!calledFromDBus()) {
            return {};
        }
        const auto replyMessage = message();
        setDelayedReply(true);
        QDBusConnection::sessionBus().send(replyMessage.createErrorReply(s_errorInvalidScreen, u"Invalid interactive kind"_s));
        return {};
    }

    return startCapture(pipe, std::move(request));
}

QVariantMap ScreenshotService::CaptureWindow(const QString &handle, const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    Q_UNUSED(options)
    Q_UNUSED(pipe)

    // KWin identifies windows by UUID, Hyprland by address. There is no way
    // to translate between them, and no client we care about uses this.
    if (!calledFromDBus()) {
        return {};
    }
    const auto replyMessage = message();
    setDelayedReply(true);
    QDBusConnection::sessionBus().send(
        replyMessage.createErrorReply(s_errorInvalidWindow, u"Windows cannot be addressed by handle"_s));

    return {};
}

QVariantMap ScreenshotService::CaptureWorkspace(const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    Q_UNUSED(options)
    Q_UNUSED(pipe)

    // This is only part of the API since version 3, which we do not claim.
    if (!calledFromDBus()) {
        return {};
    }
    const auto replyMessage = message();
    setDelayedReply(true);
    QDBusConnection::sessionBus().send(
        replyMessage.createErrorReply(s_errorFailed, u"Capturing the whole workspace is not supported"_s));

    return {};
}

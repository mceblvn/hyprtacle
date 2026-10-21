/*
 * SPDX-FileCopyrightText: 2026 hyprtacle contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <QDBusMessage>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>

/**
 * A single screenshot request.
 *
 * The job takes the picture, replies to the D-Bus call with the image
 * metadata and then pushes the raw pixels into the pipe the client handed us.
 */
class CaptureJob : public QObject
{
    Q_OBJECT

public:
    struct Request {
        enum class Kind {
            Screen,
            Area,
            ActiveScreen,
            ActiveWindow,
            InteractiveScreen,
            InteractiveWindow,
        };

        Kind kind = Kind::Screen;
        // Output name for Kind::Screen.
        QString screenName;
        // Area in logical coordinates for Kind::Area.
        QRect area;
    };

    CaptureJob(QDBusMessage replyMessage, int fileDescriptor, Request request, QObject *parent = nullptr);

    /**
     * Starts the capture. The job sends exactly one reply, either a result
     * or an error.
     */
    void start();

private:
    void startGrim(const QStringList &arguments, qreal expectedScale, qreal expectedLogicalWidth);
    void onProcessFinished();
    void onProcessError();
    void onTimeout();

    void fail(const QString &errorName, const QString &errorMessage);
    void succeed(const QImage &image, const QString &screenName);

    QDBusMessage m_replyMessage;
    int m_fileDescriptor = -1;
    Request m_request;

    class QProcess *m_process = nullptr;
    class QTimer *m_timeout = nullptr;

    // The device pixel ratio the resulting image is tagged with.
    qreal m_scale = 1.0;
    // The width the capture was requested in, used to derive the scale for
    // area captures where the compositor may apply its own scaling.
    qreal m_requestedLogicalWidth = 0.0;
    // Set for captures of a whole output.
    QString m_screenName;
};

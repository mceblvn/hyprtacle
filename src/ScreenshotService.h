/*
 * SPDX-FileCopyrightText: 2026 hyprtacle contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "CaptureJob.h"

#include <QDBusContext>
#include <QDBusUnixFileDescriptor>
#include <QObject>
#include <QVariantMap>

/**
 * Implements the org.kde.KWin.ScreenShot2 D-Bus interface on top of grim.
 *
 * Spectacle talks to KWin through this interface to take screenshots. On
 * Hyprland there is no KWin, so this service impersonates it and lets stock
 * Spectacle take pictures of the screen.
 */
class ScreenshotService : public QObject, public QDBusContext
{
    Q_OBJECT
    Q_PROPERTY(uint Version READ version CONSTANT)

public:
    explicit ScreenshotService(QObject *parent = nullptr);
    ~ScreenshotService() override;

    /**
     * The version of the emulated API. Version 2 adds CaptureActiveScreen and
     * CaptureActiveWindow.
     */
    uint version() const;

public Q_SLOTS:
    QVariantMap CaptureWindow(const QString &handle, const QVariantMap &options, const QDBusUnixFileDescriptor &pipe);
    QVariantMap CaptureActiveWindow(const QVariantMap &options, const QDBusUnixFileDescriptor &pipe);
    QVariantMap CaptureArea(int x, int y, uint width, uint height, const QVariantMap &options, const QDBusUnixFileDescriptor &pipe);
    QVariantMap CaptureScreen(const QString &name, const QVariantMap &options, const QDBusUnixFileDescriptor &pipe);
    QVariantMap CaptureActiveScreen(const QVariantMap &options, const QDBusUnixFileDescriptor &pipe);
    QVariantMap CaptureInteractive(uint kind, const QVariantMap &options, const QDBusUnixFileDescriptor &pipe);
    QVariantMap CaptureWorkspace(const QVariantMap &options, const QDBusUnixFileDescriptor &pipe);

private:
    /**
     * Starts a capture and returns an empty map: the actual reply is sent
     * once the screenshot is ready.
     */
    QVariantMap startCapture(const QDBusUnixFileDescriptor &pipe, CaptureJob::Request request);
};

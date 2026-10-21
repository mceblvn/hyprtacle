/*
 * SPDX-FileCopyrightText: 2026 hyprtacle contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <optional>

/**
 * A monitor as reported by `hyprctl -j monitors`.
 */
struct HyprlandMonitor {
    QString name;
    // Position and size in logical (scaled) coordinates.
    QRect logicalGeometry;
    // Native resolution in pixels.
    QSize pixelSize;
    qreal scale = 1.0;
    bool focused = false;
    bool disabled = false;
    // 0: none, 1: 90, 2: 180, 3: 270 degrees.
    int transform = 0;
};

/**
 * A window as reported by `hyprctl -j activewindow` or `hyprctl -j clients`.
 */
struct HyprlandWindow {
    QString address;
    // Position and size in logical coordinates.
    QRect geometry;
    int monitorId = -1;
    bool hidden = false;
    bool fullscreen = false;
    // Higher means it was focused more recently.
    qlonglong focusHistoryId = 0;
    QString title;
};

namespace HyprlandInfo
{
/**
 * True when the current session is a Hyprland session.
 */
bool isHyprlandSession();

/**
 * All monitors known to the compositor, including disabled ones.
 */
std::optional<QList<HyprlandMonitor>> monitors();

/**
 * The monitor with the given output name, if any.
 */
std::optional<HyprlandMonitor> monitorByName(const QString &name);

/**
 * The monitor that currently has focus.
 */
std::optional<HyprlandMonitor> focusedMonitor();

/**
 * The monitor that contains the given logical position.
 */
std::optional<HyprlandMonitor> monitorAt(const QPoint &pos);

/**
 * The current cursor position in logical coordinates.
 */
std::optional<QPoint> cursorPosition();

/**
 * The currently focused window.
 */
std::optional<HyprlandWindow> activeWindow();

/**
 * The topmost window at the given logical position.
 */
std::optional<HyprlandWindow> windowAt(const QPoint &pos);

/**
 * Runs `hyprctl` with the given arguments and returns its standard output.
 */
std::optional<QByteArray> runHyprctl(const QStringList &arguments, int timeoutMs = 2000);
}

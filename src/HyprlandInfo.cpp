/*
 * SPDX-FileCopyrightText: 2026 hyprtacle contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "HyprlandInfo.h"

#include "Logging.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

using namespace Qt::StringLiterals;

namespace
{
/**
 * The logical size of a monitor, taking the output transform into account.
 */
QSize logicalSize(const HyprlandMonitor &monitor)
{
    auto size = QSize(qRound(monitor.pixelSize.width() / monitor.scale), qRound(monitor.pixelSize.height() / monitor.scale));
    if (monitor.transform == 1 || monitor.transform == 3) {
        size.transpose();
    }
    return size;
}

std::optional<HyprlandMonitor> monitorFromJson(const QJsonObject &object)
{
    HyprlandMonitor monitor;
    monitor.name = object.value(u"name"_s).toString();
    if (monitor.name.isEmpty()) {
        return std::nullopt;
    }

    monitor.pixelSize = QSize(object.value(u"width"_s).toInt(), object.value(u"height"_s).toInt());
    monitor.scale = object.value(u"scale"_s).toDouble(1.0);
    if (monitor.scale <= 0) {
        monitor.scale = 1.0;
    }
    monitor.focused = object.value(u"focused"_s).toBool();
    monitor.disabled = object.value(u"disabled"_s).toBool();
    monitor.transform = object.value(u"transform"_s).toInt();
    const auto size = logicalSize(monitor);
    monitor.logicalGeometry = QRect(object.value(u"x"_s).toInt(), object.value(u"y"_s).toInt(), size.width(), size.height());

    return monitor;
}

std::optional<HyprlandWindow> windowFromJson(const QJsonObject &object)
{
    HyprlandWindow window;
    window.address = object.value(u"address"_s).toString();
    if (window.address.isEmpty()) {
        return std::nullopt;
    }

    const auto at = object.value(u"at"_s).toArray();
    const auto size = object.value(u"size"_s).toArray();
    if (at.size() != 2 || size.size() != 2) {
        return std::nullopt;
    }

    window.geometry = QRect(at.at(0).toInt(), at.at(1).toInt(), size.at(0).toInt(), size.at(1).toInt());
    window.monitorId = object.value(u"monitor"_s).toInt(-1);
    window.hidden = object.value(u"hidden"_s).toBool();
    window.fullscreen = object.value(u"fullscreen"_s).toInt() != 0;
    window.focusHistoryId = static_cast<qlonglong>(object.value(u"focusHistoryID"_s).toDouble());
    window.title = object.value(u"title"_s).toString();

    return window;
}
}

namespace HyprlandInfo
{
bool isHyprlandSession()
{
    return !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE");
}

std::optional<QByteArray> runHyprctl(const QStringList &arguments, int timeoutMs)
{
    QProcess process;
    process.start(QStringLiteral("hyprctl"), arguments);
    if (!process.waitForStarted(timeoutMs)) {
        qCWarning(lcHyprtacle) << "Failed to start hyprctl" << arguments;
        return std::nullopt;
    }
    if (!process.waitForFinished(timeoutMs)) {
        qCWarning(lcHyprtacle) << "hyprctl timed out" << arguments;
        process.kill();
        process.waitForFinished(1000);
        return std::nullopt;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qCWarning(lcHyprtacle) << "hyprctl failed" << arguments << process.readAllStandardError();
        return std::nullopt;
    }
    return process.readAllStandardOutput();
}

std::optional<QList<HyprlandMonitor>> monitors()
{
    const auto output = runHyprctl({u"-j"_s, u"monitors"_s});
    if (!output) {
        return std::nullopt;
    }

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(*output, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        qCWarning(lcHyprtacle) << "Failed to parse hyprctl monitors output:" << error.errorString();
        return std::nullopt;
    }

    QList<HyprlandMonitor> result;
    const auto array = document.array();
    for (const auto &value : array) {
        if (auto monitor = monitorFromJson(value.toObject())) {
            result.append(*monitor);
        }
    }

    return result;
}

std::optional<HyprlandMonitor> monitorByName(const QString &name)
{
    const auto all = monitors();
    if (!all) {
        return std::nullopt;
    }
    for (const auto &monitor : *all) {
        if (monitor.name == name) {
            return monitor;
        }
    }
    return std::nullopt;
}

std::optional<HyprlandMonitor> focusedMonitor()
{
    const auto all = monitors();
    if (!all) {
        return std::nullopt;
    }
    for (const auto &monitor : *all) {
        if (monitor.focused) {
            return monitor;
        }
    }
    return std::nullopt;
}

std::optional<HyprlandMonitor> monitorAt(const QPoint &pos)
{
    const auto all = monitors();
    if (!all) {
        return std::nullopt;
    }
    for (const auto &monitor : *all) {
        if (!monitor.disabled && monitor.logicalGeometry.contains(pos)) {
            return monitor;
        }
    }
    return std::nullopt;
}

std::optional<QPoint> cursorPosition()
{
    const auto output = runHyprctl({u"cursorpos"_s});
    if (!output) {
        return std::nullopt;
    }

    const auto text = QString::fromUtf8(*output).trimmed();
    const auto parts = text.split(QChar(u','));
    if (parts.size() != 2) {
        qCWarning(lcHyprtacle) << "Unexpected cursorpos output:" << text;
        return std::nullopt;
    }

    bool okX = false;
    bool okY = false;
    const int x = parts.at(0).trimmed().toInt(&okX);
    const int y = parts.at(1).trimmed().toInt(&okY);
    if (!okX || !okY) {
        qCWarning(lcHyprtacle) << "Unexpected cursorpos output:" << text;
        return std::nullopt;
    }

    return QPoint(x, y);
}

std::optional<HyprlandWindow> activeWindow()
{
    const auto output = runHyprctl({u"-j"_s, u"activewindow"_s});
    if (!output) {
        return std::nullopt;
    }

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(*output, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(lcHyprtacle) << "Failed to parse hyprctl activewindow output:" << error.errorString();
        return std::nullopt;
    }

    return windowFromJson(document.object());
}

std::optional<HyprlandWindow> windowAt(const QPoint &pos)
{
    const auto output = runHyprctl({u"-j"_s, u"clients"_s});
    if (!output) {
        return std::nullopt;
    }

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(*output, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        qCWarning(lcHyprtacle) << "Failed to parse hyprctl clients output:" << error.errorString();
        return std::nullopt;
    }

    std::optional<HyprlandWindow> result;
    for (const auto &value : document.array()) {
        const auto window = windowFromJson(value.toObject());
        if (!window || window->hidden) {
            continue;
        }
        if (!window->geometry.contains(pos)) {
            continue;
        }
        // The most recently focused window is on top.
        if (!result || window->focusHistoryId > result->focusHistoryId) {
            result = window;
        }
    }

    return result;
}
}

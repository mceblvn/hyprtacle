/*
 * SPDX-FileCopyrightText: 2026 hyprtacle contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "HyprlandInfo.h"

#include <QTest>

class CaptureTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testIsHyprlandSession();
    void testMonitors();
    void testMonitorLookup();
    void testCursorPosition();
};

void CaptureTest::testIsHyprlandSession()
{
    // The test only runs inside a Hyprland session, otherwise there is
    // nothing to query.
    if (!HyprlandInfo::isHyprlandSession()) {
        QSKIP("Not running in a Hyprland session");
    }
    QVERIFY(HyprlandInfo::isHyprlandSession());
}

void CaptureTest::testMonitors()
{
    if (!HyprlandInfo::isHyprlandSession()) {
        QSKIP("Not running in a Hyprland session");
    }

    const auto monitors = HyprlandInfo::monitors();
    QVERIFY(monitors.has_value());
    QVERIFY(!monitors->isEmpty());

    for (const auto &monitor : *monitors) {
        QVERIFY(!monitor.name.isEmpty());
        QVERIFY(monitor.pixelSize.width() > 0);
        QVERIFY(monitor.pixelSize.height() > 0);
        QVERIFY(monitor.scale > 0);
        QVERIFY(monitor.logicalGeometry.width() > 0);
        QVERIFY(monitor.logicalGeometry.height() > 0);
    }
}

void CaptureTest::testMonitorLookup()
{
    if (!HyprlandInfo::isHyprlandSession()) {
        QSKIP("Not running in a Hyprland session");
    }

    const auto monitors = HyprlandInfo::monitors();
    QVERIFY(monitors.has_value());
    QVERIFY(!monitors->isEmpty());

    const auto &first = monitors->constFirst();
    const auto found = HyprlandInfo::monitorByName(first.name);
    QVERIFY(found.has_value());
    QCOMPARE(found->name, first.name);
    QCOMPARE(found->pixelSize, first.pixelSize);

    QVERIFY(!HyprlandInfo::monitorByName(QStringLiteral("this-is-not-a-monitor")).has_value());
}

void CaptureTest::testCursorPosition()
{
    if (!HyprlandInfo::isHyprlandSession()) {
        QSKIP("Not running in a Hyprland session");
    }

    const auto position = HyprlandInfo::cursorPosition();
    QVERIFY(position.has_value());
    QVERIFY(position->x() >= 0);
    QVERIFY(position->y() >= 0);
}

QTEST_MAIN(CaptureTest)

#include "tst_capture.moc"

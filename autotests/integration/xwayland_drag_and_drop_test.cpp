/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2022 David Redondo <kde@david-redondo.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"

#include "core/output.h"
#include "core/outputbackend.h"
#include "wayland/seat_interface.h"
#include "wayland_server.h"
#include "window.h"
#include "workspace.h"
#include "xwayland/databridge.h"

#include <QProcess>
#include <QProcessEnvironment>

#include <linux/input.h>
#include <wayland/abstract_data_source.h>
using namespace KWin;

static const QString s_socketName = QStringLiteral("wayland_test_kwin_xwayland_selections-0");

struct ProcessKillBeforeDeleter
{
    void operator()(QProcess *pointer)
    {
        if (pointer) {
            pointer->kill();
        }
        delete pointer;
    }
};

class XWaylandDragAndDropTest : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void dummy();
private Q_SLOTS:
    void initTestCase();
    void cleanUp();
    void testDragAndDrop();
    void testDragAndDrop_data();
};

void XWaylandDragAndDropTest::initTestCase()
{
    qRegisterMetaType<KWin::Window *>();
    qRegisterMetaType<QProcess::ExitStatus>();
    QSignalSpy applicationStartedSpy(kwinApp(), &Application::started);
    ;
    QVERIFY(waylandServer()->init(s_socketName));
    QMetaObject::invokeMethod(kwinApp()->outputBackend(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(QVector<QRect>, QVector<QRect>() << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 1024)));

    kwinApp()->start();
    QVERIFY(applicationStartedSpy.wait());
    const auto outputs = workspace()->outputs();
    QCOMPARE(outputs.count(), 2);
    QCOMPARE(outputs[0]->geometry(), QRect(0, 0, 1280, 1024));
    QCOMPARE(outputs[1]->geometry(), QRect(1280, 0, 1280, 1024));
}

void XWaylandDragAndDropTest::cleanUp()
{
    // Always release pointer button
    Test::pointerButtonReleased(BTN_LEFT, 1);
}

void XWaylandDragAndDropTest::testDragAndDrop_data()
{
    QTest::addColumn<QString>("dragPlatform");
    QTest::addColumn<QString>("dropPlatform");

    QTest::newRow("x11->wayland") << QStringLiteral("xcb") << QStringLiteral("wayland");
    // QTest::newRow("wayland->x11") << QStringLiteral("wayland") << QStringLiteral("xcb");
}

void XWaylandDragAndDropTest::testDragAndDrop()
{
    qputenv("LD_PRELOAD", "/home/david/qt/qt5/build/qtbase/lib/libQt5WaylandClient.so.5.15.7");
    const QString drop = QFINDTESTDATA(QStringLiteral("drop"));
    const QString drag = QFINDTESTDATA(QStringLiteral("drag"));

    QSignalSpy windowAddedSpy(workspace(), &Workspace::windowAdded);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    // start the copy process
    QFETCH(QString, dragPlatform);
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), dragPlatform);
    environment.insert(QStringLiteral("WAYLAND_DISPLAY"), s_socketName);
    std::unique_ptr<QProcess, ProcessKillBeforeDeleter> dragProcess(new QProcess());
    QSignalSpy finishedDragSpy(dragProcess.get(), static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished));
    dragProcess->setProcessEnvironment(environment);
    dragProcess->setProcessChannelMode(QProcess::ForwardedChannels);
    dragProcess->setProgram(drag);
    dragProcess->start();
    QVERIFY(dragProcess->waitForStarted());

    Window *dragWindow = nullptr;
    QVERIFY(windowAddedSpy.wait());
    dragWindow = windowAddedSpy.first().first().value<Window *>();
    QVERIFY(dragWindow);
    dragWindow->move({0, 0});

    // start the paste process
    QFETCH(QString, dropPlatform);
    std::unique_ptr<QProcess, ProcessKillBeforeDeleter> dropProcess(new QProcess());
    QSignalSpy finishedDropSpy(dropProcess.get(), static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished));
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), dropPlatform);
    dropProcess->setProcessEnvironment(environment);
    dropProcess->setProcessChannelMode(QProcess::ForwardedChannels);
    dropProcess->setProgram(drop);
    dropProcess->start();
    QVERIFY(dropProcess->waitForStarted());

    windowAddedSpy.clear();
    Window *dropWindow = nullptr;
    QVERIFY(windowAddedSpy.wait());
    dropWindow = windowAddedSpy.last().first().value<Window *>();
    QCOMPARE(windowAddedSpy.count(), 1);
    QVERIFY(dropWindow);
    dropWindow->move({1280, 0});

    QSignalSpy dragStartedSpy(waylandServer()->seat(), &KWaylandServer::SeatInterface::dragStarted);
    int timestamp = 0;
    Test::pointerMotion(dragWindow->clientGeometry().topLeft(), ++timestamp);
    qDebug() << "Button down -----------------------------------------------------";
    Test::pointerButtonPressed(BTN_LEFT, ++timestamp);
    QVERIFY(dragStartedSpy.wait());

    qDebug() << "Moving to drop window -----------------------------------------------------";
    Test::pointerMotion(dropWindow->clientGeometry().topLeft(), ++timestamp);
    QTest::qSleep(1);
    Test::pointerMotion(dropWindow->clientGeometry().center() + QPoint(1, 1), ++timestamp);
    // XWayland currently does not use the normal drag and drop mechanism
    // QTRY_COMPARE(waylandServer()->seat()->dragSurface(), dropWindow->surface());
    QTRY_VERIFY(waylandServer()->seat()->dragSource()->isAccepted());

    qDebug() << "Button up -----------------------------------------------------";
    Test::pointerButtonReleased(BTN_LEFT, ++timestamp);
    QVERIFY(finishedDropSpy.count() || finishedDropSpy.wait());
    QCOMPARE(finishedDropSpy.first().first().toInt(), 0);
    QVERIFY(finishedDragSpy.count() || finishedDragSpy.wait());
    QCOMPARE(finishedDragSpy.first().first().toInt(), 0);
}

WAYLANDTEST_MAIN(XWaylandDragAndDropTest)
#include "xwayland_drag_and_drop_test.moc"

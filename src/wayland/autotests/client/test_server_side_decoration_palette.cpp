/*
    SPDX-FileCopyrightText: 2017 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
// Qt
#include <QtTest>
// KWin
#include "wayland/compositor_interface.h"
#include "wayland/display.h"
#include "wayland/server_decoration_palette_interface.h"

#include "KWayland/Client/compositor.h"
#include "KWayland/Client/connection_thread.h"
#include "KWayland/Client/event_queue.h"
#include "KWayland/Client/region.h"
#include "KWayland/Client/registry.h"
#include "KWayland/Client/server_decoration_palette.h"
#include "KWayland/Client/surface.h"

using namespace KWayland::Client;

class TestServerSideDecorationPalette : public QObject
{
    Q_OBJECT
public:
    explicit TestServerSideDecorationPalette(QObject *parent = nullptr);
private Q_SLOTS:
    void init();
    void cleanup();

    void testCreateAndSet();

private:
    std::unique_ptr<KWaylandServer::Display> m_display;
    std::unique_ptr<KWaylandServer::CompositorInterface> m_compositorInterface;
    std::unique_ptr<KWaylandServer::ServerSideDecorationPaletteManagerInterface> m_paletteManagerInterface;
    KWayland::Client::ConnectionThread *m_connection;
    KWayland::Client::Compositor *m_compositor;
    KWayland::Client::ServerSideDecorationPaletteManager *m_paletteManager;
    KWayland::Client::EventQueue *m_queue;
    QThread *m_thread;
};

static const QString s_socketName = QStringLiteral("kwayland-test-wayland-decopalette-0");

TestServerSideDecorationPalette::TestServerSideDecorationPalette(QObject *parent)
    : QObject(parent)
    , m_display(nullptr)
    , m_compositor(nullptr)
    , m_queue(nullptr)
    , m_thread(nullptr)
{
}

void TestServerSideDecorationPalette::init()
{
    using namespace KWaylandServer;
    m_display = std::make_unique<KWaylandServer::Display>();
    m_display->addSocketName(s_socketName);
    m_display->start();
    QVERIFY(m_display->isRunning());

    // setup connection
    m_connection = new KWayland::Client::ConnectionThread;
    QSignalSpy connectedSpy(m_connection, &ConnectionThread::connected);
    m_connection->setSocketName(s_socketName);

    m_thread = new QThread(this);
    m_connection->moveToThread(m_thread);
    m_thread->start();

    m_connection->initConnection();
    QVERIFY(connectedSpy.wait());

    m_queue = new KWayland::Client::EventQueue(this);
    QVERIFY(!m_queue->isValid());
    m_queue->setup(m_connection);
    QVERIFY(m_queue->isValid());

    Registry registry;
    QSignalSpy compositorSpy(&registry, &Registry::compositorAnnounced);

    QSignalSpy registrySpy(&registry, &Registry::serverSideDecorationPaletteManagerAnnounced);

    QVERIFY(!registry.eventQueue());
    registry.setEventQueue(m_queue);
    QCOMPARE(registry.eventQueue(), m_queue);
    registry.create(m_connection->display());
    QVERIFY(registry.isValid());
    registry.setup();

    m_compositorInterface = std::make_unique<CompositorInterface>(m_display.get());
    QVERIFY(compositorSpy.wait());
    m_compositor = registry.createCompositor(compositorSpy.first().first().value<quint32>(), compositorSpy.first().last().value<quint32>(), this);

    m_paletteManagerInterface = std::make_unique<ServerSideDecorationPaletteManagerInterface>(m_display.get());

    QVERIFY(registrySpy.wait());
    m_paletteManager =
        registry.createServerSideDecorationPaletteManager(registrySpy.first().first().value<quint32>(), registrySpy.first().last().value<quint32>(), this);
}

void TestServerSideDecorationPalette::cleanup()
{
#define CLEANUP(variable)   \
    if (variable) {         \
        delete variable;    \
        variable = nullptr; \
    }
    CLEANUP(m_compositor)
    CLEANUP(m_paletteManager)
    CLEANUP(m_queue)
    if (m_connection) {
        m_connection->deleteLater();
        m_connection = nullptr;
    }
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    m_compositorInterface.reset();
#undef CLEANUP
    m_paletteManagerInterface.reset();
    m_display.reset();
}

void TestServerSideDecorationPalette::testCreateAndSet()
{
    QSignalSpy serverSurfaceCreated(m_compositorInterface.get(), &KWaylandServer::CompositorInterface::surfaceCreated);

    std::unique_ptr<KWayland::Client::Surface> surface(m_compositor->createSurface());
    QVERIFY(serverSurfaceCreated.wait());

    auto serverSurface = serverSurfaceCreated.first().first().value<KWaylandServer::SurfaceInterface *>();
    QSignalSpy paletteCreatedSpy(m_paletteManagerInterface.get(), &KWaylandServer::ServerSideDecorationPaletteManagerInterface::paletteCreated);

    QVERIFY(!m_paletteManagerInterface->paletteForSurface(serverSurface));

    auto palette = m_paletteManager->create(surface.get(), surface.get());
    QVERIFY(paletteCreatedSpy.wait());
    auto paletteInterface = paletteCreatedSpy.first().first().value<KWaylandServer::ServerSideDecorationPaletteInterface *>();
    QCOMPARE(m_paletteManagerInterface->paletteForSurface(serverSurface), paletteInterface);

    QCOMPARE(paletteInterface->palette(), QString());

    QSignalSpy changedSpy(paletteInterface, &KWaylandServer::ServerSideDecorationPaletteInterface::paletteChanged);

    palette->setPalette("foobar");

    QVERIFY(changedSpy.wait());
    QCOMPARE(paletteInterface->palette(), QString("foobar"));

    // and destroy
    QSignalSpy destroyedSpy(paletteInterface, &QObject::destroyed);
    delete palette;
    QVERIFY(destroyedSpy.wait());
    QVERIFY(!m_paletteManagerInterface->paletteForSurface(serverSurface));
}

QTEST_GUILESS_MAIN(TestServerSideDecorationPalette)
#include "test_server_side_decoration_palette.moc"

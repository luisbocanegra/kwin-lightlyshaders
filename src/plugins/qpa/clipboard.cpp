/*
    SPDX-FileCopyrightText: 2022 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "clipboard.h"
#include "utils/filedescriptor.h"
#include "wayland/display.h"
#include "wayland/seat_interface.h"
#include "wayland_server.h"

#include <QtConcurrent>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace KWin
{
namespace QPA
{

DataSource::DataSource(QMimeData *mimeData, QObject *parent)
    : KWaylandServer::AbstractDataSource(parent)
    , m_mimeData(mimeData)
{
}

void DataSource::requestData(const QString &mimeType, qint32 fd)
{
    const QByteArray data = m_mimeData->data(mimeType);
    QtConcurrent::run([data, fd]() {
        write(fd, data.constData(), data.size());
        close(fd);
    });
}

void DataSource::cancel()
{
}

QStringList DataSource::mimeTypes() const
{
    return m_mimeData->formats();
}

DataSourceMimeData::DataSourceMimeData(KWaylandServer::AbstractDataSource *dataSource)
    : m_dataSource(dataSource)
{
}

static QByteArray readData(int fd)
{
    pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    FileDescriptor closeFd{fd};
    QByteArray data;
    while (true) {
        const int ready = poll(&pfd, 1, 1000);
        if (ready < 0) {
            if (errno != EINTR) {
                return QByteArrayLiteral("poll() failed: ") + strerror(errno);
            }
        } else if (ready == 0) {
            return QByteArrayLiteral("timeout reading from pipe");
        } else {
            char buf[4096];
            int n = read(fd, buf, sizeof buf);

            if (n < 0) {
                return QByteArrayLiteral("read failed: ") + strerror(errno);
            } else if (n == 0) {
                return data;
            } else if (n > 0) {
                data.append(buf, n);
            }
        }
    }
}

QVariant DataSourceMimeData::retrieveData(const QString &mimeType, QVariant::Type preferredType) const
{
    int pipeFds[2];
    if (pipe2(pipeFds, O_CLOEXEC) != 0) {
        return QVariant();
    }

    m_dataSource->requestData(mimeType, pipeFds[1]);
    waylandServer()->display()->flush();

    const QByteArray data = readData(pipeFds[0]);
    if (data.isEmpty()) {
        return QVariant();
    }

    return data;
}

Clipboard::Clipboard()
{
}

Clipboard::~Clipboard()
{
}

void Clipboard::initialize()
{
    connect(waylandServer()->seat(), &KWaylandServer::SeatInterface::selectionChanged, this, [this]() {
        if (waylandServer()->seat()->selection()) {
            m_mimeData = std::make_unique<DataSourceMimeData>(waylandServer()->seat()->selection());
        } else {
            m_mimeData.reset();
        }
        emitChanged(QClipboard::Clipboard);
    });
}

QMimeData *Clipboard::mimeData(QClipboard::Mode mode)
{
    return m_mimeData ? m_mimeData.get() : &m_emptyData;
}

void Clipboard::setMimeData(QMimeData *data, QClipboard::Mode mode)
{
    if (mode == QClipboard::Clipboard) {
        if (data) {
            std::unique_ptr<DataSource> dataSource = std::make_unique<DataSource>(data);
            waylandServer()->seat()->setSelection(dataSource.get());
            m_dataSource = std::move(dataSource);
        } else {
            if (waylandServer()->seat()->selection() == m_dataSource.get()) {
                waylandServer()->seat()->setSelection(nullptr);
            }
            m_dataSource.reset();
        }
    }
}

bool Clipboard::supportsMode(QClipboard::Mode mode) const
{
    return mode == QClipboard::Clipboard;
}

bool Clipboard::ownsMode(QClipboard::Mode mode) const
{
    if (mode == QClipboard::Clipboard) {
        return waylandServer()->seat()->selection() == m_dataSource.get();
    }
    return false;
}

}
}

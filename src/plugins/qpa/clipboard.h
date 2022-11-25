/*
    SPDX-FileCopyrightText: 2022 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "wayland/abstract_data_source.h"

#include <QMimeData>

#include <qpa/qplatformclipboard.h>

namespace KWin
{
namespace QPA
{

class DataSource : public KWaylandServer::AbstractDataSource
{
    Q_OBJECT

public:
    explicit DataSource(QMimeData *mimeData, QObject *parent = nullptr);

    void requestData(const QString &mimeType, qint32 fd) override;
    void cancel() override;
    QStringList mimeTypes() const override;

private:
    QMimeData *m_mimeData;
};

class DataSourceMimeData : public QMimeData
{
public:
    explicit DataSourceMimeData(KWaylandServer::AbstractDataSource *dataSource);

protected:
    QVariant retrieveData(const QString &mimeType, QVariant::Type preferredType) const override;

private:
    KWaylandServer::AbstractDataSource *m_dataSource;
};

class Clipboard : public QObject, public QPlatformClipboard
{
    Q_OBJECT

public:
    Clipboard();
    ~Clipboard() override;

    void initialize();

    QMimeData *mimeData(QClipboard::Mode mode = QClipboard::Clipboard) override;
    void setMimeData(QMimeData *data, QClipboard::Mode mode = QClipboard::Clipboard) override;
    bool supportsMode(QClipboard::Mode mode) const override;
    bool ownsMode(QClipboard::Mode mode) const override;

private:
    QMimeData m_emptyData;
    std::unique_ptr<DataSourceMimeData> m_mimeData;
    std::unique_ptr<DataSource> m_dataSource;
};

}
}

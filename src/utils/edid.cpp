/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "edid.h"

#include "config-kwin.h"

#include <QFile>
#include <QStandardPaths>

#include <KLocalizedString>
#include <optional>

extern "C" {
#include <libdisplay-info/edid.h>
#include <libdisplay-info/info.h>
}

namespace KWin
{

static std::optional<QByteArray> parseEisaId(const uint8_t *data)
{
    for (int i = 72; i <= 108; i += 18) {
        // Skip the block if it isn't used as monitor descriptor.
        if (data[i]) {
            continue;
        }
        if (data[i + 1]) {
            continue;
        }

        // We have found the EISA ID, it's stored as ASCII.
        if (data[i + 3] == 0xfe) {
            return QByteArray(reinterpret_cast<const char *>(&data[i + 5]), 12).trimmed();
        }
    }

    return std::nullopt;
}

static QByteArray parseVendor(const QByteArray &pnpId)
{
    // Map to vendor name
    QFile pnpFile(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("hwdata/pnp.ids")));
    if (pnpFile.exists() && pnpFile.open(QIODevice::ReadOnly)) {
        while (!pnpFile.atEnd()) {
            const auto line = pnpFile.readLine();
            if (line.startsWith(pnpId)) {
                return line.mid(4).trimmed();
            }
        }
    }

    return {};
}

Edid::Edid()
{
}

Edid::Edid(const void *data, uint32_t size)
{
    m_info = di_info_parse_edid(data, size);
    if (!m_info) {
        return;
    }

    m_raw.resize(size);
    memcpy(m_raw.data(), data, size);

    const di_edid *edid = di_info_get_edid(m_info);
    const di_edid_vendor_product *productInfo = di_edid_get_vendor_product(edid);
    const di_edid_screen_size *screenSize = di_edid_get_screen_size(edid);

    m_physicalSize = QSize(screenSize->width_cm, screenSize->height_cm) * 10;
    m_eisaId = parseEisaId(reinterpret_cast<const uint8_t *>(data)).value_or(QByteArray(productInfo->manufacturer, 3));
    m_monitorName = di_info_get_product_name(m_info);
    m_serialNumber = QByteArray::number(productInfo->serial);
    m_vendor = parseVendor(QByteArray(productInfo->manufacturer, 3));

    m_isValid = true;
}

bool Edid::isValid() const
{
    return m_isValid;
}

QSize Edid::physicalSize() const
{
    return m_physicalSize;
}

QByteArray Edid::eisaId() const
{
    return m_eisaId;
}

QByteArray Edid::monitorName() const
{
    return m_monitorName;
}

QByteArray Edid::serialNumber() const
{
    return m_serialNumber;
}

QByteArray Edid::vendor() const
{
    return m_vendor;
}

QByteArray Edid::raw() const
{
    return m_raw;
}

QString Edid::manufacturerString() const
{
    QString manufacturer;
    if (!m_vendor.isEmpty()) {
        manufacturer = QString::fromLatin1(m_vendor);
    } else if (!m_eisaId.isEmpty()) {
        manufacturer = QString::fromLatin1(m_eisaId);
    }
    return manufacturer;
}

QString Edid::nameString() const
{
    if (!m_monitorName.isEmpty()) {
        QString m = QString::fromLatin1(m_monitorName);
        if (!m_serialNumber.isEmpty()) {
            m.append('/');
            m.append(QString::fromLatin1(m_serialNumber));
        }
        return m;
    } else if (!m_serialNumber.isEmpty()) {
        return QString::fromLatin1(m_serialNumber);
    } else {
        return i18n("unknown");
    }
}

} // namespace KWin

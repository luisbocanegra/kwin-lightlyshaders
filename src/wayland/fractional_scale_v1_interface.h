/*
    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include <QPointer>
#include <qwayland-server-fractional-scale-v1.h>

namespace KWaylandServer
{

class Display;
class FractionalScaleManagerV1Private;
class SurfaceInterface;

class FractionalScaleManagerV1 : public QObject
{
    Q_OBJECT
public:
    FractionalScaleManagerV1(Display *display, QObject *parent);
    ~FractionalScaleManagerV1();

private:
    FractionalScaleManagerV1Private *d;
};

class FractionalScaleV1 : private QtWaylandServer::wp_fractional_scale_v1
{
public:
    FractionalScaleV1(SurfaceInterface *surface, wl_resource *resource);

    void setFractionalScale(double scale);

private:
    void wp_fractional_scale_v1_set_scale_factor(Resource *resource, uint32_t scale_8_24) override;
    void wp_fractional_scale_v1_destroy(Resource *resource) override;

    const QPointer<SurfaceInterface> m_surface;
};

}

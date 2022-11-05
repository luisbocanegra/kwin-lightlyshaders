/*
    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#include "fractional_scale_v1_interface.h"

#include "display.h"
#include "surface_interface_p.h"

namespace KWaylandServer
{

static constexpr uint32_t s_version = 1;

class FractionalScaleManagerV1Private : public QtWaylandServer::wp_fractional_scale_manager_v1
{
public:
    FractionalScaleManagerV1Private(Display *display);

private:
    void wp_fractional_scale_manager_v1_destroy_global() override;
    void wp_fractional_scale_manager_v1_get_fractional_scale(Resource *resource, uint32_t id, struct ::wl_resource *surface) override;
};

FractionalScaleManagerV1::FractionalScaleManagerV1(Display *display, QObject *parent)
    : QObject(parent)
    , d(new FractionalScaleManagerV1Private(display))
{
}

FractionalScaleManagerV1::~FractionalScaleManagerV1()
{
    d->globalRemove();
}

FractionalScaleManagerV1Private::FractionalScaleManagerV1Private(Display *display)
    : QtWaylandServer::wp_fractional_scale_manager_v1(*display, s_version)
{
}

void FractionalScaleManagerV1Private::wp_fractional_scale_manager_v1_destroy_global()
{
    delete this;
}

void FractionalScaleManagerV1Private::wp_fractional_scale_manager_v1_get_fractional_scale(Resource *resource, uint32_t id, struct ::wl_resource *surface)
{
    if (isGlobalRemoved()) {
        return;
    }
    SurfaceInterface *surf = SurfaceInterface::get(surface);
    SurfaceInterfacePrivate *surfPrivate = SurfaceInterfacePrivate::get(surf);
    if (surfPrivate->fractionalScale) {
        wl_resource_post_error(resource->handle, error_fractional_scale_exists, "The surface already has a fractional_scale_v1 associated with it");
        return;
    }
    wl_resource *fractionalScaleResource = wl_resource_create(resource->client(), &wp_fractional_scale_v1_interface, s_version, id);
    if (!fractionalScaleResource) {
        wl_resource_post_no_memory(resource->handle);
        return;
    }
    surfPrivate->fractionalScale = new FractionalScaleV1(surf, fractionalScaleResource);
}

FractionalScaleV1::FractionalScaleV1(SurfaceInterface *surface, wl_resource *resource)
    : QtWaylandServer::wp_fractional_scale_v1(resource)
    , m_surface(surface)
{
}

void FractionalScaleV1::setFractionalScale(double scale)
{
    if (SurfaceInterface *surf = m_surface) {
        SurfaceInterfacePrivate *surfPrivate = SurfaceInterfacePrivate::get(surf);
        if (surfPrivate->compositorToClientScale != scale) {
            send_scale_factor(resource()->handle, scale * (1UL << 24));
            surfPrivate->compositorToClientScale = scale;
        }
    }
}

void FractionalScaleV1::wp_fractional_scale_v1_set_scale_factor(Resource *resource, uint32_t scale_8_24)
{
    if (SurfaceInterface *surf = m_surface) {
        SurfaceInterfacePrivate::get(surf)->clientToCompositorScale = scale_8_24 / double(1UL << 24);
    }
}

void FractionalScaleV1::wp_fractional_scale_v1_destroy(Resource *resource)
{
    if (SurfaceInterface *surf = m_surface) {
        SurfaceInterfacePrivate *surfPrivate = SurfaceInterfacePrivate::get(surf);
        surfPrivate->clientToCompositorScale = 1;
        surfPrivate->fractionalScale = nullptr;
    }
    delete this;
}

}

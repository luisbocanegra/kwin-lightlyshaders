/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "x11_windowed_egl_backend.h"
#include "x11_windowed_backend.h"
#include "x11_windowed_logging.h"
#include "x11_windowed_output.h"
#include "../../drm/gbm_dmabuf.h"

#include "basiceglsurfacetexture_internal.h"
#include "basiceglsurfacetexture_wayland.h"

#include <drm_fourcc.h>
#include <gbm.h>
#include <xcb/dri3.h>

namespace KWin
{

X11WindowedEglLayerBuffer::X11WindowedEglLayerBuffer(const QSize &size, uint32_t format, const QVector<uint64_t> &modifiers, xcb_drawable_t drawable, X11WindowedEglBackend *backend)
    : m_backend(backend)
{
    X11WindowedBackend *x11Backend = backend->backend();

    m_bo = createGbmBo(x11Backend->gbmDevice(), size, format, modifiers);
    if (!m_bo) {
        qCCritical(KWIN_X11WINDOWED) << "Failed to allocate a buffer for an output layer";
        return;
    }

    m_pixmap = xcb_generate_id(x11Backend->connection());
    if (x11Backend->driMajorVersion() >= 1 || x11Backend->driMinorVersion() >= 2) {
        DmaBufAttributes attributes = dmaBufAttributesForBo(m_bo);

        // xcb_dri3_pixmap_from_buffers() takes the ownership of the file descriptors.
        int fds[4] = {
            attributes.fd[0].take(),
            attributes.fd[1].take(),
            attributes.fd[2].take(),
            attributes.fd[3].take(),
        };
        xcb_dri3_pixmap_from_buffers(x11Backend->connection(), m_pixmap, drawable, attributes.planeCount,
                                    size.width(), size.height(),
                                    attributes.pitch[0], attributes.offset[0],
                                    attributes.pitch[1], attributes.offset[1],
                                    attributes.pitch[2], attributes.offset[2],
                                    attributes.pitch[3], attributes.offset[3],
                                    24, 32, attributes.modifier, fds);
    } else {
        DmaBufAttributes attributes = dmaBufAttributesForBo(m_bo);

        // xcb_dri3_pixmap_from_buffer() takes the ownership of the file descriptor.
        xcb_dri3_pixmap_from_buffer(x11Backend->connection(), m_pixmap, drawable,
                                    size.height() * attributes.pitch[0], size.width(), size.height(),
                                    attributes.pitch[0], 24, 32, attributes.fd[0].take());
    }

    m_texture = backend->importDmaBufAsTexture(dmaBufAttributesForBo(m_bo));
    m_framebuffer = std::make_unique<GLFramebuffer>(m_texture.get());
}

X11WindowedEglLayerBuffer::~X11WindowedEglLayerBuffer()
{
    m_texture.reset();
    m_framebuffer.reset();

    if (m_pixmap) {
        xcb_free_pixmap(m_backend->backend()->connection(), m_pixmap);
    }
    if (m_bo) {
        gbm_bo_destroy(m_bo);
    }
}

xcb_pixmap_t X11WindowedEglLayerBuffer::pixmap() const
{
    return m_pixmap;
}

GLFramebuffer *X11WindowedEglLayerBuffer::framebuffer() const
{
    return m_framebuffer.get();
}

int X11WindowedEglLayerBuffer::age() const
{
    return m_age;
}

X11WindowedEglLayerSwapchain::X11WindowedEglLayerSwapchain(const QSize &size, uint32_t format, const QVector<uint64_t> &modifiers, xcb_drawable_t drawable, X11WindowedEglBackend *backend)
    : m_backend(backend)
    , m_size(size)
{
    for (int i = 0; i < 2; ++i) {
        m_buffers.append(std::make_shared<X11WindowedEglLayerBuffer>(size, format, modifiers, drawable, backend));
    }
}

X11WindowedEglLayerSwapchain::~X11WindowedEglLayerSwapchain()
{
}

QSize X11WindowedEglLayerSwapchain::size() const
{
    return m_size;
}

std::shared_ptr<X11WindowedEglLayerBuffer> X11WindowedEglLayerSwapchain::acquire()
{
    m_index = (m_index + 1) % m_buffers.count();
    return m_buffers[m_index];
}

void X11WindowedEglLayerSwapchain::release(std::shared_ptr<X11WindowedEglLayerBuffer> buffer)
{
    Q_ASSERT(m_buffers[m_index] == buffer);

    for (qsizetype i = 0; i < m_buffers.count(); ++i) {
        if (m_buffers[i] == buffer) {
            m_buffers[i]->m_age = 1;
        } else if (m_buffers[i]->m_age > 0) {
            m_buffers[i]->m_age++;
        }
    }
}

X11WindowedEglPrimaryLayer::X11WindowedEglPrimaryLayer(X11WindowedEglBackend *backend, X11WindowedOutput *output)
    : m_output(output)
    , m_backend(backend)
{
}

std::optional<OutputLayerBeginFrameInfo> X11WindowedEglPrimaryLayer::beginFrame()
{
    eglMakeCurrent(m_backend->eglDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE, m_backend->context());

    const QSize bufferSize = m_output->pixelSize();
    if (!m_swapchain || m_swapchain->size() != bufferSize) {
        const uint32_t format = DRM_FORMAT_XRGB8888;
        const QVector<uint64_t> modifiers = {DRM_FORMAT_MOD_INVALID};
        m_swapchain = std::make_unique<X11WindowedEglLayerSwapchain>(bufferSize, format, modifiers, m_output->window(), m_backend);
    }

    m_buffer = m_swapchain->acquire();

    QRegion repaint = m_output->exposedArea() + m_output->rect();
    m_output->clearExposedArea();

    GLFramebuffer::pushFramebuffer(m_buffer->framebuffer());
    return OutputLayerBeginFrameInfo{
        .renderTarget = RenderTarget(m_buffer->framebuffer()),
        .repaint = repaint,
    };
}

bool X11WindowedEglPrimaryLayer::endFrame(const QRegion &renderedRegion, const QRegion &damagedRegion)
{
    GLFramebuffer::popFramebuffer();
    return true;
}

void X11WindowedEglPrimaryLayer::present()
{
    xcb_xfixes_region_t valid = 0;
    xcb_xfixes_region_t update = 0;
    uint32_t serial = 0;
    uint32_t options = 0;
    uint64_t targetMsc = 0;

    xcb_present_pixmap(m_output->backend()->connection(),
                       m_output->window(),
                       m_buffer->pixmap(),
                       serial,
                       valid,
                       update,
                       0,
                       0,
                       XCB_NONE,
                       XCB_NONE,
                       XCB_NONE,
                       options,
                       targetMsc,
                       0,
                       0,
                       0,
                       nullptr);

    m_swapchain->release(m_buffer);
}

X11WindowedEglCursorLayer::X11WindowedEglCursorLayer(X11WindowedEglBackend *backend, X11WindowedOutput *output)
    : m_output(output)
    , m_backend(backend)
{
}

X11WindowedEglCursorLayer::~X11WindowedEglCursorLayer()
{
    eglMakeCurrent(m_backend->eglDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE, m_backend->context());
    m_framebuffer.reset();
    m_texture.reset();
}

QPoint X11WindowedEglCursorLayer::hotspot() const
{
    return m_hotspot;
}

void X11WindowedEglCursorLayer::setHotspot(const QPoint &hotspot)
{
    m_hotspot = hotspot;
}

QSize X11WindowedEglCursorLayer::size() const
{
    return m_size;
}

void X11WindowedEglCursorLayer::setSize(const QSize &size)
{
    m_size = size;
}

std::optional<OutputLayerBeginFrameInfo> X11WindowedEglCursorLayer::beginFrame()
{
    eglMakeCurrent(m_backend->eglDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE, m_backend->context());

    const QSize bufferSize = m_size.expandedTo(QSize(64, 64));
    if (!m_texture || m_texture->size() != bufferSize) {
        m_texture = std::make_unique<GLTexture>(GL_RGBA8, bufferSize);
        m_framebuffer = std::make_unique<GLFramebuffer>(m_texture.get());
    }

    GLFramebuffer::pushFramebuffer(m_framebuffer.get());
    return OutputLayerBeginFrameInfo{
        .renderTarget = RenderTarget(m_framebuffer.get()),
        .repaint = infiniteRegion(),
    };
}

bool X11WindowedEglCursorLayer::endFrame(const QRegion &renderedRegion, const QRegion &damagedRegion)
{
    GLFramebuffer::popFramebuffer();

    const QImage buffer = m_texture->toImage().mirrored(false, true);
    m_output->cursor()->update(buffer, m_hotspot);

    return true;
}

X11WindowedEglBackend::X11WindowedEglBackend(X11WindowedBackend *backend)
    : EglOnXBackend(backend->connection(), backend->display(), backend->rootWindow())
    , m_backend(backend)
{
}

X11WindowedEglBackend::~X11WindowedEglBackend()
{
    cleanup();
}

X11WindowedBackend *X11WindowedEglBackend::backend() const
{
    return m_backend;
}

void X11WindowedEglBackend::init()
{
    EglOnXBackend::init();

    if (!isFailed()) {
        initWayland();
    }
}

void X11WindowedEglBackend::cleanupSurfaces()
{
    m_outputs.clear();
}

bool X11WindowedEglBackend::createSurfaces()
{
    const auto &outputs = m_backend->outputs();
    for (const auto &output : outputs) {
        X11WindowedOutput *x11Output = static_cast<X11WindowedOutput *>(output);
        m_outputs[output] = Layers{
            .primaryLayer = std::make_unique<X11WindowedEglPrimaryLayer>(this, x11Output),
            .cursorLayer = std::make_unique<X11WindowedEglCursorLayer>(this, x11Output),
        };
    }
    return true;
}

void X11WindowedEglBackend::present(Output *output)
{
    m_outputs[output].primaryLayer->present();
}

OutputLayer *X11WindowedEglBackend::primaryLayer(Output *output)
{
    return m_outputs[output].primaryLayer.get();
}

X11WindowedEglCursorLayer *X11WindowedEglBackend::cursorLayer(Output *output)
{
    return m_outputs[output].cursorLayer.get();
}

std::unique_ptr<SurfaceTexture> X11WindowedEglBackend::createSurfaceTextureWayland(SurfacePixmapWayland *pixmap)
{
    return std::make_unique<BasicEGLSurfaceTextureWayland>(this, pixmap);
}

std::unique_ptr<SurfaceTexture> X11WindowedEglBackend::createSurfaceTextureInternal(SurfacePixmapInternal *pixmap)
{
    return std::make_unique<BasicEGLSurfaceTextureInternal>(this, pixmap);
}

} // namespace

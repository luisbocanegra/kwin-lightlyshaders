/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../common/x11_common_egl_backend.h"
#include "core/outputlayer.h"
#include "kwinglutils.h"

#include <QMap>

struct gbm_bo;

namespace KWin
{

class X11WindowedBackend;
class X11WindowedOutput;
class X11WindowedEglBackend;

class X11WindowedEglLayerBuffer
{
public:
    X11WindowedEglLayerBuffer(const QSize &size, uint32_t format, const QVector<uint64_t> &modifiers, xcb_drawable_t drawable, X11WindowedEglBackend *backend);
    ~X11WindowedEglLayerBuffer();

    xcb_pixmap_t pixmap() const;
    GLFramebuffer *framebuffer() const;
    int age() const;

private:
    X11WindowedEglBackend *m_backend;
    xcb_pixmap_t m_pixmap = XCB_PIXMAP_NONE;
    gbm_bo *m_bo = nullptr;
    std::unique_ptr<GLFramebuffer> m_framebuffer;
    std::shared_ptr<GLTexture> m_texture;
    int m_age = 0;
    friend class X11WindowedEglLayerSwapchain;
};

class X11WindowedEglLayerSwapchain
{
public:
    X11WindowedEglLayerSwapchain(const QSize &size, uint32_t format, const QVector<uint64_t> &modifiers, xcb_drawable_t drawable, X11WindowedEglBackend *backend);
    ~X11WindowedEglLayerSwapchain();

    QSize size() const;

    std::shared_ptr<X11WindowedEglLayerBuffer> acquire();
    void release(std::shared_ptr<X11WindowedEglLayerBuffer> buffer);

private:
    X11WindowedEglBackend *m_backend;
    QSize m_size;
    QVector<std::shared_ptr<X11WindowedEglLayerBuffer>> m_buffers;
    int m_index = 0;
};

class X11WindowedEglPrimaryLayer : public OutputLayer
{
public:
    X11WindowedEglPrimaryLayer(X11WindowedEglBackend *backend, X11WindowedOutput *output);

    std::optional<OutputLayerBeginFrameInfo> beginFrame() override;
    bool endFrame(const QRegion &renderedRegion, const QRegion &damagedRegion) override;

    void present();

private:
    std::unique_ptr<X11WindowedEglLayerSwapchain> m_swapchain;
    std::shared_ptr<X11WindowedEglLayerBuffer> m_buffer;
    X11WindowedOutput *const m_output;
    X11WindowedEglBackend *const m_backend;
};

class X11WindowedEglCursorLayer : public OutputLayer
{
    Q_OBJECT

public:
    X11WindowedEglCursorLayer(X11WindowedEglBackend *backend, X11WindowedOutput *output);
    ~X11WindowedEglCursorLayer() override;

    QPoint hotspot() const;
    void setHotspot(const QPoint &hotspot);

    QSize size() const;
    void setSize(const QSize &size);

    std::optional<OutputLayerBeginFrameInfo> beginFrame() override;
    bool endFrame(const QRegion &renderedRegion, const QRegion &damagedRegion) override;

private:
    X11WindowedOutput *const m_output;
    X11WindowedEglBackend *const m_backend;
    std::unique_ptr<GLFramebuffer> m_framebuffer;
    std::unique_ptr<GLTexture> m_texture;
    QPoint m_hotspot;
    QSize m_size;
};

/**
 * @brief OpenGL Backend using Egl windowing system over an X overlay window.
 */
class X11WindowedEglBackend : public EglOnXBackend
{
    Q_OBJECT

public:
    explicit X11WindowedEglBackend(X11WindowedBackend *backend);
    ~X11WindowedEglBackend() override;

    X11WindowedBackend *backend() const;

    std::unique_ptr<SurfaceTexture> createSurfaceTextureInternal(SurfacePixmapInternal *pixmap) override;
    std::unique_ptr<SurfaceTexture> createSurfaceTextureWayland(SurfacePixmapWayland *pixmap) override;
    void init() override;
    void endFrame(Output *output, const QRegion &renderedRegion, const QRegion &damagedRegion);
    void present(Output *output) override;
    OutputLayer *primaryLayer(Output *output) override;
    X11WindowedEglCursorLayer *cursorLayer(Output *output);

protected:
    void cleanupSurfaces() override;
    bool createSurfaces() override;

private:
    struct Layers
    {
        std::unique_ptr<X11WindowedEglPrimaryLayer> primaryLayer;
        std::unique_ptr<X11WindowedEglCursorLayer> cursorLayer;
    };

    std::map<Output *, Layers> m_outputs;
    X11WindowedBackend *m_backend;
};

} // namespace KWin

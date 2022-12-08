/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drm_gbm_surface.h"

#include <errno.h>
#include <gbm.h>

#include "drm_backend.h"
#include "drm_egl_backend.h"
#include "drm_gpu.h"
#include "drm_logging.h"
#include "kwineglutils_p.h"
#include "kwinglplatform.h"

namespace KWin
{

GbmSwapchain::GbmSwapchain(DrmGpu *gpu, gbm_bo *initialBuffer, uint32_t flags)
    : m_gpu(gpu)
    , m_size(gbm_bo_get_width(initialBuffer), gbm_bo_get_height(initialBuffer))
    , m_format(gbm_bo_get_format(initialBuffer))
    , m_modifier(gbm_bo_get_modifier(initialBuffer))
    , m_flags(flags)
    , m_buffers({std::make_pair(initialBuffer, 0)})
{
}

GbmSwapchain::~GbmSwapchain()
{
    while (!m_buffers.empty()) {
        gbm_bo_destroy(m_buffers.back().first);
        m_buffers.pop_back();
    }
}

std::pair<std::shared_ptr<GbmBuffer>, QRegion> GbmSwapchain::acquire()
{
    for (auto &[bo, bufferAge] : m_buffers) {
        bufferAge++;
    }
    if (m_buffers.empty()) {
        gbm_bo *newBo = gbm_bo_create_with_modifiers(m_gpu->gbmDevice(), m_size.width(), m_size.height(), m_format, &m_modifier, 1);
        if (!newBo) {
            qCWarning(KWIN_DRM) << "Creating gbm buffer failed!" << strerror(errno);
            return std::make_pair(nullptr, infiniteRegion());
        } else {
            return std::make_pair(std::make_shared<GbmBuffer>(newBo, shared_from_this()), infiniteRegion());
        }
    } else {
        const auto [bo, bufferAge] = m_buffers.front();
        m_buffers.pop_front();
        return std::make_pair(std::make_shared<GbmBuffer>(bo, shared_from_this()),
                              m_damageJournal.accumulate(bufferAge, infiniteRegion()));
    }
}

void GbmSwapchain::damage(const QRegion &damage)
{
    m_damageJournal.add(damage);
}

void GbmSwapchain::releaseBuffer(GbmBuffer *buffer)
{
    if (m_buffers.size() < 3) {
        m_buffers.push_back(std::make_pair(buffer->bo(), 1));
    } else {
        gbm_bo_destroy(buffer->bo());
    }
}

std::variant<std::shared_ptr<GbmSwapchain>, GbmSwapchain::Error> GbmSwapchain::createSwapchain(DrmGpu *gpu, const QSize &size, uint32_t format, uint32_t flags)
{
    gbm_bo *bo = gbm_bo_create(gpu->gbmDevice(), size.width(), size.height(), format, flags);
    if (bo) {
        return std::make_shared<GbmSwapchain>(gpu, bo, flags);
    } else {
        qCWarning(KWIN_DRM) << "Creating initial gbm buffer failed!" << strerror(errno);
        return Error::Unknown;
    }
}

std::variant<std::shared_ptr<GbmSwapchain>, GbmSwapchain::Error> GbmSwapchain::createSwapchain(DrmGpu *gpu, const QSize &size, uint32_t format, QVector<uint64_t> modifiers)
{
    gbm_bo *bo = gbm_bo_create_with_modifiers(gpu->gbmDevice(), size.width(), size.height(), format, modifiers.constData(), modifiers.size());
    if (bo) {
        // scanout is implicitly assumed with gbm_bo_create_with_modifiers
        return std::make_shared<GbmSwapchain>(gpu, bo, GBM_BO_USE_SCANOUT);
    } else {
        if (errno == ENOSYS) {
            return Error::ModifiersUnsupported;
        } else {
            qCWarning(KWIN_DRM) << "Creating initial gbm buffer failed!" << strerror(errno);
            return Error::Unknown;
        }
    }
}

DrmGpu *GbmSwapchain::gpu() const
{
    return m_gpu;
}

QSize GbmSwapchain::size() const
{
    return m_size;
}

uint32_t GbmSwapchain::format() const
{
    return m_format;
}

uint64_t GbmSwapchain::modifier() const
{
    return m_modifier;
}

uint32_t GbmSwapchain::flags() const
{
    return m_flags;
}

GbmSurface::GbmSurface(EglGbmBackend *backend, const QSize &size, uint32_t format, const QVector<uint64_t> &modifiers, uint32_t flags, gbm_surface *surface, EGLSurface eglSurface)
    : m_surface(surface)
    , m_eglBackend(backend)
    , m_eglSurface(eglSurface)
    , m_size(size)
    , m_format(format)
    , m_modifiers(modifiers)
    , m_flags(flags)
    , m_fbo(std::make_unique<GLFramebuffer>(0, size))
{
}

GbmSurface::~GbmSurface()
{
    if (m_eglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(m_eglBackend->eglDisplay(), m_eglSurface);
    }
    if (m_surface) {
        gbm_surface_destroy(m_surface);
    }
}

bool GbmSurface::makeContextCurrent() const
{
    if (eglMakeCurrent(m_eglBackend->eglDisplay(), m_eglSurface, m_eglSurface, m_eglBackend->context()) == EGL_FALSE) {
        qCCritical(KWIN_DRM) << "eglMakeCurrent failed:" << getEglErrorString();
        return false;
    }
    if (!GLPlatform::instance()->isGLES()) {
        glDrawBuffer(GL_BACK);
        glReadBuffer(GL_BACK);
    }
    return true;
}

std::shared_ptr<GbmBuffer> GbmSurface::swapBuffers(const QRegion &dirty)
{
    auto error = eglSwapBuffers(m_eglBackend->eglDisplay(), m_eglSurface);
    if (error != EGL_TRUE) {
        qCCritical(KWIN_DRM) << "an error occurred while swapping buffers" << getEglErrorString();
        return nullptr;
    }
    auto bo = gbm_surface_lock_front_buffer(m_surface);
    if (!bo) {
        return nullptr;
    }
    if (m_eglBackend->supportsBufferAge()) {
        eglQuerySurface(m_eglBackend->eglDisplay(), m_eglSurface, EGL_BUFFER_AGE_EXT, &m_bufferAge);
        m_damageJournal.add(dirty);
    }
    return std::make_shared<GbmBuffer>(m_eglBackend->gpu(), bo, shared_from_this());
}

void GbmSurface::releaseBuffer(GbmBuffer *buffer)
{
    gbm_surface_release_buffer(m_surface, buffer->bo());
}

GLFramebuffer *GbmSurface::fbo() const
{
    return m_fbo.get();
}

EGLSurface GbmSurface::eglSurface() const
{
    return m_eglSurface;
}

QSize GbmSurface::size() const
{
    return m_size;
}

uint32_t GbmSurface::format() const
{
    return m_format;
}

QVector<uint64_t> GbmSurface::modifiers() const
{
    return m_modifiers;
}

int GbmSurface::bufferAge() const
{
    return m_bufferAge;
}

QRegion GbmSurface::repaintRegion() const
{
    if (m_eglBackend->supportsBufferAge()) {
        return m_damageJournal.accumulate(m_bufferAge, infiniteRegion());
    } else {
        return infiniteRegion();
    }
}

uint32_t GbmSurface::flags() const
{
    return m_flags;
}

std::variant<std::shared_ptr<GbmSurface>, GbmSurface::Error> GbmSurface::createSurface(EglGbmBackend *backend, const QSize &size, uint32_t format, uint32_t flags, EGLConfig config)
{
    gbm_surface *surface = gbm_surface_create(backend->gpu()->gbmDevice(), size.width(), size.height(), format, flags);
    if (!surface) {
        qCWarning(KWIN_DRM) << "Creating gbm surface failed!" << strerror(errno);
        return Error::Unknown;
    }
    EGLSurface eglSurface = eglCreatePlatformWindowSurfaceEXT(backend->eglDisplay(), config, surface, nullptr);
    if (eglSurface == EGL_NO_SURFACE) {
        qCCritical(KWIN_DRM) << "Creating EGL surface failed!" << getEglErrorString();
        gbm_surface_destroy(surface);
        return Error::Unknown;
    }
    return std::make_shared<GbmSurface>(backend, size, format, QVector<uint64_t>{}, flags, surface, eglSurface);
}

std::variant<std::shared_ptr<GbmSurface>, GbmSurface::Error> GbmSurface::createSurface(EglGbmBackend *backend, const QSize &size, uint32_t format, QVector<uint64_t> modifiers, EGLConfig config)
{
    gbm_surface *surface = gbm_surface_create_with_modifiers(backend->gpu()->gbmDevice(), size.width(), size.height(), format, modifiers.data(), modifiers.size());
    if (!surface) {
        if (errno == ENOSYS) {
            return Error::ModifiersUnsupported;
        } else {
            qCWarning(KWIN_DRM) << "Creating gbm surface failed!" << strerror(errno);
            return Error::Unknown;
        }
    }
    EGLSurface eglSurface = eglCreatePlatformWindowSurfaceEXT(backend->eglDisplay(), config, surface, nullptr);
    if (eglSurface == EGL_NO_SURFACE) {
        qCCritical(KWIN_DRM) << "Creating EGL surface failed!" << getEglErrorString();
        gbm_surface_destroy(surface);
        return Error::Unknown;
    }
    return std::make_shared<GbmSurface>(backend, size, format, modifiers, 0, surface, eglSurface);
}
}

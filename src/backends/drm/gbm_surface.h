/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QVector>
#include <cstdint>
#include <epoxy/egl.h>
#include <memory>
#include <variant>

#include "drm_buffer_gbm.h"
#include "drm_surface.h"
#include "utils/damagejournal.h"

struct gbm_device;
struct gbm_surface;

namespace KWin
{

class GLFramebuffer;
class EglGbmBackend;

class GbmSurface : public std::enable_shared_from_this<GbmSurface>, public DrmSurface
{
public:
    explicit GbmSurface(EglGbmBackend *backend, const QSize &size, uint32_t format, const QVector<uint64_t> &modifiers, uint32_t flags, gbm_surface *surface, EGLSurface eglSurface);
    ~GbmSurface();

    bool makeContextCurrent() const;

    std::shared_ptr<GbmBuffer> swapBuffers(const QRegion &dirty);
    void releaseBuffer(GbmBuffer *buffer);

    GLFramebuffer *fbo() const;

    EGLSurface eglSurface() const;
    bool isValid() const;
    QVector<uint64_t> modifiers() const;
    uint32_t flags() const;
    int bufferAge() const;
    QRegion repaintRegion() const;

    enum class Error {
        ModifiersUnsupported,
        EglError,
        Unknown
    };
    static std::variant<std::shared_ptr<GbmSurface>, Error> createSurface(EglGbmBackend *backend, const QSize &size, uint32_t format, uint32_t flags, EGLConfig config);
    static std::variant<std::shared_ptr<GbmSurface>, Error> createSurface(EglGbmBackend *backend, const QSize &size, uint32_t format, QVector<uint64_t> modifiers, EGLConfig config);

private:
    gbm_surface *m_surface;
    EglGbmBackend *const m_eglBackend;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;
    const QVector<uint64_t> m_modifiers;
    const uint32_t m_flags;
    int m_bufferAge = 0;
    DamageJournal m_damageJournal;

    QScopedPointer<GLFramebuffer> m_fbo;
};

}

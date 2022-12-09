/*
    SPDX-FileCopyrightText: 2022 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "rendertarget.h"
#include "kwinglutils.h"

namespace KWin
{

RenderTarget::RenderTarget()
{
}

RenderTarget::RenderTarget(GLFramebuffer *fbo, bool invertY)
    : m_nativeHandle(fbo)
    , m_invertY(invertY)
{
}

RenderTarget::RenderTarget(QImage *image, bool invertY)
    : m_nativeHandle(image)
    , m_invertY(invertY)
{
}

QSize RenderTarget::size() const
{
    if (auto fbo = std::get_if<GLFramebuffer *>(&m_nativeHandle)) {
        return (*fbo)->size();
    } else if (auto image = std::get_if<QImage *>(&m_nativeHandle)) {
        return (*image)->size();
    } else {
        Q_UNREACHABLE();
    }
}

bool RenderTarget::invertY() const
{
    return m_invertY;
}

RenderTarget::NativeHandle RenderTarget::nativeHandle() const
{
    return m_nativeHandle;
}

void RenderTarget::setDevicePixelRatio(qreal ratio)
{
    m_devicePixelRatio = ratio;
}

qreal RenderTarget::devicePixelRatio() const
{
    return m_devicePixelRatio;
}

} // namespace KWin

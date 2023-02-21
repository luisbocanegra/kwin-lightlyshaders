/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "outputscreencastsource.h"
#include "screencastutils.h"

#include "composite.h"
#include "core/output.h"
#include "core/renderloop.h"
#include "kwingltexture.h"
#include "kwinglutils.h"
#include "scene/workspacescene.h"

namespace KWin
{

OutputScreenCastSource::OutputScreenCastSource(Output *output, QObject *parent)
    : ScreenCastSource(parent)
    , m_output(output)
{
    connect(m_output, &QObject::destroyed, this, &ScreenCastSource::closed);
}

bool OutputScreenCastSource::hasAlphaChannel() const
{
    return true;
}

QSize OutputScreenCastSource::textureSize() const
{
    return m_output->pixelSize();
}

void OutputScreenCastSource::render(QImage *image)
{
    const std::shared_ptr<GLTexture> outputTexture = Compositor::self()->scene()->textureForOutput(m_output);
    if (outputTexture) {
        grabTexture(outputTexture.get(), image);
    }
}

void OutputScreenCastSource::render(GLFramebuffer *target)
{
    const std::shared_ptr<GLTexture> outputTexture = Compositor::self()->scene()->textureForOutput(m_output);
    if (!outputTexture) {
        return;
    }

    ShaderBinder shaderBinder(ShaderTrait::MapTexture);
    QMatrix4x4 projectionMatrix;
    projectionMatrix.scale(1, -1);
    projectionMatrix.ortho(QRect(QPoint(), textureSize() * m_output->scale()));
    shaderBinder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, projectionMatrix);

    GLFramebuffer::pushFramebuffer(target);
    outputTexture->bind();
    outputTexture->render(textureSize(), m_output->scale());
    outputTexture->unbind();
    GLFramebuffer::popFramebuffer();
}

std::chrono::nanoseconds OutputScreenCastSource::clock() const
{
    return m_output->renderLoop()->lastPresentationTimestamp();
}

} // namespace KWin

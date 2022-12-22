/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2022 Adrien Faveraux <ad1rie3@hotmail.fr>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#include "presentation_time_interface.h"

#include "display.h"
#include "output_interface.h"
#include "surface_interface_p.h"
#include <qwayland-server-presentation-time.h>

namespace KWaylandServer
{

static const uint32_t s_version = 1;

class PresentationManagerInterfacePrivate : public QtWaylandServer::wp_presentation
{
public:
    PresentationManagerInterfacePrivate(Display *display);

    clockid_t clockId = CLOCK_MONOTONIC;

protected:
    void wp_presentation_destroy(Resource *resource) override;
    void wp_presentation_bind_resource(Resource *resource) override;
    void wp_presentation_feedback(Resource *resource, struct ::wl_resource *surface, uint32_t callback) override;
};

PresentationManagerInterfacePrivate::PresentationManagerInterfacePrivate(Display *display)
    : QtWaylandServer::wp_presentation(*display, s_version)
{
}

void PresentationManagerInterfacePrivate::wp_presentation_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void PresentationManagerInterfacePrivate::wp_presentation_bind_resource(Resource *resource)
{
    send_clock_id(resource->handle, clockId);
}

void PresentationManagerInterfacePrivate::wp_presentation_feedback(Resource *resource, struct ::wl_resource *wlSurface, uint32_t callback)
{
    SurfaceInterface *surface = SurfaceInterface::get(wlSurface);
    if (!surface) {
        wl_resource_post_error(resource->handle, 0, "Invalid  surface");
        return;
    }
    SurfaceInterfacePrivate::get(surface)->addPresentationFeedback(new PresentationFeedbackInterface(resource->client(), callback));
}

PresentationManagerInterface::PresentationManagerInterface(Display *display, QObject *parent)
    : QObject(parent)
    , d(new PresentationManagerInterfacePrivate(display))
{
}

PresentationManagerInterface::~PresentationManagerInterface() = default;

clockid_t PresentationManagerInterface::clockId() const
{
    return d->clockId;
}

void PresentationManagerInterface::setClockId(clockid_t clockId)
{
    d->clockId = clockId;
    d->send_clock_id(clockId);
}

PresentationFeedbackInterface::PresentationFeedbackInterface(wl_client *client, uint32_t id)
    : QtWaylandServer::wp_presentation_feedback(client, id, s_version)
{
}

PresentationFeedbackInterface::~PresentationFeedbackInterface() = default;

void PresentationFeedbackInterface::wp_presentation_feedback_destroy_resource(Resource *resource)
{
    Q_EMIT resourceDestroyed();
}

void PresentationFeedbackInterface::sync(OutputInterface *output)
{
    if (output == nullptr) {
        return;
    }

    send_sync_output(output->getOutputRessource());
}

uint32_t toFlags(PresentationFeedbackInterface::Kinds kinds)
{
    using Kind = PresentationFeedbackInterface::Kind;
    uint32_t ret = 0;
    if (kinds.testFlag(Kind::Vsync)) {
        ret |= WP_PRESENTATION_FEEDBACK_KIND_VSYNC;
    }
    if (kinds.testFlag(Kind::HwClock)) {
        ret |= WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;
    }
    if (kinds.testFlag(Kind::HwCompletion)) {
        ret |= WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION;
    }
    if (kinds.testFlag(Kind::ZeroCopy)) {
        ret |= WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY;
    }
    return ret;
}

void PresentationFeedbackInterface::presented(uint32_t tvSecHi,
                                              uint32_t tvSecLo,
                                              uint32_t tvNsec,
                                              uint32_t refresh,
                                              uint32_t seqHi,
                                              uint32_t seqLo,
                                              PresentationFeedbackInterface::Kinds kinds)
{
    send_presented(tvSecHi, tvSecLo, tvNsec, refresh, seqHi, seqLo, toFlags(kinds));
}

void PresentationFeedbackInterface::discarded()
{
    send_discarded();
}

PresentationFeedbacks::PresentationFeedbacks(QObject *parent)
    : QObject(parent)
{
}

PresentationFeedbacks::~PresentationFeedbacks()
{
    discard();
}

bool PresentationFeedbacks::active()
{
    return !m_feedbacks.empty();
}

void PresentationFeedbacks::add(PresentationFeedbackInterface *feedback)
{
    connect(feedback, &PresentationFeedbackInterface::resourceDestroyed, this, [this, feedback] {
        m_feedbacks.erase(std::find(m_feedbacks.begin(), m_feedbacks.end(), feedback));
    });
    m_feedbacks.push_back(feedback);
}

void PresentationFeedbacks::setOutput(OutputInterface *output)
{
    assert(!m_output);
    m_output = output;
    QObject::connect(output, &OutputInterface::removed, this, &PresentationFeedbacks::handleOutputRemoval);
}

void PresentationFeedbacks::unsetOutput(const OutputInterface *output)
{
    if (m_output == output) {
        handleOutputRemoval();
    }
}

OutputInterface *PresentationFeedbacks::getOutput()
{
    return m_output;
}

void PresentationFeedbacks::handleOutputRemoval()
{
    assert(m_output);
    m_output = nullptr;
    discard();
}

PresentationFeedbackInterface::Kinds toKinds(SurfaceInterface::PresentationKinds kinds)
{
    using PresentationKind = SurfaceInterface::PresentationKind;
    using FeedbackKind = PresentationFeedbackInterface::Kind;

    PresentationFeedbackInterface::Kinds ret;
    if (kinds.testFlag(PresentationKind::Vsync)) {
        ret |= FeedbackKind::Vsync;
    }
    if (kinds.testFlag(PresentationKind::HwClock)) {
        ret |= FeedbackKind::HwClock;
    }
    if (kinds.testFlag(PresentationKind::HwCompletion)) {
        ret |= FeedbackKind::HwCompletion;
    }
    if (kinds.testFlag(PresentationKind::ZeroCopy)) {
        ret |= FeedbackKind::ZeroCopy;
    }
    return ret;
}

void PresentationFeedbacks::presented(std::chrono::nanoseconds current,
                                      std::chrono::nanoseconds next,
                                      SurfaceInterface::PresentationKinds kinds)
{
    uint32_t nSecToNextRefresh = uint32_t(next.count() - current.count());

    const auto secs = duration_cast<std::chrono::seconds>(current);
    uint32_t tvSecHi = secs.count() >> 32;
    uint32_t tvSecLo = secs.count() & 0xffffffff;
    uint32_t tvNsec = (current - secs).count();

    for (PresentationFeedbackInterface *fb : std::as_const(m_feedbacks)) {
        fb->sync(m_output);
        fb->presented(tvSecHi, tvSecLo, tvNsec, nSecToNextRefresh, 0, 0, toKinds(kinds));
        delete fb;
    }
    m_feedbacks.clear();
}

void PresentationFeedbacks::discard()
{
    for (PresentationFeedbackInterface *feedback : std::as_const(m_feedbacks)) {
        feedback->discarded();
        delete feedback;
    }
    m_feedbacks.clear();
}

}

/*
    SPDX-FileCopyrightText: 2022 Nicolas Fella <nicolas.fella@gmx.de>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "mousekeys.h"
#include "mousekeys_debug.h"

#include <cmath>

QString InputDevice::name() const
{
    return QStringLiteral("Mouse keys device");
}

QString InputDevice::sysName() const
{
    return {};
}

KWin::LEDs InputDevice::leds() const
{
    return {};
}

void InputDevice::setLeds(KWin::LEDs leds)
{
}

void InputDevice::setEnabled(bool enabled)
{
}

bool InputDevice::isEnabled() const
{
    return true;
}

bool InputDevice::isAlphaNumericKeyboard() const
{
    return false;
}

bool InputDevice::isKeyboard() const
{
    return false;
}

bool InputDevice::isLidSwitch() const
{
    return false;
}

bool InputDevice::isPointer() const
{
    return true;
}

bool InputDevice::isTabletModeSwitch() const
{
    return false;
}

bool InputDevice::isTabletPad() const
{
    return false;
}

bool InputDevice::isTabletTool() const
{
    return false;
}

bool InputDevice::isTouch() const
{
    return false;
}

bool InputDevice::isTouchpad() const
{
    return false;
}

MouseKeysFilter::MouseKeysFilter()
    : m_configWatcher(KConfigWatcher::create(KSharedConfig::openConfig("kaccessrc")))
{
    const QLatin1String groupName("Mouse");
    connect(m_configWatcher.get(), &KConfigWatcher::configChanged, this, [this, groupName](const KConfigGroup &group) {
        if (group.parent().name() == groupName) {
            loadConfig(group);
        }
    });
    loadConfig(m_configWatcher->config()->group(groupName));

    KWin::input()->addInputDevice(&m_inputDevice);
    KWin::input()->prependInputEventFilter(this);

    m_delayTimer.setInterval(m_delay);
    m_delayTimer.setSingleShot(true);
    connect(&m_delayTimer, &QTimer::timeout, this, &MouseKeysFilter::delayTriggered);

    m_repeatTimer.setInterval(m_interval);
    connect(&m_repeatTimer, &QTimer::timeout, this, &MouseKeysFilter::repeatTriggered);

    m_keyStates[Qt::Key_1] = false;
    m_keyStates[Qt::Key_2] = false;
    m_keyStates[Qt::Key_3] = false;
    m_keyStates[Qt::Key_4] = false;
    m_keyStates[Qt::Key_6] = false;
    m_keyStates[Qt::Key_7] = false;
    m_keyStates[Qt::Key_8] = false;
    m_keyStates[Qt::Key_9] = false;
}

void MouseKeysFilter::loadConfig(const KConfigGroup &group)
{
    m_enabled = group.readEntry<bool>("MouseKeys", false);
    m_stepsToMax = group.readEntry<int>("MKTimeToMax", 30);
    m_curve = group.readEntry<int>("MKCurve", 0);
    m_maxSpeed = group.readEntry<int>("MKMaxSpeed", 30);
    m_delay = group.readEntry<int>("MKDelay", 160);
    m_interval = group.readEntry<int>("MKInterval", 40);
}

QPointF deltaForKey(int key)
{
    static constexpr int delta = 5;

    switch (key) {
    case Qt::Key_1:
        return {-delta, delta};
    case Qt::Key_2:
        return {0, delta};
    case Qt::Key_3:
        return {delta, delta};
    case Qt::Key_4:
        return {-delta, 0};
    case Qt::Key_6:
        return {delta, 0};
    case Qt::Key_7:
        return {-delta, -delta};
    case Qt::Key_8:
        return {0, -delta};
    case Qt::Key_9:
        return {delta, -delta};
    }

    return {0, 0};
}

void MouseKeysFilter::delayTriggered()
{
    m_repeatTimer.start();
    movePointer(deltaForKey(m_currentKey));
}

double MouseKeysFilter::deltaFactorForStep(int step) const
{
    // The algorithm to compute the delta is described in the XKB library
    // specification (http://www.xfree86.org/current/XKBlib.pdf), section 10.5

    if (step > m_stepsToMax) {
        return m_maxSpeed;
    }

    double curve_factor = 1 + ((double)m_curve / 1000);

    return (m_maxSpeed / std::pow(m_stepsToMax, curve_factor)) * std::pow(step, curve_factor);
}

void MouseKeysFilter::repeatTriggered()
{
    ++m_currentStep;

    movePointer(deltaForKey(m_currentKey) * deltaFactorForStep(m_currentStep));
}

void MouseKeysFilter::handleKeyEvent(KWin::KeyEvent *event)
{
    const bool keyDown = event->type() == QEvent::KeyPress;

    if (!m_keyStates[event->key()] && keyDown && m_currentKey == 0) {
        m_keyStates[event->key()] = true;
        m_delayTimer.start();
        m_currentKey = event->key();

        movePointer(deltaForKey(event->key()));

    } else if (m_keyStates[event->key()] && !keyDown && event->key() == m_currentKey) {
        m_keyStates[event->key()] = false;
        m_delayTimer.stop();
        m_repeatTimer.stop();
        m_currentKey = 0;
        m_currentStep = 0;
    }
}

void MouseKeysFilter::movePointer(QPointF delta)
{
    const auto time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch());
    Q_EMIT m_inputDevice.pointerMotion(delta, delta, time, &m_inputDevice);
}

bool MouseKeysFilter::keyEvent(KWin::KeyEvent *event)
{
    if (!m_enabled) {
        return false;
    }

    // Only process numpad keys, not regular number keys
    if (!(event->modifiers().testFlag(Qt::KeypadModifier))) {
        return false;
    }

    if (event->key() == Qt::Key_1
        || event->key() == Qt::Key_2
        || event->key() == Qt::Key_3
        || event->key() == Qt::Key_4
        || event->key() == Qt::Key_6
        || event->key() == Qt::Key_7
        || event->key() == Qt::Key_8
        || event->key() == Qt::Key_9) {

        handleKeyEvent(event);

        return true;
    }

    return false;
}

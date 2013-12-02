/**************************************************************************
 * KWin - the KDE window manager                                          *
 * This file is part of the KDE project.                                  *
 *                                                                        *
 * Copyright (C) 2013 Antonis Tsiapaliokas <kok3rs@gmail.com>             *
 * Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>                 *
 *                                                                        *
 * This program is free software; you can redistribute it and/or modify   *
 * it under the terms of the GNU General Public License as published by   *
 * the Free Software Foundation; either version 2 of the License, or      *
 * (at your option) any later version.                                    *
 *                                                                        *
 * This program is distributed in the hope that it will be useful,        *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 * GNU General Public License for more details.                           *
 *                                                                        *
 * You should have received a copy of the GNU General Public License      *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.  *
 **************************************************************************/



#include "model.h"
#include <QApplication>
#include <QLayout>

#include <kcmodule.h>
#include <kservice.h>
#include <kdeclarative/kdeclarative.h>

class KWinCompositingKCM : public KCModule
{
    Q_OBJECT
public:
    explicit KWinCompositingKCM(QWidget* parent = 0, const QVariantList& args = QVariantList());
    virtual ~KWinCompositingKCM();

private:
    QScopedPointer<KWin::Compositing::EffectView> m_view;
};

KWinCompositingKCM::KWinCompositingKCM(QWidget* parent, const QVariantList& args)
    : KCModule(parent, args)
    , m_view(new KWin::Compositing::EffectView)
{
    KDeclarative kdeclarative;
    kdeclarative.setDeclarativeEngine(m_view->engine());
    kdeclarative.setupBindings();
    QVBoxLayout *vl = new QVBoxLayout(this);

    QWidget *w = QWidget::createWindowContainer(m_view.data(), this);
    w->setMinimumSize(m_view->initialSize());
    vl->addWidget(w);
    setLayout(vl);
    m_view->setWidth(width());
    m_view->setHeight(height());
}

KWinCompositingKCM::~KWinCompositingKCM()
{
}

K_PLUGIN_FACTORY(KWinCompositingConfigFactory,
                 registerPlugin<KWinCompositingKCM>();
                )

#include "main.moc"

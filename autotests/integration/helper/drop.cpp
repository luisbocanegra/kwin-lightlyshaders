/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <QDebug>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QRasterWindow>
class Window : public QRasterWindow
{
    Q_OBJECT
public:
    explicit Window();
    ~Window() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;
};

Window::Window()
    : QRasterWindow()
{
}

Window::~Window() = default;

void Window::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.fillRect(0, 0, width(), height(), Qt::blue);
}

bool Window::event(QEvent *event)
{
    if (event->type() == QEvent::DragEnter) {
        auto dragEvent = static_cast<QDragEnterEvent *>(event);
        qDebug() << "accepting on enter";
        dragEvent->accept();
        return true;
    }
    if (event->type() == QEvent::Drop) {
        auto dropEvent = static_cast<QDropEvent *>(event);
        qDebug() << "drop out";
        qApp->exit(dropEvent->mimeData()->text() != QLatin1String("test"));
    }
    return QWindow::event(event);
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    std::unique_ptr<Window> w(new Window);
    w->setGeometry(QRect(0, 0, 100, 200));
    w->show();

    return app.exec();
}

#include "drop.moc"

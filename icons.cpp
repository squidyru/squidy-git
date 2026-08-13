// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "icons.h"

#include <QFont>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScreen>

#include <cmath>

namespace {

constexpr qreal Grid = 24.0;
constexpr qreal Pi = 3.14159265358979323846;

qreal devicePixelRatio() {
    const QScreen *screen = QGuiApplication::primaryScreen();
    return screen != nullptr ? screen->devicePixelRatio() : 1.0;
}

/// Fixed toolbar colors that remain legible in both themes.
QColor defaultColor(const Icons::Glyph glyph) {
    switch (glyph) {
        case Icons::Glyph::Trash:
        case Icons::Glyph::Remove:
        case Icons::Glyph::Reset:
        case Icons::Glyph::Warning:
            return QColor(QStringLiteral("#D9455A"));
        case Icons::Glyph::Create:
        case Icons::Glyph::Add:
            return QColor(QStringLiteral("#238A5B"));
        case Icons::Glyph::Terminal:
        case Icons::Glyph::Explorer:
        case Icons::Glyph::Settings:
        case Icons::Glyph::Search:
        case Icons::Glyph::Discard:
            return QColor(QStringLiteral("#7F899E"));
        case Icons::Glyph::WindowMinimize:
        case Icons::Glyph::WindowMaximize:
        case Icons::Glyph::WindowRestore:
        case Icons::Glyph::WindowClose:
        case Icons::Glyph::TabClose:
            return QColor(QStringLiteral("#FFFFFF"));
        default:
            return QColor(QStringLiteral("#5A6FBE"));
    }
}

/// The squid-ghost silhouette. @p box bounds the dome and flanks; the tentacled
/// hem bulges half a tentacle below it.
QPainterPath ghostShape(const QRectF &box, const bool punchEyes) {
    constexpr int Tentacles = 4;
    const qreal width = box.width();
    const qreal radius = width / 2.0;
    const qreal domeCenterY = box.top() + radius;
    const qreal tentacleWidth = width / Tentacles;

    QPainterPath ghost;
    ghost.moveTo(box.left(), domeCenterY);
    ghost.arcTo(QRectF(box.left(), box.top(), width, width), 180.0, -180.0);
    ghost.lineTo(box.right(), box.bottom());
    for (int index = 0; index < Tentacles; ++index) {
        const qreal lobeLeft = box.right() - (index + 1) * tentacleWidth;
        ghost.arcTo(QRectF(lobeLeft, box.bottom() - tentacleWidth / 2.0,
                           tentacleWidth, tentacleWidth),
                    0.0, -180.0);
    }
    ghost.closeSubpath();

    if (punchEyes) {
        const qreal eyeRadius = width * 0.075;
        const qreal eyeOffset = width * 0.17;
        QPainterPath eyes;
        eyes.addEllipse(QPointF(box.center().x() - eyeOffset, domeCenterY),
                        eyeRadius, eyeRadius);
        eyes.addEllipse(QPointF(box.center().x() + eyeOffset, domeCenterY),
                        eyeRadius, eyeRadius);
        ghost = ghost.subtracted(eyes);
    }
    return ghost;
}

void addArrowHead(QPainterPath &path, const QPointF &tip, const QPointF &direction,
                  const qreal size) {
    const QPointF unit = direction / std::hypot(direction.x(), direction.y());
    const QPointF normal(-unit.y(), unit.x());
    const QPointF base = tip - unit * size;
    path.moveTo(base + normal * size * 0.6);
    path.lineTo(tip);
    path.lineTo(base - normal * size * 0.6);
}

void paintGlyph(QPainter &painter, const Icons::Glyph glyph, const QColor &color) {
    QPen pen(color, 1.15);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Enclose the four primary actions in a thin ring.
    const auto drawRing = [&painter, &color](const bool dashed) {
        QPen ring(color, 1.2);
        if (dashed) {
            ring.setDashPattern({3.0, 2.2});
        }
        painter.setPen(ring);
        painter.setBrush(Qt::white);
        painter.drawEllipse(QPointF(12, 12), 9.6, 9.6);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, 1.15, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    };

    const auto drawHollowNode = [&painter, &color](const QPointF &center) {
        painter.setPen(QPen(color, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::white);
        painter.drawEllipse(center, 2.05, 2.05);
        painter.setBrush(Qt::NoBrush);
    };

    QPainterPath path;
    switch (glyph) {
        case Icons::Glyph::Commit:
            drawRing(false);
            painter.drawLine(QPointF(12, 8.0), QPointF(12, 16.0));
            painter.drawLine(QPointF(8.0, 12), QPointF(16.0, 12));
            break;
        case Icons::Glyph::Checkout:
            path.moveTo(3.5, 12.5);
            path.lineTo(9.5, 18.5);
            path.lineTo(20.5, 6.0);
            painter.drawPath(path);
            break;
        case Icons::Glyph::Discard:
            // A deliberately quiet circular undo mark. The arrow head is part
            // of the arc end, while the pale red dot sits in the upper gap.
            painter.setPen(QPen(QColor(QStringLiteral("#B8B8B8")), 1.25,
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            path.arcMoveTo(QRectF(3.2, 3.2, 17.6, 17.6), 58.0);
            path.arcTo(QRectF(3.2, 3.2, 17.6, 17.6), 58.0, 267.0);
            painter.drawPath(path);
            path = QPainterPath();
            addArrowHead(path, QPointF(18.8, 17.1), QPointF(0.82, -0.42), 3.2);
            painter.drawPath(path);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(QStringLiteral("#E7A09A")));
            painter.drawEllipse(QPointF(18.8, 5.0), 2.05, 2.05);
            break;
        case Icons::Glyph::Stash:
        case Icons::Glyph::StashPop: {
            painter.drawRoundedRect(QRectF(2.6, 12.0, 18.8, 9.0), 2.2, 2.2);
            painter.setBrush(color);
            painter.setPen(Qt::NoPen);
            for (const qreal x : {7.0, 12.0, 17.0}) {
                painter.drawEllipse(QPointF(x, 16.5), 1.4, 1.4);
            }
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            const bool up = glyph == Icons::Glyph::StashPop;
            painter.drawLine(QPointF(12, up ? 8.6 : 2.8), QPointF(12, up ? 3.4 : 8.0));
            addArrowHead(path, QPointF(12, up ? 2.6 : 8.8), QPointF(0, up ? -1 : 1), 4.2);
            painter.drawPath(path);
            break;
        }
        case Icons::Glyph::Fetch:
            drawRing(true);
            painter.drawLine(QPointF(12, 8.0), QPointF(12, 14.9));
            addArrowHead(path, QPointF(12, 16.2), QPointF(0, 1), 3.5);
            painter.drawPath(path);
            break;
        case Icons::Glyph::Pull:
            drawRing(false);
            painter.drawLine(QPointF(12, 8.0), QPointF(12, 14.9));
            addArrowHead(path, QPointF(12, 16.2), QPointF(0, 1), 3.5);
            painter.drawPath(path);
            break;
        case Icons::Glyph::Push:
            drawRing(false);
            painter.drawLine(QPointF(12, 16.0), QPointF(12, 9.1));
            addArrowHead(path, QPointF(12, 7.8), QPointF(0, -1), 3.5);
            painter.drawPath(path);
            break;
        case Icons::Glyph::Branch: {
            painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            path.moveTo(7, 5.9);
            path.lineTo(7, 18.1);
            path.moveTo(7, 14.5);
            path.cubicTo(7, 11.0, 17, 13.0, 17, 10.0);
            painter.drawPath(path);
            drawHollowNode(QPointF(7, 4.0));
            drawHollowNode(QPointF(7, 20.0));
            drawHollowNode(QPointF(17, 8.0));
            break;
        }
        case Icons::Glyph::Merge: {
            painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            path.moveTo(7, 5.9);
            path.lineTo(7, 18.1);
            path.moveTo(17, 18.1);
            path.lineTo(17, 14.3);
            path.cubicTo(17, 11.5, 13.6, 9.8, 10.7, 9.8);
            painter.drawPath(path);
            path = QPainterPath();
            addArrowHead(path, QPointF(9.2, 9.8), QPointF(-1, 0), 3.0);
            painter.drawPath(path);
            drawHollowNode(QPointF(7, 4.0));
            drawHollowNode(QPointF(7, 20.0));
            drawHollowNode(QPointF(17, 20.0));
            break;
        }
        case Icons::Glyph::Rebase:
            painter.drawLine(QPointF(5, 4), QPointF(5, 20));
            path.moveTo(5, 15);
            path.cubicTo(12, 15, 12, 9, 19, 9);
            painter.drawPath(path);
            addArrowHead(path, QPointF(20, 9), QPointF(1, 0), 4.5);
            painter.drawPath(path);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(5, 20.5), 2.4, 2.4);
            break;
        case Icons::Glyph::Tag:
            path.moveTo(12.5, 3);
            path.lineTo(21, 11.5);
            path.lineTo(12, 20.5);
            path.lineTo(3.5, 12);
            path.lineTo(3.5, 3);
            path.closeSubpath();
            painter.drawPath(path);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(8, 7.5), 1.8, 1.8);
            break;
        case Icons::Glyph::Terminal:
            painter.drawRoundedRect(QRectF(3, 4.5, 18, 15), 2.5, 2.5);
            path.moveTo(7, 9.5);
            path.lineTo(11, 12.5);
            path.lineTo(7, 15.5);
            painter.drawPath(path);
            painter.drawLine(QPointF(13, 15.5), QPointF(17.5, 15.5));
            break;
        case Icons::Glyph::Explorer:
        case Icons::Glyph::OpenFolder:
        case Icons::Glyph::Clone:
        case Icons::Glyph::Create:
            path.moveTo(3, 19.5);
            path.lineTo(3, 5.5);
            path.lineTo(9.5, 5.5);
            path.lineTo(11.5, 8.5);
            path.lineTo(21, 8.5);
            path.lineTo(21, 19.5);
            path.closeSubpath();
            painter.drawPath(path);
            if (glyph == Icons::Glyph::Create) {
                painter.drawLine(QPointF(12, 11.5), QPointF(12, 17));
                painter.drawLine(QPointF(9.25, 14.25), QPointF(14.75, 14.25));
            } else if (glyph == Icons::Glyph::Clone) {
                painter.drawLine(QPointF(12, 11), QPointF(12, 16));
                QPainterPath head;
                addArrowHead(head, QPointF(12, 16.5), QPointF(0, 1), 4.0);
                painter.drawPath(head);
            }
            break;
        case Icons::Glyph::Settings:
            path = QPainterPath();
            for (int index = 0; index < 32; ++index) {
                const qreal angle = -Pi / 2.0 + index * Pi / 16.0;
                const int phase = index % 4;
                const qreal radius = phase == 0 || phase == 3 ? 9.7 : 7.4;
                const QPointF point(12 + std::cos(angle) * radius,
                                    12 + std::sin(angle) * radius);
                if (index == 0) {
                    path.moveTo(point);
                } else {
                    path.lineTo(point);
                }
            }
            path.closeSubpath();
            painter.drawPath(path);
            painter.drawEllipse(QPointF(12, 12), 3.4, 3.4);
            break;
        case Icons::Glyph::Remote:
            // Represent remotes as a light outline cloud.
            path.moveTo(6.0, 18.5);
            path.cubicTo(3.8, 18.5, 2.5, 17.0, 2.5, 15.0);
            path.cubicTo(2.5, 12.7, 4.2, 11.1, 6.3, 10.9);
            path.cubicTo(7.1, 7.6, 9.7, 5.4, 13.0, 5.4);
            path.cubicTo(16.8, 5.4, 19.2, 8.1, 19.5, 11.2);
            path.cubicTo(21.2, 11.8, 22.0, 13.2, 22.0, 14.9);
            path.cubicTo(22.0, 17.0, 20.5, 18.5, 18.2, 18.5);
            path.closeSubpath();
            painter.drawPath(path);
            break;
        case Icons::Glyph::Refresh:
            painter.drawArc(QRectF(4, 4, 16, 16), 300 * 16, 300 * 16);
            addArrowHead(path, QPointF(19.5, 9.5), QPointF(0.4, -1.0), 5.0);
            painter.drawPath(path);
            break;
        case Icons::Glyph::Search:
            painter.drawEllipse(QPointF(10.5, 10.5), 6.5, 6.5);
            painter.drawLine(QPointF(15.5, 15.5), QPointF(20.5, 20.5));
            break;
        case Icons::Glyph::FileStatus:
            // Desktop/working-copy symbol used by the sidebar.
            painter.drawRect(QRectF(2.8, 4.0, 18.4, 13.0));
            painter.drawLine(QPointF(9.0, 20.0), QPointF(15.0, 20.0));
            painter.drawLine(QPointF(12.0, 17.0), QPointF(12.0, 20.0));
            break;
        case Icons::Glyph::History:
            painter.drawEllipse(QPointF(12, 12), 8.6, 8.6);
            painter.drawLine(QPointF(12, 7), QPointF(12, 12.5));
            painter.drawLine(QPointF(12, 12.5), QPointF(16, 14.5));
            break;
        case Icons::Glyph::Add:
            painter.setPen(QPen(color, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(QPointF(12, 2.5), QPointF(12, 21.5));
            painter.drawLine(QPointF(2.5, 12), QPointF(21.5, 12));
            break;
        case Icons::Glyph::Remove:
            painter.drawLine(QPointF(4, 12), QPointF(20, 12));
            break;
        case Icons::Glyph::Trash:
            painter.drawLine(QPointF(3.5, 6.5), QPointF(20.5, 6.5));
            painter.drawLine(QPointF(9.5, 6.5), QPointF(9.5, 3.5));
            painter.drawLine(QPointF(9.5, 3.5), QPointF(14.5, 3.5));
            painter.drawLine(QPointF(14.5, 3.5), QPointF(14.5, 6.5));
            path.moveTo(5.5, 6.5);
            path.lineTo(6.8, 20.5);
            path.lineTo(17.2, 20.5);
            path.lineTo(18.5, 6.5);
            painter.drawPath(path);
            break;
        case Icons::Glyph::Reset:
            painter.drawArc(QRectF(4, 4, 16, 16), 140 * 16, 280 * 16);
            addArrowHead(path, QPointF(6.5, 5.5), QPointF(-0.8, -0.6), 5.0);
            painter.drawPath(path);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(12, 12), 2.2, 2.2);
            break;
        case Icons::Glyph::CherryPick:
            painter.drawEllipse(QPointF(8.5, 17.0), 3.6, 3.6);
            painter.drawEllipse(QPointF(16.5, 15.5), 3.0, 3.0);
            path.moveTo(8.5, 13.4);
            path.cubicTo(9.5, 8.0, 13.0, 5.0, 17.5, 4.0);
            path.moveTo(16.5, 12.5);
            path.cubicTo(16.0, 9.0, 16.5, 6.0, 17.5, 4.0);
            painter.drawPath(path);
            break;
        case Icons::Glyph::Submodule:
            painter.drawRoundedRect(QRectF(3.5, 3.5, 8, 8), 1.5, 1.5);
            painter.drawRoundedRect(QRectF(12.5, 12.5, 8, 8), 1.5, 1.5);
            painter.drawLine(QPointF(11.5, 7.5), QPointF(16.5, 7.5));
            painter.drawLine(QPointF(16.5, 7.5), QPointF(16.5, 12.5));
            break;
        case Icons::Glyph::Repository:
            painter.drawRoundedRect(QRectF(4, 3.5, 16, 17), 2.0, 2.0);
            painter.drawLine(QPointF(8, 3.5), QPointF(8, 20.5));
            painter.drawLine(QPointF(11.5, 8.5), QPointF(16.5, 8.5));
            painter.drawLine(QPointF(11.5, 12.0), QPointF(16.5, 12.0));
            break;
        case Icons::Glyph::Ghost:
            // Compact repository tree mark for the title bar. A large outer
            // ring and edge-to-edge branches remain legible at 24-32 px.
            painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QPointF(12, 12), 10.2, 10.2);
            path.moveTo(12, 18.5);
            path.lineTo(12, 9.0);
            path.moveTo(12, 13.5);
            path.cubicTo(10.5, 11.0, 7.0, 12.0, 7.0, 8.0);
            path.moveTo(12, 11.5);
            path.cubicTo(13.5, 9.0, 17.0, 10.0, 17.0, 6.5);
            painter.drawPath(path);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(12, 19.0), 1.45, 1.45);
            painter.drawEllipse(QPointF(12, 8.5), 1.45, 1.45);
            painter.drawEllipse(QPointF(7, 7.5), 1.45, 1.45);
            painter.drawEllipse(QPointF(17, 6.0), 1.45, 1.45);
            break;
        case Icons::Glyph::WindowMinimize:
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::SquareCap));
            painter.drawLine(QPointF(5, 12), QPointF(19, 12));
            break;
        case Icons::Glyph::WindowMaximize:
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(QPen(Qt::white, 1.5, Qt::SolidLine,
                                Qt::SquareCap, Qt::MiterJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(5.0, 5.0, 14.0, 14.0));
            break;
        case Icons::Glyph::WindowRestore:
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(QPen(Qt::white, 1.5, Qt::SolidLine,
                                Qt::SquareCap, Qt::MiterJoin));
            painter.setBrush(Qt::NoBrush);
            path.moveTo(9.0, 7.5);
            path.lineTo(9.0, 5.0);
            path.lineTo(19.0, 5.0);
            path.lineTo(19.0, 15.0);
            path.lineTo(16.5, 15.0);
            painter.drawPath(path);
            painter.drawRect(QRectF(5.0, 9.0, 10.0, 10.0));
            break;
        case Icons::Glyph::WindowClose:
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::SquareCap));
            painter.drawLine(QPointF(5, 5), QPointF(19, 19));
            painter.drawLine(QPointF(19, 5), QPointF(5, 19));
            break;
        case Icons::Glyph::TabClose:
            painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(QPointF(7.0, 7.0), QPointF(17.0, 17.0));
            painter.drawLine(QPointF(17.0, 7.0), QPointF(7.0, 17.0));
            break;
        case Icons::Glyph::Warning:
            path.moveTo(12, 3.5);
            path.lineTo(21.5, 20.0);
            path.lineTo(2.5, 20.0);
            path.closeSubpath();
            painter.drawPath(path);
            painter.drawLine(QPointF(12, 9.5), QPointF(12, 14.5));
            painter.setBrush(color);
            painter.drawEllipse(QPointF(12, 17.4), 1.1, 1.1);
            break;
    }
}

}

QPixmap Icons::pixmap(const Glyph glyph, const int size, const QColor &color) {
    const qreal ratio = devicePixelRatio();
    QPixmap canvas(QSize(size, size) * ratio);
    canvas.setDevicePixelRatio(ratio);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(size / Grid, size / Grid);
    paintGlyph(painter, glyph, color.isValid() ? color : defaultColor(glyph));
    painter.end();
    return canvas;
}

QIcon Icons::icon(const Glyph glyph, const QColor &color) {
    QIcon result;
    const bool windowControl = glyph == Glyph::WindowMinimize
                               || glyph == Glyph::WindowMaximize
                               || glyph == Glyph::WindowRestore
                               || glyph == Glyph::WindowClose;
    for (const int size : {16, 20, 24, 32, 48}) {
        const QPixmap rendered = pixmap(glyph, size, color);
        result.addPixmap(rendered, QIcon::Normal, QIcon::Off);
        if (windowControl) {
            // Prevent QStyle from synthesizing grey inactive/active variants:
            // Keep every window-control glyph pure white.
            result.addPixmap(rendered, QIcon::Active, QIcon::Off);
            result.addPixmap(rendered, QIcon::Selected, QIcon::Off);
            result.addPixmap(rendered, QIcon::Disabled, QIcon::Off);
        }
        if (glyph == Glyph::Discard) {
            // Preserve the pale red status dot when the action is
            // disabled; Qt's synthesized disabled icon would turn it grey.
            result.addPixmap(rendered, QIcon::Disabled, QIcon::Off);
        }
    }
    return result;
}

QPixmap Icons::applicationPixmap(const int size) {
    const qreal s = size;
    QPixmap canvas(size, size);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient background(0.0, 0.0, s, s);
    background.setColorAt(0.0, QColor(QStringLiteral("#FF6A5B")));
    background.setColorAt(0.55, QColor(QStringLiteral("#EE2A4B")));
    background.setColorAt(1.0, QColor(QStringLiteral("#A3061F")));
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawEllipse(QRectF(0.0, 0.0, s, s));

    const qreal width = 0.39 * s;
    const QRectF body(0.5 * s - width / 2.0, 0.26 * s, width, 0.43 * s);
    const qreal domeCenterY = body.top() + width / 2.0;

    painter.setBrush(Qt::white);
    painter.drawPath(ghostShape(body, false));

    painter.setBrush(QColor(QStringLiteral("#3B6BE8")));
    const qreal eyeRadius = 0.029 * s;
    const qreal eyeOffset = 0.067 * s;
    painter.drawEllipse(QPointF(0.5 * s - eyeOffset, domeCenterY), eyeRadius, eyeRadius);
    painter.drawEllipse(QPointF(0.5 * s + eyeOffset, domeCenterY), eyeRadius, eyeRadius);
    painter.end();

    return canvas;
}

QIcon Icons::applicationIcon() {
    QIcon result;
    for (const int size : {16, 24, 32, 48, 64, 128, 256, 512}) {
        result.addPixmap(applicationPixmap(size));
    }
    return result;
}

QIcon Icons::badgedIcon(const Glyph glyph, const QColor &color, const int count,
                        const QColor &badgeColor) {
    if (count <= 0) {
        return icon(glyph, color);
    }

    QIcon result;
    for (const int size : {24, 32, 48}) {
        QPixmap canvas = pixmap(glyph, size, color);
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QString text = count > 99 ? QStringLiteral("99+") : QString::number(count);
        QFont font = painter.font();
        font.setPixelSize(qMax(7, size * 7 / 16));
        font.setBold(true);
        painter.setFont(font);

        const int textWidth = painter.fontMetrics().horizontalAdvance(text);
        const int badgeHeight = qMax(10, size * 9 / 16);
        const QRectF badge(canvas.width() / canvas.devicePixelRatio() - textWidth - 7,
                           0.0, textWidth + 6.0, badgeHeight);
        painter.setPen(Qt::NoPen);
        painter.setBrush(badgeColor);
        painter.drawRoundedRect(badge, badgeHeight / 2.0, badgeHeight / 2.0);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, text);
        painter.end();
        result.addPixmap(canvas);
    }
    return result;
}

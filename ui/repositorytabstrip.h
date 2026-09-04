// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "theme.h"
#include "shellmetrics.h"

#include <QEvent>
#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QTabBar>
#include <QToolButton>
#include <QWidget>

class RepositoryTabBar final : public QTabBar {
    Q_OBJECT

public:
    using QTabBar::QTabBar;

    void setCalculatedWidths(const QList<int> &widths) {
        if (widths_ == widths) {
            return;
        }
        widths_ = widths;
        // QTabBar caches its layout separately from QWidget's size hint.
        QEvent change(QEvent::StyleChange);
        QCoreApplication::sendEvent(this, &change);
        updateGeometry();
    }

    QSize tabSizeHint(const int index) const override {
        QSize size = QTabBar::tabSizeHint(index);
        size.setHeight(ShellMetrics::RepositoryTabHeight);
        if (index >= 0 && index < widths_.size()) {
            size.setWidth(widths_.at(index));
        }
        return size;
    }

    QSize minimumTabSizeHint(const int index) const override {
        return tabSizeHint(index);
    }

    bool activeTabTouchesLeadingEdge() const {
        if (currentIndex() <= 0 || property("overviewActive").toBool()) {
            return false;
        }
        const QRect active = tabRect(currentIndex());
        return active.left() <= 0
               && active.right() - ShellMetrics::RepositoryTabGap >= 16;
    }

Q_SIGNALS:
    void leadingEdgeJoinedChanged();

protected:
    void paintEvent(QPaintEvent *event) override {
        // Scrolling changes tab rectangles without changing the active index.
        // Check the visible edge before painting, including native arrow scrolls.
        const bool joined = activeTabTouchesLeadingEdge();
        if (joined != leadingEdgeJoined_) {
            leadingEdgeJoined_ = joined;
            Q_EMIT leadingEdgeJoinedChanged();
        }
        QTabBar::paintEvent(event);
    }

    void tabLayoutChange() override {
        QTabBar::tabLayoutChange();
        if (parentWidget() != nullptr) {
            parentWidget()->update();
        }
    }

private:
    QList<int> widths_;
    bool leadingEdgeJoined_ = false;
};

// The join extends outside the tab's rectangle, so it belongs to the strip.
// QTabBar still handles its own labels, hit testing and drag animations.
class RepositoryTabStrip final : public QWidget {
public:
    using QWidget::QWidget;

    void setTabBar(QTabBar *tabs) {
        tabs_ = tabs;
        tabs->installEventFilter(this);
        connect(tabs, &QTabBar::currentChanged, this, [this] { update(); });
        connect(tabs, &QTabBar::tabMoved, this, [this] { update(); });
        connect(Theme::instance(), &Theme::changed, this, [this] { update(); });
    }

    void setLeadingTabJoined(bool joined) {
        if (leadingTabJoined_ != joined) {
            leadingTabJoined_ = joined;
            update();
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == tabs_ && (event->type() == QEvent::Resize
                                || event->type() == QEvent::Move
                                || event->type() == QEvent::LayoutRequest
                                || event->type() == QEvent::StyleChange)) {
            update();
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *event) override {
        QWidget::paintEvent(event);
        if (tabs_ == nullptr || tabs_->currentIndex() <= 0 || !tabs_->isVisible()
            || tabs_->property("overviewActive").toBool()) {
            return;
        }
        const QRect tab = tabs_->tabRect(tabs_->currentIndex());
        const QPoint origin = tabs_->mapTo(this, tab.topLeft());
        const qreal left = origin.x();
        const qreal right = left + tab.width() - ShellMetrics::RepositoryTabGap;
        const qreal bottom = origin.y() + tab.height();
        constexpr qreal radius = 10;
        QPainterPath joins;
        if (!leadingTabJoined_) {
            joins.moveTo(left - radius, bottom);
            joins.quadTo(left, bottom, left, bottom - radius);
            joins.lineTo(left, bottom);
            joins.closeSubpath();
        }
        joins.moveTo(right, bottom - radius);
        joins.quadTo(right, bottom, right + radius, bottom);
        joins.lineTo(right, bottom);
        joins.closeSubpath();

        QPainter painter(this);
        QRegion clip(rect());
        for (QToolButton *button : tabs_->findChildren<QToolButton *>(
                 QString(), Qt::FindDirectChildrenOnly)) {
            if (button->isVisible()) {
                clip &= QRegion(QRect(tabs_->mapTo(this, QPoint()), tabs_->size()));
                clip -= QRegion(QRect(button->mapTo(this, QPoint()), button->size()));
            }
        }
        painter.setClipRegion(clip);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Theme::instance()->palette().toolbar);
        painter.drawPath(joins);
    }

private:
    QPointer<QTabBar> tabs_;
    bool leadingTabJoined_ = false;
};

// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QAbstractItemView>
#include <QComboBox>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>

/// Opens combo boxes as a plain framed list instead of a menu-like popup. The
/// popup form draws scroll arrows above and below the entries and, as Qt
/// documents, ignores maxVisibleItems; the list form has a normal scroll bar
/// and honours the row limit.
class FlatComboBoxStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    [[nodiscard]] int styleHint(const StyleHint hint, const QStyleOption *option,
                                const QWidget *widget,
                                QStyleHintReturn *returnData) const override {
        if (hint == SH_ComboBox_Popup) {
            return 0;
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

/// A combo box that draws its own drop-down arrow. The style sheet flattens the
/// native one away, so every combo box of the workspace pages uses this class to
/// keep the marker visible.
class FlatComboBox final : public QComboBox {
public:
    explicit FlatComboBox(QWidget *parent = nullptr)
        : QComboBox(parent) {
        auto *listStyle = new FlatComboBoxStyle;
        listStyle->setParent(this);
        setStyle(listStyle);
        // Long lists of branches and tags scroll rather than run off the screen.
        setMaxVisibleItems(15);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QComboBox::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(isEnabled() ? QPalette::Active
                                                     : QPalette::Disabled,
                                         QPalette::ButtonText));
        const qreal centerX = width() - 9.0;
        const qreal centerY = height() / 2.0 + 1.0;
        QPainterPath arrow;
        arrow.moveTo(centerX - 3.5, centerY - 2.0);
        arrow.lineTo(centerX + 3.5, centerY - 2.0);
        arrow.lineTo(centerX, centerY + 2.0);
        arrow.closeSubpath();
        painter.drawPath(arrow);
    }
};

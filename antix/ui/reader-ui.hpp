#pragma once
#include "reader-device.hpp"
#include <QtWidgets>
#include <functional>
namespace ReRead {
class Screen : public QWidget {
  Device *device;
  QTimer clock;

protected:
  void showEvent(QShowEvent *) override;
  void hideEvent(QHideEvent *) override;
  void paintEvent(QPaintEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  void keyReleaseEvent(QKeyEvent *) override;
  virtual void idleChanged(bool) {}
  virtual void draw(QPainter &, const QRect &) = 0;
  virtual void button(reader_button, int) = 0;

public:
  struct Control { reader_button button; QString action; };
  QList<Control> controls;
  // Each app chooses its own chrome. Safety notices remain available without it.
  bool showHeader = true, showControls = true, controlsOverlay = false;
  int orientation = 0;
  QString title, subtitle, notice;
  QRect safeArea() const;
  void buttonIcon(QPainter &, const QRect &, reader_button);
  Screen(const QString &name);
  ~Screen() override;
  QRect content() const;
  void dispatch(reader_button id, int value);
  void toast(const QString &text);
  struct ListInfoLayout {
    QRect entries, info;
    int first, visible, rowHeight;
    QRect row(int index, int gap = 2) const;
  };
  ListInfoLayout listInfo(QPainter &, const QRect &, int items, int selected,
                          int rowHeight = 60, qreal listShare = 0.62);
  QRect listInfoRow(QPainter &, const ListInfoLayout &, int index, int selected,
                    int gap = 2);
  void label(QPainter &, const QRect &, const QString &, int pixels = 22,
             bool bold = false, QColor color = QColor(),
             Qt::TextElideMode elide = Qt::ElideRight);
  void list(QPainter &, const QRect &, const QStringList &labels,
            const QStringList &details, int selected);
  void text(QPainter &, const QRect &, const QString &, int pixels = 26,
            QColor color = QColor());
};
} // namespace ReRead

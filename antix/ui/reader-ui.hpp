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
  void foreground(bool enabled);
  void toast(const QString &text);
  void list(QPainter &, const QRect &, const QStringList &labels,
            const QStringList &details, int selected);
  void text(QPainter &, const QRect &, const QString &, int pixels = 26,
            QColor color = QColor());
};
} // namespace ReRead

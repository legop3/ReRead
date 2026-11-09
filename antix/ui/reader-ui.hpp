#pragma once
#include "reader-keys.h"
#include <QtNetwork>
#include <QtWidgets>
#include <functional>
namespace ReRead {
QJsonObject readJson(const QString &path);
void writeJson(const QString &path, const QJsonObject &value);
void command(const QString &program, const QStringList &args,
             std::function<void(QByteArray)> done = {});
void cameraRequest(
    const QString &endpoint, const QJsonObject &body,
    std::function<void(int, QByteArray, QMap<QByteArray, QByteArray>)> done,
    QObject *owner);
class Screen : public QWidget {
  struct Input {
    int fd;
    QSocketNotifier *notifier;
    bool builtin;
  };
  QList<Input> inputs;
  QTimer discovery, clock, idle;
  bool dimmed = false, locked = false, powerDown = false, suspending = false;
  qint64 lastVolume = 0, powerBlockUntil = 0;
  void discover();
  void closeInputs();
  void activity();

protected:
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
            QColor color = QColor("#eef3f8"));
};
} // namespace ReRead

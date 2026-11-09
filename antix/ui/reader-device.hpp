#pragma once
#include "reader-keys.h"
#include <QtCore>
#include <functional>
#include <memory>
namespace ReRead {
QJsonObject readJson(const QString &path);
void writeJson(const QString &path, const QJsonObject &value);
void command(const QString &program, const QStringList &args,
             std::function<void(QByteArray)> done = {}, QObject *owner = nullptr);
// Optional QtCore-only adapter to existing Linux input, backlight, ALSA and
// power helpers. It owns no window, renderer, layout, camera or app registry.
class Device : public QObject {
  struct State;
  std::unique_ptr<State> state;
public:
  explicit Device(QObject *parent = nullptr);
  ~Device() override;
  void setActive(bool enabled);
  bool isActive() const;
  bool isLocked() const;
  void activity();
  void dispatch(reader_button id, int value);
  void onButton(std::function<void(reader_button, int)> handler);
  void onIdle(std::function<void(bool)> handler);
  void onHold(std::function<void(bool)> handler);
  void onNotice(std::function<void(const QString &)> handler);
  void onResume(std::function<void()> handler);
};
} // namespace ReRead

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
// Optional QtCore/QtDBus semantic dispatch and session-state adapter. System
// services own power policy, volume and dimming. This object owns no renderer.
class Device : public QObject {
  Q_OBJECT
  struct State;
  std::unique_ptr<State> state;
private slots:
  void sleepChanged(bool sleeping);
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

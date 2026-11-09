#include "reader-device.hpp"
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
namespace ReRead {
QJsonObject readJson(const QString &path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly)
             ? QJsonDocument::fromJson(f.readAll()).object()
             : QJsonObject();
}
void writeJson(const QString &path, const QJsonObject &value) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(value).toJson(QJsonDocument::Compact));
    f.commit();
  }
}
void command(const QString &program, const QStringList &args,
             std::function<void(QByteArray)> done, QObject *owner) {
  auto p = new QProcess(owner ? owner : QCoreApplication::instance());
  QObject::connect(
      p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), owner ? owner : p,
      [p, done](int code, QProcess::ExitStatus) {
        auto out = p->readAllStandardOutput();
        if (code)
          qWarning() << p->program() << p->readAllStandardError();
        if (done)
          done(out);
        p->deleteLater();
      });
  QObject::connect(p, &QProcess::errorOccurred, owner ? owner : p,
                   [p, done](QProcess::ProcessError e) {
                     if (e == QProcess::FailedToStart) {
                       if (done)
                         done({});
                       p->deleteLater();
                     }
                   });
  p->start(program, args);
}
// Transient dimming never changes the saved level. Restore synchronously before
// dispatching the waking key, so a simultaneous brightness adjustment starts at
// the user's level rather than racing an asynchronous restore process.
static bool transientBrightness(int percent) {
  auto providers =
      QDir("/sys/class/backlight").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  providers.removeAll("psb-bl");
  providers.prepend("psb-bl");
  providers.removeAll("reader-panel");
  providers.prepend("reader-panel");
  for (const auto &provider : providers) {
    QString path = "/sys/class/backlight/" + provider;
    QFile maximum(path + "/max_brightness");
    if (!maximum.open(QIODevice::ReadOnly))
      continue;
    int max = maximum.readAll().trimmed().toInt();
    if (max <= 0)
      continue;
    QFile level(path + "/brightness");
    if (level.open(QIODevice::WriteOnly))
      return level.write(QByteArray::number(
                             qMax(1, max * qBound(10, percent, 100) / 100)) +
                         "\n") > 0;
  }
  return false;
}
struct Device::State {
  Device &owner;
  struct Input { int fd; QSocketNotifier *notifier; };
  QList<Input> inputs;
  QTimer discovery, idle;
  bool active = false, dimmed = false, locked = false;
  bool powerDown = false, suspending = false;
  qint64 lastVolume = 0, powerBlockUntil = 0;
  std::function<void(reader_button, int)> buttonHandler;
  std::function<void(bool)> idleHandler, holdHandler;
  std::function<void(const QString &)> noticeHandler;
  std::function<void()> resumeHandler;
  explicit State(Device &device) : owner(device) {
    QObject::connect(&discovery, &QTimer::timeout, &owner, [this] { discover(); });
    idle.setSingleShot(true);
    QObject::connect(&idle, &QTimer::timeout, &owner, [this] {
      if (!active || readJson("/etc/reader/display.json")
                         .value("idle_dim_seconds").toInt(120) == 0) return;
      dimmed = transientBrightness(10);
      if (dimmed && idleHandler) idleHandler(true);
    });
  }
  ~State() {
    closeInputs();
    if (active && dimmed) transientBrightness(readJson("/etc/reader/display.json")
                                       .value("brightness_percent").toInt(100));
  }
  void setActive(bool);
  void activity();
  void closeInputs();
  void discover();
  void dispatch(reader_button, int);
};
Device::Device(QObject *parent) : QObject(parent), state(new State(*this)) {}
Device::~Device() = default;
void Device::setActive(bool value) { state->setActive(value); }
bool Device::isActive() const { return state->active; }
bool Device::isLocked() const { return state->locked; }
void Device::activity() { if (state->active) state->activity(); }
void Device::dispatch(reader_button id, int value) { state->dispatch(id, value); }
void Device::onButton(std::function<void(reader_button,int)> h) { state->buttonHandler = std::move(h); }
void Device::onIdle(std::function<void(bool)> h) { state->idleHandler = std::move(h); }
void Device::onHold(std::function<void(bool)> h) { state->holdHandler = std::move(h); }
void Device::onNotice(std::function<void(const QString &)> h) { state->noticeHandler = std::move(h); }
void Device::onResume(std::function<void()> h) { state->resumeHandler = std::move(h); }
void Device::State::activity() {
  if (dimmed) {
    dimmed = false;
    transientBrightness(readJson("/etc/reader/display.json")
                            .value("brightness_percent")
                            .toInt(100));
    if (idleHandler) idleHandler(false);
  }
  int seconds =
      readJson("/etc/reader/display.json").value("idle_dim_seconds").toInt(120);
  if (seconds > 0)
    idle.start(seconds * 1000);
  else
    idle.stop();
}
void Device::State::closeInputs() {
  for (auto input : inputs) {
    delete input.notifier;
    ioctl(input.fd, EVIOCGRAB, 0);
    ::close(input.fd);
  }
  inputs.clear();
}
void Device::State::setActive(bool enabled) {
  if (active == enabled) return;
  active = enabled;
  powerDown = false;
  if (enabled) {
    discover();
    discovery.start(5000);
    activity();
  } else {
    // Restore the shared backlight without waking this hidden app's resources.
    // Keep dimmed set until it becomes active again, then emit onIdle(false).
    if (dimmed) transientBrightness(readJson("/etc/reader/display.json")
                                       .value("brightness_percent").toInt(100));
    idle.stop();
    discovery.stop();
    closeInputs();
  }
}
void Device::State::discover() {
  for (auto name :
       QDir("/dev/input").entryList({"event*"}, QDir::System | QDir::Files)) {
    QString path = "/dev/input/" + name;
    bool found = false;
    for (auto input : inputs) {
      char link[256] = {};
      auto n = ::readlink(
          ("/proc/self/fd/" + QString::number(input.fd)).toLocal8Bit(), link,
          255);
      if (n > 0 && path == QString::fromLocal8Bit(link, n))
        found = true;
    }
    if (found)
      continue;
    int fd = ::open(path.toLocal8Bit(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
      continue;
    unsigned long types = 0;
    ioctl(fd, EVIOCGBIT(0, sizeof(types)), &types);
    if (!(types & (1UL << EV_KEY))) {
      ::close(fd);
      continue;
    }
    char label[128] = {};
    ioctl(fd, EVIOCGNAME(sizeof(label)), label);
    bool builtin = QString::fromLocal8Bit(label).contains("AT Translated");
    if (builtin)
      for (const auto &key : reader_keys) {
        unsigned map[2] = {key.scan, key.key};
        ioctl(fd, EVIOCSKEYCODE, map);
      }
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
      ::close(fd);
      continue;
    }
    auto notifier = new QSocketNotifier(fd, QSocketNotifier::Read, &owner);
    inputs.append({fd, notifier});
    QObject::connect(notifier, &QSocketNotifier::activated, &owner, [this, fd](int) {
      QPointer<Device> alive(&owner);
      input_event events[32];
      ssize_t size;
      while ((size = ::read(fd, events, sizeof(events))) > 0)
        for (int n = 0; n < size / int(sizeof(input_event)); n++) {
          const auto &e = events[n];
          if (e.type != EV_KEY)
            continue;
          reader_button id = READER_NONE;
          for (const auto &key : reader_keys)
            if (e.code == key.key) {
              id = key.id;
              break;
            }
          if (e.code == KEY_ESC)
            id = READER_BACK;
          dispatch(id, e.value);
          if (!alive || !active) return;
        }
      if (size == 0 || (size < 0 && (errno == ENODEV || errno == EIO))) {
        for (int i = 0; i < inputs.size(); i++)
          if (inputs[i].fd == fd) {
            inputs[i].notifier->deleteLater();
            ::close(fd);
            inputs.removeAt(i);
            break;
          }
      }
    });
  }
}
void Device::State::dispatch(reader_button id, int value) {
  if (id == READER_NONE || !active)
    return;
  if (id == READER_HOLD) {
    locked = value == 0;
    activity();
    if (holdHandler) holdHandler(locked);
    return;
  }
  if (locked && id != READER_POWER)
    return;
  activity();
  if (id == READER_POWER) {
    if (suspending || QDateTime::currentMSecsSinceEpoch() < powerBlockUntil)
      return;
    if (value == 1)
      powerDown = true;
    if (value == 0 && powerDown) {
      powerDown = false;
      suspending = true;
      command("/usr/local/sbin/reader-power", {"suspend"}, [this](QByteArray) {
        suspending = false;
        powerBlockUntil = QDateTime::currentMSecsSinceEpoch() + 1000;
        closeInputs();
        if (active) discover();
        command("/usr/local/sbin/reader-brightness", {"init"}, {}, &owner);
        if (resumeHandler) resumeHandler();
      }, &owner);
    }
    return;
  }
  if ((id == READER_VOLUME_UP || id == READER_VOLUME_DOWN) && value) {
    auto now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastVolume >= 180) {
      lastVolume = now;
      command("/usr/local/sbin/reader-audio",
              {id == READER_VOLUME_UP ? "up" : "down"},
              [this](QByteArray data) {
                if (noticeHandler) noticeHandler("Volume " +
                      QString::number(QJsonDocument::fromJson(data)
                                          .object()
                                          .value("volume_percent")
                                          .toInt()) +
                      "%");
              }, &owner);
    }
    return;
  }
  if (buttonHandler) buttonHandler(id, value);
}
} // namespace ReRead

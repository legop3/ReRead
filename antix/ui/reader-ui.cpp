#include "reader-ui.hpp"
#include <fcntl.h>
#include <linux/input.h>
#include <memory>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
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
             std::function<void(QByteArray)> done) {
  auto p = new QProcess(qApp);
  QObject::connect(
      p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), p,
      [p, done](int code, QProcess::ExitStatus) {
        auto out = p->readAllStandardOutput();
        if (code)
          qWarning() << p->program() << p->readAllStandardError();
        if (done)
          done(out);
        p->deleteLater();
      });
  QObject::connect(p, &QProcess::errorOccurred, p,
                   [p, done](QProcess::ProcessError e) {
                     if (e == QProcess::FailedToStart) {
                       if (done)
                         done({});
                       p->deleteLater();
                     }
                   });
  p->start(program, args);
}
void cameraRequest(
    const QString &endpoint, const QJsonObject &body,
    std::function<void(int, QByteArray, QMap<QByteArray, QByteArray>)> done,
    QObject *owner) {
  auto socket = new QLocalSocket(owner);
  auto data = std::make_shared<QByteArray>();
  auto timer = new QTimer(socket);
  timer->setSingleShot(true);
  auto finished = std::make_shared<bool>(false);
  auto finish = [socket, data, timer, done,
                 finished](int code, QByteArray bytes,
                           QMap<QByteArray, QByteArray> headers) {
    if (*finished)
      return;
    *finished = true;
    timer->stop();
    done(code, bytes, headers);
    socket->deleteLater();
  };
  QObject::connect(timer, &QTimer::timeout, socket,
                   [finish] { finish(504, "Camera request timed out", {}); });
  QObject::connect(
      socket, &QLocalSocket::connected, socket, [socket, endpoint, body] {
        bool get = endpoint == "/status" || endpoint.startsWith("/live?") ||
                   endpoint.startsWith("/rgb?");
        QByteArray bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        socket->write(
            (get ? "GET " : "POST ") + endpoint.toUtf8() +
            " HTTP/1.0\r\nContent-Type: application/json\r\nContent-Length: " +
            QByteArray::number(get ? 0 : bytes.size()) + "\r\n\r\n" +
            (get ? QByteArray() : bytes));
      });
  QObject::connect(socket, &QLocalSocket::readyRead, socket,
                   [socket, data, finish] {
                     *data += socket->readAll();
                     int end = data->indexOf("\r\n\r\n");
                     if (end < 0)
                       return;
                     auto lines = data->left(end).split('\n');
                     QMap<QByteArray, QByteArray> headers;
                     for (auto line : lines) {
                       int colon = line.indexOf(':');
                       if (colon > 0)
                         headers[line.left(colon).toLower().trimmed()] =
                             line.mid(colon + 1).trimmed();
                     }
                     int length = headers.value("content-length").toInt();
                     if (data->size() < end + 4 + length)
                       return;
                     finish(lines.first().split(' ').value(1).toInt(),
                            data->mid(end + 4, length), headers);
                   });
  QObject::connect(socket,
                   QOverload<QLocalSocket::LocalSocketError>::of(
                       &QLocalSocket::errorOccurred),
                   socket, [finish](QLocalSocket::LocalSocketError e) {
                     if (e != QLocalSocket::PeerClosedError)
                       finish(503, "Camera service unavailable", {});
                   });
  timer->start(30000);
  socket->connectToServer("/run/reader-camera.sock");
}
// Transient dimming never changes the saved level. Restore synchronously before
// dispatching the waking key, so a simultaneous brightness adjustment starts at
// the user's level rather than racing an asynchronous restore process.
static bool transientBrightness(int percent) {
  auto providers =
      QDir("/sys/class/backlight").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  providers.removeAll("psb-bl");
  providers.prepend("psb-bl");
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
Screen::Screen(const QString &name) : title(name) {
  prctl(PR_SET_PDEATHSIG, SIGTERM);
  setWindowFlags(Qt::FramelessWindowHint);
  setCursor(Qt::BlankCursor);
  resize(960, 544);
  connect(&discovery, &QTimer::timeout, this, [this] { discover(); });
  discovery.start(5000);
  discover();
  connect(&clock, &QTimer::timeout, this, [this] {
    if (isVisible())
      update();
  });
  clock.start(30000);
  idle.setSingleShot(true);
  connect(&idle, &QTimer::timeout, this, [this] {
    if (!isVisible())
      return;
    auto settings = readJson("/etc/reader/display.json");
    if (settings.value("idle_dim_seconds").toInt(120) == 0)
      return;
    dimmed = transientBrightness(10);
    if (dimmed)
      idleChanged(true);
  });
  auto batteryWatcher = new QFileSystemWatcher(this);
  const QString batteryPath = "/run/reader-battery.json";
  batteryWatcher->addPath(batteryPath);
  auto displayedBattery = std::make_shared<QByteArray>();
  connect(batteryWatcher, &QFileSystemWatcher::fileChanged, this,
          [this, batteryWatcher, batteryPath, displayedBattery] {
            batteryWatcher->addPath(batteryPath);
            auto state = readJson(batteryPath);
            QJsonObject visible;
            for (const auto &key : {"capacity_percent", "external_power",
                                    "level", "shutdown_in_seconds"})
              visible[key] = state[key];
            auto signature =
                QJsonDocument(visible).toJson(QJsonDocument::Compact);
            if (signature != *displayedBattery) {
              *displayedBattery = signature;
              if (isVisible())
                update();
            }
          });
  activity();
}
Screen::~Screen() {
  closeInputs();
  if (dimmed)
    transientBrightness(readJson("/etc/reader/display.json")
                            .value("brightness_percent")
                            .toInt(100));
}
QRect Screen::content() const {
  int margin = qBound(
      20, readJson("/etc/reader/display.json").value("safe_margin").toInt(28),
      60);
  return rect().adjusted(margin, 96, -margin, -78);
}
void Screen::text(QPainter &p, const QRect &r, const QString &t, int size,
                  QColor color) {
  auto f = p.font();
  f.setPixelSize(size);
  p.setFont(f);
  p.setPen(color);
  p.drawText(r, Qt::AlignVCenter | Qt::TextWordWrap, t);
}
void Screen::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.fillRect(rect(), QColor("#101820"));
  p.setRenderHint(QPainter::Antialiasing);
  QRect c = content();
  text(p, QRect(c.left(), 22, width() / 2, 46), title, 34);
  auto battery = readJson("/run/reader-battery.json");
  QString status = (battery.value("external_power").toBool() ? "AC · " : "") +
                   QString::number(battery.value("capacity_percent").toInt()) +
                   "%   " + QTime::currentTime().toString("HH:mm");
  auto f = p.font();
  f.setPixelSize(23);
  p.setFont(f);
  p.setPen(QColor("#aabfc9"));
  p.drawText(QRect(width() / 2, 22, width() / 2 - c.left(), 46),
             Qt::AlignRight | Qt::AlignVCenter, status);
  if (!subtitle.isEmpty())
    text(p, QRect(c.left(), 66, c.width(), 26), subtitle, 20,
         QColor("#aabfc9"));
  draw(p, c);
  QString batteryNotice;
  const QString level = battery.value("level").toString();
  if (level == "critical" || level == "warning") {
    batteryNotice = level == "critical" ? "Critical battery — connect power"
                                        : "Low battery — connect power";
    if (!battery.value("shutdown_in_seconds").isNull())
      batteryNotice +=
          " · shutdown in " +
          QString::number(battery.value("shutdown_in_seconds").toInt()) + "s";
  }

  p.setPen(QColor("#31444f"));
  p.drawLine(c.left(), height() - 64, c.right(), height() - 64);
  text(p, QRect(c.left(), height() - 60, c.width(), 38),
       locked                     ? "Hold locked — slide switch to unlock"
       : !batteryNotice.isEmpty() ? batteryNotice
       : notice.isEmpty()         ? hint
                                  : notice,
       21, QColor("#aabfc9"));
}
void Screen::list(QPainter &p, const QRect &r, const QStringList &labels,
                  const QStringList &details, int selected) {
  int visible = qMax(1, r.height() / 84);
  int first = qMax(0, selected - visible + 1);
  for (int i = first; i < labels.size() && i < first + visible; i++) {
    QRect row(r.x(), r.y() + (i - first) * 84, r.width(), 76);
    p.setPen(QPen(i == selected ? QColor("#38c9bb") : QColor("#31444f"),
                  i == selected ? 2 : 1));
    p.setBrush(QColor(i == selected ? "#203d45" : "#192a34"));
    p.drawRoundedRect(row, 9, 9);
    text(p, row.adjusted(18, details.value(i).isEmpty() ? 0 : -16, -32, 0),
         labels[i], 27);
    if (!details.value(i).isEmpty())
      text(p, QRect(row.x() + 18, row.y() + 43, row.width() - 42, 25),
           details[i], 19, QColor("#aabfc9"));
  }
  if (labels.isEmpty())
    text(p, r, "Nothing here yet.");
  if (labels.size() > visible)
    text(p, QRect(r.right() - 75, r.bottom() - 25, 75, 25),
         QString::number(selected + 1) + " / " + QString::number(labels.size()),
         17, QColor("#38c9bb"));
}
void Screen::toast(const QString &t) {
  notice = t;
  update();
  QTimer::singleShot(3000, this, [this, t] {
    if (notice == t) {
      notice.clear();
      update();
    }
  });
}
void Screen::activity() {
  if (dimmed) {
    dimmed = false;
    transientBrightness(readJson("/etc/reader/display.json")
                            .value("brightness_percent")
                            .toInt(100));
    idleChanged(false);
  }
  int seconds =
      readJson("/etc/reader/display.json").value("idle_dim_seconds").toInt(120);
  if (seconds > 0)
    idle.start(seconds * 1000);
  else
    idle.stop();
}
void Screen::closeInputs() {
  for (auto input : inputs) {
    delete input.notifier;
    ioctl(input.fd, EVIOCGRAB, 0);
    ::close(input.fd);
  }
  inputs.clear();
}
void Screen::foreground(bool enabled) {
  if (enabled) {
    discover();
    discovery.start(5000);
    showFullScreen();
    activity();
    update();
  } else {
    activity();
    idle.stop();
    discovery.stop();
    closeInputs();
    hide();
  }
}
void Screen::discover() {
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
    auto notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    inputs.append({fd, notifier, builtin});
    connect(notifier, &QSocketNotifier::activated, this, [this, fd](int) {
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
void Screen::dispatch(reader_button id, int value) {
  if (id == READER_NONE)
    return;
  if (id == READER_HOLD) {
    locked = value == 0;
    activity();
    update();
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
        discover();
        command("/usr/local/sbin/reader-brightness", {"init"});
        update();
      });
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
                toast("Volume " +
                      QString::number(QJsonDocument::fromJson(data)
                                          .object()
                                          .value("volume_percent")
                                          .toInt()) +
                      "%");
              });
    }
    return;
  }
  button(id, value);
}
void Screen::keyPressEvent(QKeyEvent *e) {
  reader_button id = READER_NONE;
  switch (e->key()) {
  case Qt::Key_Up:
    id = READER_UP;
    break;
  case Qt::Key_Down:
    id = READER_DOWN;
    break;
  case Qt::Key_Left:
    id = READER_LEFT;
    break;
  case Qt::Key_Right:
    id = READER_RIGHT;
    break;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    id = READER_OK;
    break;
  case Qt::Key_Escape:
    id = READER_BACK;
    break;
  default:
    break;
  }
  dispatch(id, e->isAutoRepeat() ? 2 : 1);
}
} // namespace ReRead

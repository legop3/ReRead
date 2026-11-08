#include "reader-ui.hpp"
#include <sys/statvfs.h>
using namespace ReRead;
class Launcher : public Screen {
  struct App {
    QString name, description, exec, id;
  };
  QList<App> apps;
  QString page = "home", information;
  int selected = 0;
  QStringList labels, details, files;
  QImage photo;
  QProcess *child = nullptr;
  void refresh() {
    labels.clear();
    details.clear();
    title = page == "home"       ? "ReRead"
            : page == "files"    ? "Files"
            : page == "settings" ? "Settings"
            : page == "power"    ? "Power"
                                 : "About ReRead";
    subtitle = "";
    hint = "↑↓ Navigate   OK Select   Options Settings";
    if (page == "home") {
      apps.clear();
      for (const auto &file : QDir("/usr/share/applications")
                                  .entryList({"*.desktop"}, QDir::Files)) {
        QFile entry("/usr/share/applications/" + file);
        if (!entry.open(QIODevice::ReadOnly))
          continue;
        QMap<QString, QString> desktop;
        bool group = false;
        for (const auto &line :
             QString::fromUtf8(entry.readAll()).split('\n')) {
          if (line.startsWith('[')) {
            group = line.trimmed() == "[Desktop Entry]";
            continue;
          }
          int equals = line.indexOf('=');
          if (group && equals > 0 && !line.startsWith('#')) {
            QString value = line.mid(equals + 1).trimmed();
            value.replace("\\n", "\n")
                .replace("\\s", " ")
                .replace("\\t", "\t")
                .replace("\\r", "\r")
                .replace("\\\\", "\\");
            desktop[line.left(equals).trimmed()] = value;
          }
        }
        if (desktop.value("Type") != "Application" ||
            desktop.value("X-ReRead") != "true" ||
            desktop.value("Hidden") == "true" ||
            desktop.value("NoDisplay") == "true")
          continue;
        App app{desktop.value("Name"), desktop.value("Comment"),
                desktop.value("Exec"), file.left(file.size() - 8)};
        if (app.name.isEmpty() || app.exec.isEmpty())
          continue;
        apps.append(app);
        labels << app.name;
        details << app.description;
      }
      labels << "Files"
             << "Settings";
      details << "Photos and files stored on this Reader"
              << "Display, sound, connections and power";
      subtitle = "Your Reader · Apps";
    } else if (page == "files") {
      files = QDir("/var/lib/reader/files").entryList(QDir::Files, QDir::Time);
      labels = files;
      for (auto file : files) {
        QFileInfo info("/var/lib/reader/files/" + file);
        details << QString::number(info.size() / 1024) + " KiB · " +
                       info.lastModified().toString("dd MMM HH:mm");
      }
      hint = "↑↓ Browse   OK Open image   Back Home";
    } else if (page == "settings") {
      int brightness = 100;
      QFile f("/sys/class/backlight/psb-bl/brightness");
      if (f.open(QIODevice::ReadOnly))
        brightness = f.readAll().trimmed().toInt();
      auto config = readJson("/etc/reader/display.json");
      int idle = config.value("idle_dim_seconds").toInt(120);
      labels << "Brightness"
             << "Volume"
             << "Idle dim"
             << "Screen margin"
             << "Connections"
             << "About ReRead"
             << "Power";
      QFile audio("/run/reader-audio-status");
      QString volume;
      if (audio.open(QIODevice::ReadOnly))
        volume = QString::fromUtf8(audio.readAll()).trimmed();
      details << QString::number(brightness) + "% · Left / Right to adjust"
              << volume + " · Left / Right to adjust"
              << (idle ? QString::number(idle) +
                             " seconds · Left / Right to change"
                       : "Off · Left / Right to change")
              << QString::number(config.value("safe_margin").toInt(28)) +
                     " pixels · Left / Right to adjust"
              << "Wi-Fi and USB information"
              << "Version, storage and application information"
              << "Sleep, reboot or shut down";
      hint = "↑↓ Navigate   ←→ Adjust   Back Home";
    } else if (page == "power") {
      labels << "Sleep"
             << "Reboot"
             << "Shut down";
      details << "Wake with the power button"
              << "Restart ReRead"
              << "Turn the Reader off";
      hint = "↑↓ Navigate   OK Select   Back Cancel";
    } else if (page == "connections") {
      title = "Connections";
      hint = "Back Settings";
      information = "Checking Wi-Fi…";
      command(
          "/usr/bin/python3",
          {"-c", "import sys,json;sys.path.insert(0,'/usr/local/sbin');import "
                 "reader_wifi;print(json.dumps(reader_wifi.status()))"},
          [this](QByteArray data) {
            if (page != "connections")
              return;
            auto state = QJsonDocument::fromJson(data).object();
            information =
                "Wi-Fi: " + state.value("ssid").toString("Not connected") +
                "\nAddress: " + state.value("address").toString("—") +
                "\nUSB management: http://192.168.7.1\nWi-Fi setup, uploads "
                "and updates are available over USB.";
            update();
          });
    } else if (page == "about") {
      QFile f("/usr/share/reader/version");
      QString version;
      if (f.open(QIODevice::ReadOnly))
        version = QString::fromUtf8(f.readAll()).trimmed();
      struct statvfs fs {};
      statvfs("/var/lib/reader", &fs);
      information =
          "ReRead " + version + " · antiX Linux\n" +
          QString::number(double(fs.f_bavail) * fs.f_frsize / 1048576, 'f', 0) +
          " MiB free of " +
          QString::number(double(fs.f_blocks) * fs.f_frsize / 1048576, 'f', 0) +
          " MiB\nApps use standard Debian packages and Linux "
          "processes.\nPower: short press sleeps. Volume keys work in every "
          "app.\nFavorites returns to Home. Back returns one screen.";
      hint = "Back Settings";
    }
    selected = qBound(0, selected, qMax(0, labels.size() - 1));
    update();
  }
  void go(const QString &destination) {
    page = destination;
    selected = 0;
    photo = QImage();
    refresh();
  }
  void launch(const App &app) {
    auto args = QProcess::splitCommand(app.exec);
    if (args.isEmpty())
      return;
    // .desktop Exec field codes are not shell syntax. ReRead apps take no files
    // at launch.
    for (int i = args.size() - 1; i >= 0; i--)
      if (args[i].startsWith('%'))
        args.removeAt(i);
    QString executable = args.takeFirst();
    child = new QProcess(this);
    auto env = QProcessEnvironment::systemEnvironment();
    QString data = "/var/lib/reader/apps/" + app.id;
    QDir().mkpath(data);
    env.insert("READER_APP_ID", app.id);
    env.insert("READER_APP_DATA_DIR", data);
    env.insert("XDG_DATA_HOME", data);
    child->setProcessEnvironment(env);
    child->setStandardOutputFile("/var/log/reader/apps.log", QIODevice::Append);
    child->setStandardErrorFile("/var/log/reader/apps.log", QIODevice::Append);
    connect(child,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
              child->deleteLater();
              child = nullptr;
              foreground(true);
              refresh();
              if (code)
                toast("App closed unexpectedly. See apps.log over USB.");
            });
    connect(child, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
              if (error == QProcess::FailedToStart) {
                child->deleteLater();
                child = nullptr;
                foreground(true);
                refresh();
                toast("Unable to start this app.");
              }
            });
    foreground(false);
    child->start(executable, args);
  }
  void button(reader_button id, int value) override {
    if (id == READER_FAVORITES) {
      go("home");
      return;
    }
    if (id == READER_GUIDE) {
      toast("Arrows navigate · OK selects · Back returns · Power sleeps");
      return;
    }
    if (id == READER_BACK) {
      if (page == "home")
        return;
      go(page == "settings" || page == "files" ? "home"
         : page == "image"                     ? "files"
                                               : "settings");
      return;
    }
    if (id == READER_OPTIONS) {
      go("settings");
      return;
    }
    if (id == READER_SHOOT && page == "home") {
      for (const auto &app : apps)
        if (app.id == "reread-camera") {
          launch(app);
          return;
        }
    }
    if (page == "image") {
      if (id == READER_LEFT || id == READER_RIGHT) {
        selected = qBound(0, selected + (id == READER_RIGHT ? 1 : -1),
                          files.size() - 1);
        openPhoto();
      }
      return;
    }
    if (id == READER_UP || id == READER_DOWN) {
      selected = qBound(0, selected + (id == READER_DOWN ? 1 : -1),
                        qMax(0, labels.size() - 1));
      update();
      return;
    }
    if (page == "settings" &&
        (id == READER_LEFT || id == READER_RIGHT || id == READER_OK) &&
        selected < 4) {
      int direction = id == READER_LEFT ? -1 : 1;
      if (selected == 0)
        command("/usr/local/sbin/reader-brightness",
                {direction < 0 ? "down" : "up"},
                [this](QByteArray) { refresh(); });
      if (selected == 1)
        command("/usr/local/sbin/reader-audio", {direction < 0 ? "down" : "up"},
                [this](QByteArray) { refresh(); });
      if (selected >= 2) {
        auto config = readJson("/etc/reader/display.json");
        if (selected == 2) {
          QList<int> choices{0, 30, 60, 120, 300};
          int i = choices.indexOf(config.value("idle_dim_seconds").toInt(120));
          config["idle_dim_seconds"] =
              choices[qBound(0, i + direction, choices.size() - 1)];
        } else
          config["safe_margin"] = qBound(
              20, config.value("safe_margin").toInt(28) + direction * 4, 60);
        writeJson("/etc/reader/display.json", config);
        refresh();
      }
      return;
    }
    if (id != READER_OK || value != 1)
      return;
    if (page == "home") {
      if (selected < apps.size())
        launch(apps[selected]);
      else
        go(selected == apps.size() ? "files" : "settings");
    } else if (page == "files")
      openPhoto();
    else if (page == "settings")
      go(selected == 4 ? "connections" : selected == 5 ? "about" : "power");
    else if (page == "power") {
      if (selected == 0) {
        dispatch(READER_POWER, 1);
        dispatch(READER_POWER, 0);
      } else {
        title = selected == 1 ? "Rebooting…" : "Shutting down…";
        labels.clear();
        update();
        QProcess::startDetached("/sbin/shutdown",
                                {selected == 1 ? "-r" : "-h", "now"});
      }
    }
  }
  void openPhoto() {
    if (files.isEmpty())
      return;
    QImageReader reader("/var/lib/reader/files/" + files.value(selected));
    reader.setAutoTransform(true);
    reader.setScaledSize(
        reader.size().scaled(content().size(), Qt::KeepAspectRatio));
    auto loaded = reader.read();
    if (loaded.isNull()) {
      toast("No image viewer for this file. Download it over USB.");
      return;
    }
    photo = loaded;
    page = "image";
    title = "Photo";
    subtitle = files.value(selected);
    hint = "←→ Previous / Next   Back Files";
    update();
  }
  void draw(QPainter &p, const QRect &r) override {
    if (page == "image") {
      auto target = photo.size().scaled(r.size(), Qt::KeepAspectRatio);
      p.drawImage(QRect(QPoint(r.center().x() - target.width() / 2,
                               r.center().y() - target.height() / 2),
                        target),
                  photo);
    } else if (page == "about" || page == "connections")
      text(p, r, information, 25);
    else
      list(p, r, labels, details, selected);
  }

public:
  Launcher() : Screen("ReRead") {
    refresh();
    auto watcher = new QFileSystemWatcher(this);
    watcher->addPath("/usr/share/applications");
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
      if (page == "home")
        refresh();
    });
  }
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setFont(QFont("DejaVu Sans"));
  Launcher window;
  window.showFullScreen();
  return app.exec();
}

#include <gio/gdesktopappinfo.h>
#include "reader-ui.hpp"
#include <sys/statvfs.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
using namespace ReRead;
class Launcher : public Screen {
  struct App {
    QString name, description, desktop, id;
    QIcon icon;
  };
  QList<App> apps;
  QString page = "home", information;
  int selected = 0;
  qint64 lastAdjustment = 0;
  QStringList labels, details;
  static QString filesRoot() { return qEnvironmentVariable("REREAD_FILES_DIR", "/var/lib/reader/files"); }
  QList<QIcon> icons;
  static constexpr int columns = 3, pageSize = 6;
  GPid child = 0;
  int childFd = -1;
  QSocketNotifier *childWatch = nullptr;
  QMap<QString, int> positions;
  bool adjustmentPage() const {
    return page == "display" || page == "sound" || (page == "power" && selected == 0);
  }
  void selectionControls() {
    if (adjustmentPage())
      controls = {{READER_LEFT,"Less"},{READER_RIGHT,"More"},{READER_BACK,"Settings"}};
    else if (page == "power")
      controls = {{READER_OK,"Select"},{READER_BACK,"Settings"}};
  }
  static QString duration(int seconds) {
    return seconds == 0 ? "Off" : seconds < 60 ? QString::number(seconds) + " seconds"
                                              : QString::number(seconds / 60) + " min";
  }
  static QIcon icon(const QString &name) {
    QIcon result = name.startsWith('/') ? QIcon(name) : QIcon::fromTheme(name);
    return result.isNull() ? QIcon::fromTheme("application-x-executable") : result;
  }
  void refresh() {
    showHeader = true;
    showControls = true;
    controlsOverlay = false;
    labels.clear();
    details.clear();
    icons.clear();
    title = page == "home"       ? "ReRead"
            : page == "settings" ? "Settings"
            : page == "power"    ? "Power"
                                 : "About ReRead";
    subtitle = "";
    controls = {{READER_OK,"Open"},{READER_OPTIONS,"Settings"}};
    if (page == "home") {
      showControls = false;
      apps.clear();
      GList *entries = g_app_info_get_all();
      for (GList *item = entries; item; item = item->next) {
        if (!G_IS_DESKTOP_APP_INFO(item->data)) continue;
        auto entry = G_DESKTOP_APP_INFO(item->data);
        auto info = G_APP_INFO(entry);
        if (!g_desktop_app_info_get_boolean(entry,"X-ReRead") ||
            !g_app_info_should_show(info)) continue;
        const char *filename = g_desktop_app_info_get_filename(entry);
        const char *id = g_app_info_get_id(info);
        if (!filename || !id) continue;
        gchar *art = g_desktop_app_info_get_string(entry,"Icon");
        App app{QString::fromUtf8(g_app_info_get_display_name(info)),
                QString::fromUtf8(g_app_info_get_description(info)),
                QString::fromUtf8(filename),QString::fromUtf8(id),
                icon(QString::fromUtf8(art))};
        if (app.id.endsWith(".desktop")) app.id.chop(8);
        if (!art || !*art) qWarning() << id << "needs an Icon= entry; using the fallback icon";
        g_free(art);
        apps.append(app);
      }
      g_list_free_full(entries,g_object_unref);
      std::sort(apps.begin(),apps.end(),[](const App &a,const App &b){return a.id<b.id;});
      for (const auto &app: apps) {
        icons << app.icon; labels << app.name; details << app.description;
      }
      labels << "Settings";
      details << "Display, sound, connections and power";
      icons << icon("preferences-system");
    } else if (page == "settings") {
      labels << "Display" << "Sound" << "Power" << "Connections" << "About ReRead";
      details << "Brightness and idle dimming" << "Speaker and headphone volume"
              << "Automatic sleep, restart and shutdown" << "Wi-Fi and USB"
              << "Version and storage";
      for (const auto &name : {"video-display", "audio-volume-high", "system-suspend",
                               "network-wireless", "help-about"}) icons << icon(name);
      controls = {{READER_OK,"Open"},{READER_BACK,"Home"}};
    } else if (page == "display") {
      title = "Settings / Display";
      auto config = readJson("/etc/reader/display.json");
      labels << "Brightness" << "Dim after";
      details << QString::number(config.value("brightness_percent").toInt(100)) + "%"
              << duration(config.value("idle_dim_seconds").toInt(120));
    } else if (page == "sound") {
      title = "Settings / Sound";
      labels << "Volume";
      QFile audio("/run/reader-audio-status");
      details << (audio.open(QIODevice::ReadOnly)
                  ? QString::fromUtf8(audio.readAll()).trimmed() : "Use the volume buttons");
    } else if (page == "power") {
      title = "Settings / Power";
      labels << "Auto sleep after" << "Sleep now" << "Restart" << "Shut down";
      details << duration(readJson("/etc/reader/display.json")
                              .value("auto_suspend_seconds").toInt(0)) + " · without button activity"
              << "Wake with the power button" << "Restart ReRead" << "Turn the Reader off";
      controls = {{READER_OK,"Select"},{READER_BACK,"Settings"}};
    } else if (page == "connections") {
      title = "Settings / Connections";
      controls = {{READER_BACK,"Settings"}};
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
      title = "Settings / About";
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
      controls = {{READER_BACK,"Settings"}};
    }
    selected = qBound(0, selected, qMax(0, labels.size() - 1));
    selectionControls();
    update();
  }
  void go(const QString &destination) {
    positions[page] = selected;
    page = destination;
    selected = positions.value(page);
    refresh();
  }
  static void childSetup(gpointer data) {
    // GIO owns Exec parsing/spawning; retain the foreground parent-death rule.
    pid_t parent = *static_cast<pid_t *>(data);
    if (prctl(PR_SET_PDEATHSIG,SIGTERM) != 0 || getppid() != parent) _exit(127);
  }
  static void launched(GDesktopAppInfo *, GPid pid, gpointer data) {
    auto self = static_cast<Launcher *>(data);
    self->child = pid;
    self->childFd = syscall(SYS_pidfd_open,pid,0);
  }
  void appFinished() {
    int status = 0;
    if (waitpid(child,&status,WNOHANG) != child) return;
    g_spawn_close_pid(child); child = 0;
    if (childWatch) { childWatch->setEnabled(false);childWatch->deleteLater();childWatch=nullptr; }
    if (childFd >= 0) { ::close(childFd);childFd=-1; }
    foreground(true); refresh();
    if (!WIFEXITED(status) || WEXITSTATUS(status))
      toast("App closed unexpectedly. See apps.log over USB.");
  }
  void launch(const App &app) {
    GDesktopAppInfo *entry = g_desktop_app_info_new_from_filename(app.desktop.toUtf8());
    if (!entry) { toast("Unable to read this application entry.");return; }
    // Foreground ownership needs a direct process, not terminal/D-Bus activation.
    if (g_desktop_app_info_get_boolean(entry,"Terminal") ||
        g_desktop_app_info_get_boolean(entry,"DBusActivatable")) {
      g_object_unref(entry);toast("This app needs a direct foreground executable.");return;
    }
    auto context = g_app_launch_context_new();
    g_app_launch_context_unsetenv(context,"QT_QPA_FB_DISABLE_INPUT");
    QString data = "/var/lib/reader/apps/" + app.id;
    QDir().mkpath(data);
    g_app_launch_context_setenv(context,"REREAD_FILES_DIR",filesRoot().toUtf8());
    g_app_launch_context_setenv(context,"READER_APP_ID",app.id.toUtf8());
    g_app_launch_context_setenv(context,"READER_APP_DATA_DIR",data.toUtf8());
    g_app_launch_context_setenv(context,"XDG_DATA_HOME",data.toUtf8());
    int log = open("/var/log/reader/apps.log",O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0644);
    pid_t parent = getpid(); GError *error = nullptr;
    foreground(false);
    gboolean ok = g_desktop_app_info_launch_uris_as_manager_with_fds(
      entry,nullptr,context,G_SPAWN_DO_NOT_REAP_CHILD,childSetup,&parent,
      launched,this,STDIN_FILENO,log,log,&error);
    if (log >= 0) ::close(log);
    g_object_unref(context);g_object_unref(entry);
    if (!ok || !child) {
      if (error) {qWarning() << error->message;g_error_free(error);}
      foreground(true);refresh();toast("Unable to start this app.");return;
    }
    if (childFd >= 0) {
      childWatch = new QSocketNotifier(childFd,QSocketNotifier::Read,this);
      connect(childWatch,&QSocketNotifier::activated,this,[this](int){appFinished();});
    } else {
      // The supported Reader kernel has pidfd_open. Reap an already-exited
      // child immediately; report an unsupported kernel rather than lose Home.
      kill(child,SIGKILL);waitpid(child,nullptr,0);g_spawn_close_pid(child);child=0;
      foreground(true);refresh();toast("Cannot track the application process.");
    }
  }
  void button(reader_button id, int value) override {
    if (value == 0)
      return;
    if (value == 2 && id != READER_UP && id != READER_DOWN &&
        id != READER_LEFT && id != READER_RIGHT)
      return;
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
      go(page == "settings" ? "home" : "settings");
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
    if (page == "home" && (id == READER_UP || id == READER_DOWN ||
                           id == READER_LEFT || id == READER_RIGHT)) {
      int count = labels.size();
      if (id == READER_LEFT && selected % columns != 0) --selected;
      if (id == READER_RIGHT && selected % columns != columns - 1 && selected + 1 < count) ++selected;
      if (id == READER_UP && selected >= columns) selected -= columns;
      if (id == READER_DOWN && (selected / columns + 1) * columns < count)
        selected = qMin(selected + columns, count - 1);
      update();
      return;
    }
    if (id == READER_UP || id == READER_DOWN) {
      selected = qBound(0, selected + (id == READER_DOWN ? 1 : -1),
                        qMax(0, labels.size() - 1));
      selectionControls();
      update();
      return;
    }
    if (adjustmentPage() && (id == READER_LEFT || id == READER_RIGHT)) {
      auto now = QDateTime::currentMSecsSinceEpoch();
      if (value == 2 && now - lastAdjustment < 200) return;
      lastAdjustment = now;
      int direction = id == READER_LEFT ? -1 : 1;
      if (page == "display" && selected == 0)
        command("/usr/local/sbin/reader-brightness", {direction < 0 ? "down" : "up"},
                [this](QByteArray) { refresh(); });
      else if (page == "sound")
        command("/usr/local/sbin/reader-audio", {direction < 0 ? "down" : "up"},
                [this](QByteArray) { refresh(); });
      else {
        auto config = readJson("/etc/reader/display.json");
        QString key = page == "power" ? "auto_suspend_seconds" : "idle_dim_seconds";
        QList<int> choices = page == "power" ? QList<int>{0,120,300,600,1800}
                                            : QList<int>{0,30,60,120,300};
        int current = config.value(key).toInt(page == "power" ? 0 : 120);
        int next = current;
        if (direction > 0) {
          for (int choice : choices) if (choice > current) { next = choice; break; }
        } else {
          for (int choice : choices) if (choice < current) next = choice;
        }
        config[key] = next;
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
        go("settings");
    } else if (page == "settings")
      go(QStringList{"display","sound","power","connections","about"}.value(selected));
    else if (page == "power") {
      if (selected == 0) return;
      if (selected == 1) {
        command("/usr/local/sbin/reader-power", {"suspend"});
      } else {
        title = selected == 2 ? "Rebooting…" : "Shutting down…";
        labels.clear();
        update();
        QProcess::startDetached("/sbin/shutdown",
                                {selected == 2 ? "-r" : "-h", "now"});
      }
    }
  }
  void drawHome(QPainter &p, const QRect &r) {
    int first = (selected / pageSize) * pageSize;
    const int gap = 16, footer = 50;
    int w = (r.width() - gap * 2) / columns;
    int h = (r.height() - footer - gap) / 2;
    int rows = qMin(2, (labels.size() - first + columns - 1) / columns);
    int top = r.y() + (r.height() - footer - (h * rows + gap * (rows - 1))) / 2;
    for (int i = first; i < qMin(first + pageSize, labels.size()); ++i) {
      int position = i - first;
      QRect tile(r.x() + (position % columns) * (w + gap),
                 top + (position / columns) * (h + gap), w, h);
      bool focus = i == selected;
      p.setPen(QPen(focus ? palette().link() : palette().mid(), focus ? 3 : 2));
      p.setBrush(palette().base());
      p.drawRect(tile.adjusted(2,2,-2,-2));
      p.fillRect(tile.adjusted(4,h-53,-4,-4),focus ? palette().highlight() : palette().button());
      int size = qMin(100, h - 58);
      icons.value(i).paint(&p, QRect(tile.center().x() - size/2, tile.top()+16, size, size));
      auto font = p.font(); font.setPixelSize(27); font.setBold(focus); p.setFont(font);
      p.setPen(palette().color(focus ? QPalette::HighlightedText : QPalette::WindowText));
      QString name = QFontMetrics(font).elidedText(labels[i], Qt::ElideRight, w-28);
      p.drawText(tile.adjusted(14,h-48,-14,-8), Qt::AlignCenter, name);
    }
    auto font = p.font(); font.setBold(false); font.setPixelSize(21); p.setFont(font);
    p.setPen(palette().color(QPalette::Dark));
    int pages = (labels.size() + pageSize - 1) / pageSize;
    QRect caption(r.x()+2,r.bottom()-footer+10,r.width()-(pages > 1 ? 110 : 0),40);
    p.drawText(caption, Qt::AlignVCenter,
               QFontMetrics(font).elidedText(details.value(selected), Qt::ElideRight, caption.width()));
    if (pages > 1)
      p.drawText(QRect(r.right()-100,caption.y(),100,40), Qt::AlignRight|Qt::AlignVCenter,
                 QString::number(selected/pageSize+1) + " / " + QString::number(pages));
  }
  void draw(QPainter &p, const QRect &r) override {
    if (page == "home")
      drawHome(p, r);
    else if (page == "settings") {
      // Categories show all five destinations at once; values live one level down.
      int height = r.height() / labels.size();
      for (int i = 0; i < labels.size(); ++i) {
        QRect row(r.x(),r.y()+i*height,r.width(),height-6);
        p.setPen(QPen(i == selected ? palette().link() : palette().mid(),i == selected ? 3 : 2));
        p.setBrush(i == selected ? palette().highlight() : palette().base());
        p.drawRect(row.adjusted(1,1,-1,-1));
        if (i == selected) p.fillRect(QRect(row.left()+1,row.top()+1,5,row.height()-2),palette().link());
        p.fillRect(QRect(row.x()+8,row.center().y()-28,56,56),palette().base());
        icons.value(i).paint(&p,QRect(row.x()+12,row.center().y()-24,48,48));
        const auto ink = palette().color(i == selected ? QPalette::HighlightedText : QPalette::WindowText);
        text(p,QRect(row.x()+80,row.y(),260,row.height()),labels[i],26,ink);
        text(p,QRect(row.x()+350,row.y(),row.width()-390,row.height()),details[i],20,
             palette().color(i == selected ? QPalette::HighlightedText : QPalette::Dark));
        text(p,QRect(row.right()-30,row.y(),24,row.height()),"›",30,ink);
      }
    } else if (page == "about" || page == "connections")
      text(p, r, information, 25);
    else
      list(p, r, labels, details, selected);
  }

public:
  ~Launcher() override {
    if (child) kill(child,SIGTERM);
    delete childWatch;
    if (childFd >= 0) ::close(childFd);
  }
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
  qputenv("XDG_CURRENT_DESKTOP","ReRead");
  QApplication app(argc, argv);
  // LinuxFB has no desktop platform theme to populate icon search paths.
  QStringList iconPaths{QDir::homePath() + "/.icons"}, pixmapPaths;
  for (const auto &path : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
    iconPaths << path + "/icons";
    pixmapPaths << path + "/pixmaps";
  }
  QIcon::setThemeSearchPaths(iconPaths + QIcon::themeSearchPaths());
  QIcon::setFallbackSearchPaths(pixmapPaths);
  QIcon::setThemeName("reread");
  QIcon::setFallbackThemeName("hicolor");
  app.setFont(QFont("DejaVu Sans"));
  Launcher window;
  window.showFullScreen();
  return app.exec();
}

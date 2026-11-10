#include <gio/gdesktopappinfo.h>
#include "reader-ui.hpp"
#include <sys/statvfs.h>
#include <fcntl.h>
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

    labels.clear();
    details.clear();
    icons.clear();
    title = page == "home"       ? "ReRead"
            : page == "settings" ? "Settings"
            : page == "power"    ? "Power"
                                 : "About ReRead";
    subtitle = "";
    controls = {{READER_UP,"Navigate"},{READER_OK,"Open"},{READER_OPTIONS,"Settings"}};
    if (page == "home") {
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
      labels << "Auto sleep after" << "Auto sleep on" << "Sleep now" << "Restart" << "Shut down";
      details << duration(readJson("/etc/reader/display.json")
                              .value("auto_suspend_seconds").toInt(0)) + " · without button activity"
              << (readJson("/etc/reader/display.json").value("auto_suspend_battery_only").toBool(false)
                      ? "Battery only" : "Battery and external power")
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
  void launch(const App &app) {
    GDesktopAppInfo *entry = g_desktop_app_info_new_from_filename(app.desktop.toUtf8());
    if (!entry) { toast("Unable to read this application entry.");return; }
    auto context = g_app_launch_context_new();
    QString data = "/var/lib/reader/apps/" + app.id;
    QDir().mkpath(data);
    g_app_launch_context_setenv(context,"REREAD_FILES_DIR",filesRoot().toUtf8());
    g_app_launch_context_setenv(context,"READER_APP_ID",app.id.toUtf8());
    g_app_launch_context_setenv(context,"READER_APP_DATA_DIR",data.toUtf8());
    int log = open("/var/log/reader/apps.log",O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0644);
    GError *error = nullptr;
    gboolean ok = g_desktop_app_info_launch_uris_as_manager_with_fds(
      entry,nullptr,context,G_SPAWN_DEFAULT,nullptr,nullptr,
      nullptr,nullptr,STDIN_FILENO,log,log,&error);
    if (log >= 0) ::close(log);
    g_object_unref(context);g_object_unref(entry);
    if (!ok) {
      if(error){qWarning()<<error->message;g_error_free(error);}
      toast("Unable to start this app.");
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
        auto config = readJson("/etc/reader/display.json");
        config["auto_suspend_battery_only"] = !config.value("auto_suspend_battery_only").toBool(false);
        writeJson("/etc/reader/display.json", config);
        refresh();
        return;
      }
      if (selected == 2) {
        command("/usr/local/sbin/reader-power", {"suspend"});
      } else {
        title = selected == 3 ? "Rebooting…" : "Shutting down…";
        labels.clear();
        update();
        QProcess::startDetached("/sbin/shutdown",
                                {selected == 3 ? "-r" : "-h", "now"});
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
      if (focus) {
        const QRect frame = tile.adjusted(3,3,-3,-3);
        p.setPen(QPen(palette().link(),3));
        // Corner marks indicate focus without enclosing every app in a card.
        for (const auto &corner : {frame.topLeft(),frame.topRight(),frame.bottomLeft(),frame.bottomRight()}) {
          const int dx = corner.x()==frame.left() ? 20 : -20;
          const int dy = corner.y()==frame.top() ? 20 : -20;
          p.drawLine(corner,corner+QPoint(dx,0));
          p.drawLine(corner,corner+QPoint(0,dy));
        }
        p.fillRect(tile.adjusted(10,h-51,-10,-9),palette().highlight());
      }
      int size = qMin(100, h - 58);
      icons.value(i).paint(&p, QRect(tile.center().x() - size/2, tile.top()+16, size, size));
      auto font = p.font(); font.setPixelSize(27); font.setBold(focus); p.setFont(font);
      p.setPen(palette().color(focus ? QPalette::HighlightedText : QPalette::ButtonText));
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
        if (i == selected) {
          p.fillRect(row,palette().highlight());
          p.fillRect(QRect(row.left(),row.top(),6,row.height()),palette().link());
        } else {
          p.setPen(QPen(palette().mid(),1));
          p.drawLine(row.left()+80,row.bottom(),row.right(),row.bottom());
        }
        p.fillRect(QRect(row.x()+8,row.center().y()-28,56,56),palette().base());
        icons.value(i).paint(&p,QRect(row.x()+12,row.center().y()-24,48,48));
        const auto ink = palette().color(i == selected ? QPalette::HighlightedText : QPalette::Text);
        text(p,QRect(row.x()+80,row.y(),260,row.height()),labels[i],26,ink);
        text(p,QRect(row.x()+350,row.y(),row.width()-390,row.height()),details[i],20,
             palette().color(i == selected ? QPalette::HighlightedText : QPalette::Text));
        text(p,QRect(row.right()-30,row.y(),24,row.height()),"›",30,ink);
      }
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
  qputenv("XDG_CURRENT_DESKTOP","ReRead");
  QApplication app(argc, argv);
  // No desktop environment supplies icon search paths; use standard data locations.
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
  window.show();
  return app.exec();
}

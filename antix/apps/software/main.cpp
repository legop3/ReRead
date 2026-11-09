#include "reader-ui.hpp"
using namespace ReRead;

class Software : public Screen {
  QJsonObject state;
  QProcess *request = nullptr;
  QTimer poll;
  QString page = "home", chosen, error;
  int selected = 0, logOffset = 0;
  QStringList labels, details, packages, actions;
  bool running() const { return state.value("job").toObject().value("state") == "running"; }
  QJsonObject candidate(const QString &name) const {
    for (const auto &value : state.value("catalog").toObject().value("packages").toArray())
      if (value.toObject().value("package") == name) return value.toObject();
    return {};
  }
  QJsonObject installed(const QString &name) const {
    for (const auto &value : state.value("apps").toArray())
      if (value.toObject().value("package") == name) return value.toObject();
    return {};
  }
  QString appName(const QString &name) const {
    auto info = installed(name);
    return info.isEmpty() ? name.mid(QString("reread-app-").size()) : info.value("name").toString();
  }
  void load(const QString &action = "status", const QString &package = {}) {
    if (request) { if (action != "status") toast("Please wait…"); return; }
    if (action != "status" && running()) { toast("A package operation is already running."); return; }
    auto process = new QProcess(this);
    request = process;
    connect(process, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished), this,
      [this,process,action](int code, QProcess::ExitStatus result) {
        request = nullptr;
        auto bytes = process->readAllStandardOutput();
        QString problem = QString::fromUtf8(process->readAllStandardError()).trimmed();
        QJsonParseError parse;
        auto document = QJsonDocument::fromJson(bytes, &parse);
        if (result != QProcess::NormalExit || code || parse.error != QJsonParseError::NoError || !document.isObject()) {
          error = problem.isEmpty() ? "Software manager unavailable. Try again." : problem;
          toast(error);
        } else {
          error.clear();
          if (action == "status") state = document.object();
          else { state.insert("job", document.object()); page = "operation"; selected = 0; logOffset = 0; }
        }
        process->deleteLater();
        poll.setInterval(running() ? 3000 : 10000);
        refresh();
        if (action != "status") QTimer::singleShot(0, this, [this] { load(); });
      });
    connect(process, &QProcess::errorOccurred, this, [this,process](QProcess::ProcessError problem) {
      if (problem == QProcess::FailedToStart) {
        request = nullptr; error = "Software manager is not installed correctly.";
        toast(error); process->deleteLater(); refresh();
      }
    });
    QTimer::singleShot(15000, process, [process] { if (process->state() != QProcess::NotRunning) process->kill(); });
    process->start("/usr/local/sbin/reader-software", package.isEmpty() ? QStringList{action} : QStringList{action,package});
  }
  void refresh() {
    title = "Software";
    subtitle = running() ? "Package operation in progress" : "";
    controls = {{READER_OK,"Select"},{READER_BACK,page == "home" ? "Home" : "Back"}};
    labels.clear(); details.clear(); packages.clear(); actions.clear();
    auto catalog = state.value("catalog").toObject();
    auto platform = candidate("reread");
    if (page == "home") {
      labels = QStringList{"Check for updates", "ReRead system", "Applications", "Current operation"};
      auto checked = catalog.value("checked");
      details << (checked.isDouble() ? "Last checked " + QDateTime::fromSecsSinceEpoch(qint64(checked.toDouble())).toString("d MMM, h:mm ap") : "Connect to Wi-Fi, then check for available software")
              << ("Installed " + state.value("version").toString() + (platform.value("upgrade").toBool() ? " · Update available" : ""))
              << "Install, update or remove applications"
              << state.value("job").toObject().value("message").toString("No package operation yet");
      if (!error.isEmpty()) subtitle = error;
    } else if (page == "apps") {
      title = "Applications";
      QSet<QString> names;
      for (const auto &value : state.value("apps").toArray()) names.insert(value.toObject().value("package").toString());
      for (const auto &value : catalog.value("packages").toArray()) {
        auto name = value.toObject().value("package").toString();
        if (name.startsWith("reread-app-")) names.insert(name);
      }
      packages = names.values(); std::sort(packages.begin(), packages.end());
      for (const auto &name : packages) {
        auto info = installed(name), available = candidate(name);
        labels << appName(name);
        details << (info.isEmpty() ? "Available · " + available.value("version").toString()
                    : "Installed " + info.value("version").toString() +
                      (available.value("upgrade").toBool() ? " · Update " + available.value("version").toString() : ""));
      }
      if (labels.isEmpty()) { labels << "No apps available"; details << "Check for updates first. Repositories can be managed over USB."; }
    } else if (page == "app" || page == "system") {
      auto name = page == "system" ? QString("reread") : chosen;
      title = page == "system" ? "ReRead system" : appName(name);
      auto available = candidate(name), info = installed(name);
      QString current = page == "system" ? state.value("version").toString() : info.value("version").toString();
      subtitle = current.isEmpty() ? "Not installed" : "Installed " + current;
      if (!available.isEmpty() && (current.isEmpty() || available.value("upgrade").toBool())) {
        labels << (current.isEmpty() ? "Install" : "Install update");
        details << ("Version " + available.value("version").toString() + " · " + available.value("description").toString());
        actions << "install";
      } else {
        labels << (catalog.value("checked").isDouble() ? "No newer version available" : "Check for updates first");
        details << "Use Back to return to Software"; actions << "";
      }
      if (page == "app" && !info.isEmpty()) {
        labels << "Remove application"; details << "Saved files and settings will be kept"; actions << "remove";
      }
      if (page == "system" && state.value("reboot_required").toBool()) {
        labels << "Restart needed"; details << "Use Home → Settings → Power → Restart"; actions << "";
      }
    } else if (page == "confirm") {
      title = "Remove " + appName(chosen) + "?";
      labels = QStringList{"Remove application", "Cancel"};
      details = QStringList{"Saved files and app data will be kept", "Return without removing"};
    } else if (page == "operation") {
      title = "Package operation";
      subtitle = state.value("job").toObject().value("state").toString("No operation");
      controls = {{READER_UP,"Scroll"},{READER_DOWN,"Scroll"},{READER_BACK,"Software"}};
    }
    selected = qBound(0, selected, qMax(0, labels.size()-1));
    update();
  }
  void go(const QString &next) { page = next; selected = 0; refresh(); }
protected:
  void draw(QPainter &painter, const QRect &area) override {
    if (page == "operation") {
      QStringList lines = state.value("log").toString().replace('\r','\n').split('\n',Qt::SkipEmptyParts);
      int count = qMax(1,(area.height()-75)/20);
      logOffset = qBound(0,logOffset,qMax(0,lines.size()-count));
      text(painter,QRect(area.x(),area.y(),area.width(),65),state.value("job").toObject().value("message").toString("No operation yet"),22);
      text(painter,area.adjusted(0,75,0,0),lines.mid(qMax(0,lines.size()-count-logOffset),count).join('\n'),15);
    } else list(painter,area,labels,details,selected);
  }
  void button(reader_button key, int value) override {
    if (!value) return;
    if (key == READER_BACK && value == 1) {
      if (page == "home") close();
      else if (page == "confirm") go("app");
      else if (page == "app") go("apps");
      else go("home");
      return;
    }
    if (key == READER_UP || key == READER_DOWN) {
      if (page == "operation") logOffset = qMax(0,logOffset+(key==READER_UP?3:-3));
      else selected = qBound(0,selected+(key==READER_UP?-1:1),qMax(0,labels.size()-1));
      update(); return;
    }
    if (key != READER_OK || value != 1) return;
    if (page == "home") {
      if (selected == 0) load("check");
      else go(selected == 1 ? "system" : selected == 2 ? "apps" : "operation");
    } else if (page == "apps" && selected < packages.size()) { chosen=packages[selected]; go("app"); }
    else if (page == "confirm") { if (selected == 0) load("remove",chosen); else go("app"); }
    else if ((page == "app" || page == "system") && selected < actions.size()) {
      if (actions[selected] == "remove") { go("confirm"); selected=1; update(); }
      else if (actions[selected] == "install") load("install",page == "system" ? "reread" : chosen);
    }
  }
public:
  Software() : Screen("Software") {
    connect(&poll,&QTimer::timeout,this,[this] { load(); });
    poll.start(10000); refresh(); load();
  }
};
int main(int argc,char **argv) {
  qputenv("QT_QPA_FB_DISABLE_INPUT","1");
  QApplication app(argc,argv); app.setFont(QFont("DejaVu Sans"));
  Software window; window.showFullScreen(); return app.exec();
}

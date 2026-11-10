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
  QIcon appIcon(const QString &package) const {
    auto metadata=readJson("/usr/share/reread/apps/"+package+".json");
    QSettings desktop("/usr/share/applications/"+metadata.value("desktop").toString(),QSettings::IniFormat);
    QString name=desktop.value("Desktop Entry/Icon").toString();
    return QIcon::fromTheme(name,QIcon("/usr/share/icons/reread/scalable/apps/application-x-executable.svg"));
  }
  void draw(QPainter &p, const QRect &r) override {
    const QColor accent=palette().color(QPalette::Link);
    if(page=="home") {
      const int width=r.width()*2/5;
      const QStringList symbols={"network-wireless","preferences-system","application-x-executable","help-about"};
      for(int i=0;i<labels.size();++i) {
        QRect row(r.x(),r.y()+i*76,width-20,66);
        if(i==selected)p.fillRect(row,Qt::white);
        p.fillRect(QRect(row.x()+5,row.y()+9,48,48),Qt::black);
        QIcon::fromTheme(symbols[i]).paint(&p,QRect(row.x()+11,row.y()+15,36,36));
        label(p,row.adjusted(65,0,-8,0),labels[i],23,i==selected,i==selected?Qt::black:Qt::white);
      }
      const int x=r.x()+width+18,w=r.right()-x;
      p.setPen(QPen(Qt::white,1));p.drawLine(x-18,r.top(),x-18,r.bottom());
      label(p,QRect(x,r.y(),w,28),"SYSTEM",19,true,accent);
      label(p,QRect(x,r.y()+35,w,48),"ReRead "+state.value("version").toString("…"),34,true);
      int upgrades=0;for(const auto &v:state.value("catalog").toObject().value("packages").toArray())
        if(v.toObject().value("upgrade").toBool())++upgrades;
      label(p,QRect(x,r.y()+92,w,34),QString::number(state.value("apps").toArray().size())+" apps installed",24);
      auto checked=state.value("catalog").toObject().value("checked");
      label(p,QRect(x,r.y()+132,w,34),!checked.isDouble()?"Check for available updates":QString::number(upgrades)+" updates available",24,true,accent);
      label(p,QRect(x,r.y()+184,w,26),"LAST CHECK",18,true,accent);
      label(p,QRect(x,r.y()+214,w,30),checked.isDouble()?QDateTime::fromSecsSinceEpoch(qint64(checked.toDouble())).toString("d MMM yyyy · HH:mm"):"Not checked yet",23);
      label(p,QRect(x,r.y()+266,w,26),"OPERATION",18,true,accent);
      label(p,QRect(x,r.y()+297,w,32),running()?"Package operation running":state.value("job").toObject().value("message").toString("Ready"),23);
      if(!error.isEmpty())label(p,QRect(x,r.bottom()-32,w,30),error,20,true,QColor("#ff3030"));
    } else if(page=="apps") {
      const auto layout=listInfo(p,r.adjusted(0,42,0,0),packages.size(),selected,64,0.6);
      label(p,QRect(r.x(),r.y(),layout.entries.width(),30),QString::number(packages.size())+" applications",22,true,accent);
      for(int i=layout.first;i<qMin(layout.first+layout.visible,packages.size());++i) {
        QRect row=listInfoRow(p,layout,i,selected,4);bool focus=i==selected;
        p.fillRect(QRect(row.x()+5,row.y()+5,50,50),Qt::black);
        appIcon(packages[i]).paint(&p,QRect(row.x()+10,row.y()+10,40,40));
        label(p,row.adjusted(66,-16,-8,0),labels[i],24,focus,palette().color(focus?QPalette::HighlightedText:QPalette::Text));
        label(p,QRect(row.x()+66,row.y()+34,row.width()-74,22),details[i],18,false,palette().color(focus?QPalette::HighlightedText:QPalette::Text));
      }
      const QRect side=layout.info;
      if(packages.isEmpty()) {text(p,side,"Check for apps first.\n\nRepositories can be managed through the web interface.",23);return;}
      auto info=installed(packages[selected]),available=candidate(packages[selected]);
      appIcon(packages[selected]).paint(&p,QRect(side.x(),side.y(),64,64));
      label(p,QRect(side.x(),side.y()+76,side.width(),34),labels[selected],26,true);
      QString description=info.value("description").toString(available.value("description").toString());
      auto font=p.font();font.setBold(false);p.setFont(font);
      text(p,QRect(side.x(),side.y()+120,side.width(),140),description.isEmpty()?"Application package":description,22);
      label(p,QRect(side.x(),side.bottom()-60,side.width(),28),info.isEmpty()?"AVAILABLE TO INSTALL":available.value("upgrade").toBool()?"UPDATE AVAILABLE":"INSTALLED",19,true,accent);
      label(p,QRect(side.x(),side.bottom()-27,side.width(),26),QString::number(selected+1)+" of "+QString::number(packages.size()),18);
    } else if(page=="operation") {
      auto job=state.value("job").toObject();auto phase=job.value("state").toString();
      QColor status=phase=="failed"?QColor("#ff3030"):running()?accent:QColor("#00df40");
      label(p,QRect(r.x(),r.y(),r.width(),38),phase.isEmpty()?"No operation yet":phase.toUpper(),27,true,status);
      label(p,QRect(r.x(),r.y()+42,r.width(),34),job.value("message").toString("Package activity will appear here"),23);
      p.setPen(QPen(Qt::white,1));p.drawLine(r.left(),r.y()+88,r.right(),r.y()+88);
      auto lines=state.value("log").toString().replace('\r','\n').split('\n',Qt::SkipEmptyParts);
      int count=qMax(1,(r.height()-104)/25);logOffset=qBound(0,logOffset,qMax(0,lines.size()-count));
      auto font=p.font();font.setFamily("DejaVu Sans Mono");font.setBold(false);font.setPixelSize(19);p.setFont(font);p.setPen(Qt::white);
      const int first=qMax(0,lines.size()-count-logOffset);
      for(int i=0;i<count&&first+i<lines.size();++i)
        p.drawText(QRect(r.x(),r.y()+104+i*25,r.width(),25),Qt::AlignVCenter,QFontMetrics(font).elidedText(lines[first+i],Qt::ElideRight,r.width()));
    } else if(page=="app"||page=="system") {
      const QString name=page=="system"?"reread":chosen;
      if(page=="app")appIcon(name).paint(&p,QRect(r.x(),r.y(),72,72));
      label(p,QRect(r.x()+(page=="app"?90:0),r.y(),r.width()-90,36),subtitle,30,true);
      label(p,QRect(r.x()+(page=="app"?90:0),r.y()+40,r.width()-90,30),candidate(name).isEmpty()?"No catalog information":"Available "+candidate(name).value("version").toString(),23,false,accent);
      list(p,r.adjusted(0,94,0,0),labels,details,selected);
    } else list(p,r,labels,details,selected);
  }
  void button(reader_button key, int value) override {
    if (!value) return;
    if (key == READER_FAVORITES && value == 1) { close(); return; }
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
  QStringList iconPaths;
  for(const auto &path:QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))iconPaths << path+"/icons";
  QIcon::setThemeSearchPaths(iconPaths);QIcon::setThemeName("reread");QIcon::setFallbackThemeName("hicolor");
  Software window; window.showFullScreen(); return app.exec();
}

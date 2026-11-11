#include "reader-ui.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSaveFile>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileSystemWatcher>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
using namespace ReRead;
class J2ME : public Screen {
  QStringList games;
  QMap<QString,QJsonObject> metadata;
  QFileSystemWatcher watcher;
  int selected=0, field=0;
  bool options=false, running=false, confirmDelete=false, errorPage=false;
  QString errorText;
  QString message;
  QProcess child;
  QJsonObject profile;
  const QString state="/var/lib/reader/apps/j2me";
  QString game() const {return games.value(selected);}
  QString identity() const {
    return QString::fromLatin1(QCryptographicHash::hash(game().toUtf8(),QCryptographicHash::Sha256).toHex().left(20));
  }
  QString profilePath() const {return state+"/profiles/"+identity()+".json";}
  int setting(const char *key,int fallback) const {return profile.value(QLatin1String(key)).toInt(fallback);}
  void load() {
    profile=QJsonObject{};
    QFile f(profilePath());
    if(f.open(QIODevice::ReadOnly))profile=QJsonDocument::fromJson(f.readAll()).object();
  }
  void save() {
    QDir().mkpath(state+"/profiles");
    QSaveFile f(profilePath());
    if(f.open(QIODevice::WriteOnly)) {
      f.write(QJsonDocument(profile).toJson(QJsonDocument::Indented));
      f.commit();
    }
  }
  void scan() {
    QString keep=game();
    games.clear();
    const QString jarFolder="/var/lib/reader/files/J2ME";
    QDir().mkpath(jarFolder);
    QDirIterator it(jarFolder,QStringList()<<"*.jar"<<"*.JAR",
                    QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);
    while(it.hasNext() && games.size()<200)games<<it.next();
    games.sort(Qt::CaseInsensitive);
    metadata.clear();
    QProcess inspector;
    inspector.start("/usr/bin/python3",{"/usr/lib/reread/apps/j2me/catalog.py",jarFolder});
    if(inspector.waitForFinished(5000) && inspector.exitCode()==0) {
      const QJsonArray entries=QJsonDocument::fromJson(inspector.readAllStandardOutput()).array();
      for(const auto &entry:entries) {
        const QJsonObject item=entry.toObject();
        const QString path=item.value("path").toString();
        if(games.contains(path))metadata.insert(path,item);
      }
    }
    if(!watcher.directories().isEmpty())watcher.removePaths(watcher.directories());
    watcher.addPath(jarFolder);
    QDirIterator dirs(jarFolder,QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks,
                      QDirIterator::Subdirectories);
    while(dirs.hasNext())watcher.addPath(dirs.next());
    selected=games.indexOf(keep);
    if(selected<0)selected=0;
    load();chrome();
  }
  void chrome() {
    showHeader=true;
    title=errorPage?"Game failed":confirmDelete?"Erase game data?":options?"Game settings":"Java ME";
    subtitle=(options||confirmDelete||errorPage)?metadata.value(game()).value("title").toString(QFileInfo(game()).fileName()):QString::number(games.size())+" games in Files/J2ME";
    controls=running?QList<Control>{{READER_GUIDE,"Hold + Back: Exit"}}:
      errorPage?QList<Control>{{READER_BACK,"Games"}}:
      confirmDelete?QList<Control>{{READER_OK,"Erase"},{READER_BACK,"Cancel"}}:
      options?QList<Control>{{READER_UP,"Setting"},{READER_LEFT,"Change"},{READER_BACK,"Games"},{READER_OK,"Change"}}:
      QList<Control>{{READER_UP,"Select"},{READER_OK,"Play"},{READER_OPTIONS,"Settings"},{READER_BACK,"Home"}};
    update();
  }
  void change(int amount) {
    if(game().isEmpty())return;
    if(field==7) {
      profile=QJsonObject{};save();message="Game settings reset.";update();return;
    }
    if(field==8) {confirmDelete=true;chrome();return;}
    static const char* keys[]={"orientation","width","height","fps","layout","fullscreen","scale"};
    const char *k=keys[field];
    if(field==0)profile[k]=(setting(k,0)+4+amount%4)%4;
    if(field==1)profile[k]=qBound(128,setting(k,240)+amount*16,480);
    if(field==2)profile[k]=qBound(128,setting(k,320)+amount*16,480);
    if(field==3)profile[k]=qBound(10,setting(k,60)+amount*5,60);
    if(field==4)profile[k]=(setting(k,0)+11+amount%11)%11;
    if(field==5)profile[k]=1-setting(k,1);
    if(field==6)profile[k]=qBound(1,setting(k,1)+amount,4);
    save();update();
  }
  void start() {
    if(game().isEmpty()||running)return;
    QString runtime="/usr/lib/reread/apps/j2me/freej2me_plus.jar";
    if(!QFileInfo::exists(runtime)){message="FreeJ2ME-Plus is not installed.";update();return;}
    const int rotation=setting("orientation",0)*90,w=setting("width",240),h=setting("height",320);
    QDir().mkpath(state+"/data/"+identity());
    child.setWorkingDirectory(state+"/data/"+identity());
    // Run the original AWT entry point: the direct -jar invocation was
    // already verified on actual Reader hardware. The reflective bootstrap
    // previously exited successfully without ever presenting the game.
    child.setProgram("/usr/bin/java");
    child.setArguments({"-Xms16m","-Xmx128m",
        "-Dreread.rotation="+QString::number(rotation),"-jar",runtime,
        QUrl::fromLocalFile(game()).toString(QUrl::FullyEncoded),
        QString::number(setting("fullscreen",1)),QString::number(w),QString::number(h),
        QString::number(setting("scale",1)),QString::number(setting("layout",0)),QString::number(setting("fps",60))});
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.setStandardOutputFile(state+"/data/"+identity()+"/last-run.log",QIODevice::Truncate);
    profile["plays"]=setting("plays",0)+1;
    profile["lastPlayed"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    save();
    errorPage=false;errorText.clear();
    message="Starting "+QFileInfo(game()).fileName();
    running=true;
    child.start();
    // One visible app experience: the game owns the screen while running.
    // Do not leave a competing ReRead "running" status window onscreen.
    if(child.waitForStarted(2000)) hide();
    chrome();
  }
protected:
  void button(reader_button id,int value) override {
    if(value!=1 && !(value==2 && (id==READER_UP||id==READER_DOWN||id==READER_LEFT||id==READER_RIGHT)))return;
    if(running) {
      if(id==READER_FAVORITES){child.terminate();if(!child.waitForFinished(1500))child.kill();}
      return;
    }
    if(id==READER_FAVORITES){close();return;}
    if(errorPage){errorPage=false;chrome();return;}
    if(confirmDelete) {
      if(id==READER_BACK || id==READER_OPTIONS){confirmDelete=false;chrome();return;}
      if(id==READER_OK) {
        const QString data=state+"/data/"+identity();
        const bool cleared=!QFileInfo::exists(data)||QDir(data).removeRecursively();
        confirmDelete=false;
        message=cleared?"Game data erased.":"Unable to erase game data.";
        chrome();return;
      }
      return;
    }
    if(options) {
      if(id==READER_BACK||id==READER_OPTIONS){options=false;chrome();return;}
      if(id==READER_UP||id==READER_DOWN){field=qBound(0,field+(id==READER_DOWN?1:-1),8);update();return;}
      if(id==READER_LEFT||id==READER_RIGHT||id==READER_OK){change(id==READER_LEFT?-1:1);return;}
      return;
    }
    if(id==READER_BACK){close();return;}
    if(id==READER_OPTIONS&&!games.isEmpty()){options=true;field=0;chrome();return;}
    if(id==READER_OK){start();return;}
    if(id==READER_UP||id==READER_DOWN){
      selected=qBound(0,selected+(id==READER_DOWN?1:-1),qMax(0,games.size()-1));load();update();
    }
  }
  void draw(QPainter &p,const QRect &r) override {
    if(running){text(p,r,"Starting "+QFileInfo(game()).fileName()+"...",25);return;}
    if(errorPage) {
      text(p,r,errorText,20);
    } else if(confirmDelete) {
      text(p,r,"Erase the saved progress and emulator data for this game?\n\n"
          "The JAR file and game settings will be kept.\n\n"
          "Press OK to ERASE or Back to cancel.",26);
    } else if(options){
      QStringList labels={"Rotation","Virtual width","Virtual height","Frame cap","Phone layout","Fullscreen","Window scale","Reset settings","Erase saved data…"};
      const QStringList layouts={"Standard","LG","Motorola","Moto Triplets","Moto V8","Moto A1000","Nokia","Sagem","Siemens","SKT","KDDI"};
      QStringList detail={QString::number(setting("orientation",0)*90)+QString::fromUtf8("°"),
          QString::number(setting("width",240))+" px",QString::number(setting("height",320))+" px",
          QString::number(setting("fps",60))+" FPS",
          layouts.value(setting("layout",0),"Standard"),
          setting("fullscreen",1)?"On":"Off",QString::number(setting("scale",1))+"×",
          "Use standard defaults","Keep JAR; clear game progress"};
      list(p,r,labels,detail,field);
    }else if(games.isEmpty()){
      text(p,r,"No Java ME games found.\n\nUpload .jar games to Files/J2ME.\n"
          "Return here to browse your games.",26);
    }else {
      QStringList names,descriptions;
      for(const QString &g:games){
        const auto m=metadata.value(g);
        names<<m.value("title").toString(QFileInfo(g).completeBaseName());
        const QString vendor=m.value("vendor").toString();
        const QString version=m.value("version").toString();
        descriptions<<(vendor.isEmpty()?QString::number(QFileInfo(g).size()/1024)+" KiB":vendor+
                      (version.isEmpty()?"":" · "+version));
      }
      list(p,r,names,descriptions,selected);
    }
    if(!message.isEmpty())label(p,QRect(r.x(),r.bottom()-32,r.width(),30),message,18);
  }
public:
  J2ME():Screen("Java ME") {
    connect(&watcher,&QFileSystemWatcher::directoryChanged,this,[this]{if(!running&&!options)scan();});
    connect(&child,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){
      if(e==QProcess::FailedToStart){running=false;show();raise();activateWindow();errorPage=true;
        errorText="Unable to launch Java:\n"+child.errorString();chrome();}
    });
    connect(&child,static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
            this,[this](int code,QProcess::ExitStatus){
      running=false;
      show();raise();activateWindow();
      if(code==0) {
        message="Game closed.";
      } else {
        errorPage=true;
        const QString log=state+"/data/"+identity()+"/last-run.log";
        QFile f(log);
        if(f.open(QIODevice::ReadOnly)) {
          const qint64 pos=qMax<qint64>(0,f.size()-3000);
          f.seek(pos);
          errorText=QString::fromLocal8Bit(f.readAll());
        }
        errorText=QString("Emulator exited with code %1.\n\n").arg(code)+
          (errorText.isEmpty()?"No output captured.":errorText)+
          "\n\nLog: "+log;
      }
      chrome();
    });
    scan();
  }
  ~J2ME() override {
    if(child.state()!=QProcess::NotRunning){
      child.terminate();if(!child.waitForFinished(1500)){child.kill();child.waitForFinished(1500);}
    }
  }
};
int main(int argc,char **argv){
  QApplication app(argc,argv);
  app.setApplicationName("reread-j2me");
  app.setDesktopFileName("reread-j2me");
  app.setFont(QFont("DejaVu Sans"));
  J2ME screen;screen.show();return app.exec();
}

#include "reader-ui.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
using namespace ReRead;
class J2ME : public Screen {
  QStringList games;
  int selected=0, field=0;
  bool options=false, running=false;
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
    QFile f(profilePath());
    if(f.open(QIODevice::WriteOnly|QIODevice::Truncate))
      f.write(QJsonDocument(profile).toJson(QJsonDocument::Indented));
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
    selected=games.indexOf(keep);
    if(selected<0)selected=0;
    load();chrome();
  }
  void chrome() {
    showHeader=true;
    title=options?"J2ME game settings":"Java ME";
    subtitle=options?QFileInfo(game()).fileName():QString::number(games.size())+" games in Files/J2ME";
    controls=running?QList<Control>{{READER_FAVORITES,"Stop"}}:
      options?QList<Control>{{READER_UP,"Setting"},{READER_LEFT,"Change"},{READER_BACK,"Games"},{READER_OK,"Change"}}:
      QList<Control>{{READER_UP,"Select"},{READER_OK,"Play"},{READER_OPTIONS,"Settings"},{READER_BACK,"Home"}};
    update();
  }
  void change(int amount) {
    if(game().isEmpty())return;
    static const char* keys[]={"orientation","width","height","fps","layout"};
    const char *k=keys[field];
    if(field==0)profile[k]=(setting(k,0)+2+amount%2)%2;
    if(field==1)profile[k]=qBound(128,setting(k,240)+amount*16,480);
    if(field==2)profile[k]=qBound(128,setting(k,320)+amount*16,480);
    if(field==3)profile[k]=qBound(10,setting(k,60)+amount*5,60);
    if(field==4)profile[k]=(setting(k,0)+11+amount%11)%11;
    save();update();
  }
  void start() {
    if(game().isEmpty()||running)return;
    QString runtime="/usr/lib/reread/apps/j2me/freej2me_plus.jar";
    if(!QFileInfo::exists(runtime)){message="FreeJ2ME-Plus is not installed.";update();return;}
    const int land=setting("orientation",0),w=setting("width",240),h=setting("height",320);
    QDir().mkpath(state+"/data/"+identity());
    child.setWorkingDirectory(state+"/data/"+identity());
    child.setProgram("/usr/bin/java");
    child.setArguments({"-Xms16m","-Xmx128m",
        "-Dreread.freej2me.jar="+runtime,
        "-cp","/usr/lib/reread/apps/j2me/classes:"+runtime,
        "ReReadJ2ME",
        QUrl::fromLocalFile(game()).toString(QUrl::FullyEncoded),
        "1",QString::number(land?h:w),QString::number(land?w:h),
        "1",QString::number(setting("layout",0)),QString::number(setting("fps",60))});
    message="Starting "+QFileInfo(game()).fileName();
    running=true;child.start();chrome();
  }
protected:
  void button(reader_button id,int value) override {
    if(value!=1 && !(value==2 && (id==READER_UP||id==READER_DOWN||id==READER_LEFT||id==READER_RIGHT)))return;
    if(running) {
      if(id==READER_FAVORITES){child.terminate();if(!child.waitForFinished(1500))child.kill();}
      return;
    }
    if(id==READER_FAVORITES){close();return;}
    if(options) {
      if(id==READER_BACK||id==READER_OPTIONS){options=false;chrome();return;}
      if(id==READER_UP||id==READER_DOWN){field=qBound(0,field+(id==READER_DOWN?1:-1),4);update();return;}
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
    if(running){text(p,r,"Running "+QFileInfo(game()).fileName()+"\n\n"
                    "The Java ME game is in a separate window.\n"
                    "Press Favorites to stop it.",25);return;}
    if(options){
      QStringList labels={"Orientation","Virtual width","Virtual height","Frame cap","Phone layout"};
      QStringList detail={setting("orientation",0)?"Landscape":"Portrait",
          QString::number(setting("width",240))+" px",QString::number(setting("height",320))+" px",
          QString::number(setting("fps",60))+" FPS",
          QString::number(setting("layout",0))+" (0=default)"};
      list(p,r,labels,detail,field);
    }else if(games.isEmpty()){
      text(p,r,"No Java ME games found.\n\nUpload .jar games to Files/J2ME.\n"
          "Return here to browse your games.",26);
    }else {
      QStringList names,descriptions;
      for(const QString &g:games){
        names<<QFileInfo(g).completeBaseName();
        descriptions<<QString::number(QFileInfo(g).size()/1024)+" KiB";
      }
      list(p,r,names,descriptions,selected);
    }
    if(!message.isEmpty())label(p,QRect(r.x(),r.bottom()-32,r.width(),30),message,18);
  }
public:
  J2ME():Screen("Java ME") {
    connect(&child,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){
      if(e==QProcess::FailedToStart){running=false;message="Java could not start.";chrome();}
    });
    connect(&child,static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
            this,[this](int code,QProcess::ExitStatus){
      running=false;message=code==0?"Game closed.":QString("Game exited (%1).").arg(code);chrome();
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

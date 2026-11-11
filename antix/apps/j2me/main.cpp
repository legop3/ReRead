#include "reader-ui.hpp"
#include "core.hpp"
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
#include <QTransform>
#include <QSet>
using namespace ReRead;
class J2ME : public Screen {
  QStringList games;
  QMap<QString,QJsonObject> metadata;
  QFileSystemWatcher watcher;
  int selected=0, field=0;
  bool options=false, running=false, confirmDelete=false, errorPage=false, gameMenu=false;
  int gameAction=0;
  QSet<int> heldKeys;
  QString errorText;
  QString message;
  JavaMECore core;
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
    showHeader=!running;
    title=errorPage?"Game failed":confirmDelete?"Erase game data?":options?"Game settings":"Java ME";
    subtitle=(options||confirmDelete||errorPage)?metadata.value(game()).value("title").toString(QFileInfo(game()).fileName()):QString::number(games.size())+" games in Files/J2ME";
    controls=running?(gameMenu?QList<Control>{{READER_UP,"Select"},{READER_OK,"Choose"},{READER_BACK,"Resume"},{READER_FAVORITES,"Quit"}}:
      QList<Control>{{READER_UP,"Move"},{READER_OK,"Fire"},{READER_OPTIONS,"Left softkey"},{READER_BACK,"Right softkey"},{READER_ZOOM_IN,"*"},{READER_ZOOM_OUT,"#"},{READER_VIEW_TOGGLE,"Menu"},{READER_FAVORITES,"Quit"}}):
      errorPage?QList<Control>{{READER_BACK,"Games"}}:
      confirmDelete?QList<Control>{{READER_OK,"Erase"},{READER_BACK,"Cancel"}}:
      options?QList<Control>{{READER_UP,"Setting"},{READER_LEFT,"Change"},{READER_BACK,"Games"},{READER_OK,"Change"}}:
      QList<Control>{{READER_UP,"Select"},{READER_OK,"Play"},{READER_OPTIONS,"Settings"},{READER_BACK,"Home"}};
    update();
  }
  void change(int amount) {
    if(game().isEmpty())return;
    if(field==5) {
      profile=QJsonObject{};save();message="Game settings reset.";update();return;
    }
    if(field==6) {confirmDelete=true;chrome();return;}
    static const char* keys[]={"orientation","width","height","fps","layout"};
    const char *k=keys[field];
    if(field==0)profile[k]=(setting(k,0)+4+amount%4)%4;
    if(field==1)profile[k]=qBound(128,setting(k,240)+amount*16,480);
    if(field==2)profile[k]=qBound(128,setting(k,320)+amount*16,480);
    if(field==3)profile[k]=qBound(10,setting(k,60)+amount*5,60);
    if(field==4)profile[k]=(setting(k,0)+11+amount%11)%11;
    save();update();
  }
  void start() {
    if(game().isEmpty()||running)return;
    const QString emulator="/usr/lib/reread/apps/j2me/freej2me_plus-lr.jar";
    if(!QFileInfo::exists(emulator)){message="Java ME core is missing.";chrome();return;}
    const QString data=state+"/data/"+identity()+"/";
    QDir().mkpath(data);
    const int w=setting("width",240),h=setting("height",320);
    const int angle=setting("orientation",0);
    const int layout=setting("layout",0),fps=setting("fps",60);
    profile["plays"]=setting("plays",0)+1;
    profile["lastPlayed"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    save();
    errorPage=false;errorText.clear();gameMenu=false;gameAction=0;heldKeys.clear();
    message="Starting "+QFileInfo(game()).fileName();
    running=true;
    // No AWT window. Java ME frames are rendered by this ReRead Screen.
    core.launch(QUrl::fromLocalFile(game()).toString(QUrl::FullyEncoded),
                data,emulator,w,h,angle,layout,fps);
    chrome();
  }
  void finishGame() {
    heldKeys.clear();
    core.stop();running=false;gameMenu=false;
    message="Returned to Java ME library.";
    chrome();
  }
  static int phoneKey(reader_button id) {
    switch(id) {
      case READER_UP:return 2;
      case READER_LEFT:return 3;
      case READER_OK:return 4;
      case READER_RIGHT:return 5;
      case READER_DOWN:return 6;
      case READER_OPTIONS:return 0;
      case READER_BACK:return 1;
      case READER_ZOOM_IN:return 16;
      case READER_ZOOM_OUT:return 18;
      default:return -1;
    }
  }
protected:
  void button(reader_button id,int value) override {
    if(running) {
      if(gameMenu) {
        if(value!=1)return;
        if(id==READER_FAVORITES){finishGame();return;}
        if(id==READER_BACK||id==READER_VIEW_TOGGLE){gameMenu=false;core.pause(false);chrome();return;}
        if(id==READER_UP||id==READER_DOWN){
          gameAction=qBound(0,gameAction+(id==READER_DOWN?1:-1),1);
          update();return;
        }
        if(id==READER_OK){
          if(gameAction==0){gameMenu=false;core.pause(false);chrome();}
          else finishGame();
        }
        return;
      }
      if(id==READER_FAVORITES && value==1){finishGame();return;}
      if(id==READER_VIEW_TOGGLE && value==1){
        for(int key:heldKeys)core.key(key,false);
        heldKeys.clear();
        gameMenu=true;gameAction=0;core.pause(true);chrome();return;
      }
      if(value==2)return; // The emulator produces its own key repeats per frame.
      const int key=phoneKey(id);
      if(key>=0){
        if(value==1){heldKeys.insert(key);core.key(key,true);}
        else if(heldKeys.remove(key))core.key(key,false);
      }
      return;
    }
    if(value!=1 && !(value==2 && (id==READER_UP||id==READER_DOWN||id==READER_LEFT||id==READER_RIGHT)))return;
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
      if(id==READER_UP||id==READER_DOWN){field=qBound(0,field+(id==READER_DOWN?1:-1),6);update();return;}
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
    if(running) {
      p.fillRect(r,Qt::black);
      if(core.hasFrame()) {
        QImage frame=core.frame();
        const int angle=core.frameRotation()*90;
        if(angle) {
          QTransform transform;transform.rotate(angle);
          frame=frame.transformed(transform,Qt::FastTransformation);
        }
        const QSize scaled=frame.size().scaled(r.size(),Qt::KeepAspectRatio);
        const QRect dest(r.x()+(r.width()-scaled.width())/2,
                         r.y()+(r.height()-scaled.height())/2,scaled.width(),scaled.height());
        p.setRenderHint(QPainter::SmoothPixmapTransform,false);
        p.drawImage(dest,frame);
      } else {
        text(p,safeArea(),message,25);
      }
      if(gameMenu) {
        p.fillRect(r,QColor(0,0,0,235));
        const QRect menu=safeArea().adjusted(70,40,-70,-40);
        label(p,QRect(menu.x(),menu.y(),menu.width(),50),"Game menu",32,true);
        list(p,menu.adjusted(0,65,0,0),{"Resume","Exit to Java ME"},
             {"Continue playing","Close the game; keep its saves"},gameAction);
      }
      return;
    }
    if(errorPage) {
      text(p,r,errorText,20);
    } else if(confirmDelete) {
      text(p,r,"Erase the saved progress and emulator data for this game?\n\n"
          "The JAR file and game settings will be kept.\n\n"
          "Press OK to ERASE or Back to cancel.",26);
    } else if(options){
      QStringList labels={"Rotation","Virtual width","Virtual height","Frame cap","Phone layout","Reset settings","Erase saved data…"};
      const QStringList layouts={"Standard","LG","Motorola","Moto Triplets","Moto V8","Moto A1000","Nokia","Sagem","Siemens","SKT","KDDI"};
      QStringList detail={QString::number(setting("orientation",0)*90)+QString::fromUtf8("°"),
          QString::number(setting("width",240))+" px",QString::number(setting("height",320))+" px",
          QString::number(setting("fps",60))+" FPS",
          layouts.value(setting("layout",0),"Standard"),
          "Use standard defaults","Keep JAR; clear game progress"};
      list(p,r,labels,detail,field);
    }else if(games.isEmpty()){
      text(p,r,"No Java ME games found.\n\nUpload .jar games to Files/J2ME.\n"
          "Return here to browse your games.",26);
    }else {
      // The standard ReRead collection/details layout; no custom grid.
      const auto layout=listInfo(p,r,games.size(),selected,62,0.62);
      for(int i=layout.first;i<qMin(layout.first+layout.visible,games.size());++i){
        const QRect row=listInfoRow(p,layout,i,selected);
        const QString path=games.at(i);
        const auto m=metadata.value(path);
        const bool current=i==selected;
        const auto color=palette().color(current?QPalette::HighlightedText:QPalette::Text);
        label(p,row.adjusted(13,0,-14,0),
              m.value("title").toString(QFileInfo(path).completeBaseName()),
              25,current,color);
      }
      const QString path=game();
      const auto m=metadata.value(path);
      const QRect side=layout.info.adjusted(5,0,-5,0);
      label(p,QRect(side.x(),side.y(),side.width(),52),
            m.value("title").toString(QFileInfo(path).completeBaseName()),28,true);
      QString details;
      const QString vendor=m.value("vendor").toString();
      const QString version=m.value("version").toString();
      if(!vendor.isEmpty())details+="By "+vendor+"\n";
      if(!version.isEmpty())details+="Version "+version+"\n";
      details+="JAR · "+QString::number(QFileInfo(path).size()/1024)+" KiB";
      details+="\n\n"+QString::number(setting("width",240))+
        " × "+QString::number(setting("height",320))+
        " · "+QString::number(setting("orientation",0)*90)+"°";
      details+="\n\nPlays: "+QString::number(setting("plays",0));
      const QString last=profile.value("lastPlayed").toString();
      if(!last.isEmpty())details+="\nLast played: "+last.left(10);
      details+="\n\nOK: Play\nOptions: Game settings";
      text(p,side.adjusted(0,62,0,0),details,21);

    }
    if(!message.isEmpty())label(p,QRect(r.x(),r.bottom()-32,r.width(),30),message,18);
  }
public:
  J2ME():Screen("Java ME"),core(this) {
    core.onFrame=[this]{if(running&&!gameMenu)update();};
    core.onFailure=[this](const QString &what) {
      if(!running)return;
      message.clear();
      errorText="Java ME failed:\n"+what+"\n\n"+core.errors();
      errorPage=true;running=false;gameMenu=false;core.stop();chrome();
    };
    core.onExited=[this](int code){
      if(!running)return;
      running=false;gameMenu=false;
      if(code==0 && core.frameCount()>0) {
        message="Game finished.";chrome();return;
      }
      message.clear();errorPage=true;
      errorText=QString("Java ME core exited (%1).\n\n").arg(code)+core.errors();
      chrome();
    };
    connect(&watcher,&QFileSystemWatcher::directoryChanged,this,
            [this]{if(!running&&!options)scan();});
    scan();
  }
  ~J2ME() override {core.stop();}

};
int main(int argc,char **argv){
  QApplication app(argc,argv);
  app.setApplicationName("reread-j2me");
  app.setDesktopFileName("reread-j2me");
  app.setFont(QFont("DejaVu Sans"));
  J2ME screen;screen.show();return app.exec();
}

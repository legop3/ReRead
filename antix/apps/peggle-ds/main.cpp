#include "reader-ui.hpp"
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFile>
#include <QTextStream>
using namespace ReRead;

class PeggleDS : public Screen {
  QStringList roms;
  int selected=0;
  QProcess emulator;
  QString status;
  bool running=false;
  QString diagnostic;
  QString logPath="/var/lib/reader/apps/peggle-ds/emulator.log";

  void appendOutput(const QByteArray &bytes) {
    if(bytes.isEmpty()) return;
    QFile f(logPath);
    if(f.open(QIODevice::WriteOnly|QIODevice::Append)) f.write(bytes);
    diagnostic += QString::fromLocal8Bit(bytes);
    if(diagnostic.size()>3000) diagnostic=diagnostic.right(3000);
  }


  void scan() {
    roms.clear();
    QDirIterator it("/var/lib/reader/files", QStringList()<<"*.nds"<<"*.NDS",
                    QDir::Files,QDirIterator::Subdirectories);
    while(it.hasNext() && roms.size()<100) roms<<it.next();
    roms.sort(Qt::CaseInsensitive);
    selected=qBound(0,selected,qMax(0,roms.size()-1));
    update();
  }
  void launch() {
    if(running || roms.isEmpty()) return;
    running=true;
    status="Starting Nintendo DS emulator...";
    diagnostic.clear();
    QDir().mkpath("/var/lib/reader/apps/peggle-ds");
    QFile::remove(logPath);
    auto env=QProcessEnvironment::systemEnvironment();
    env.insert("SDL_VIDEODRIVER","x11");
    env.insert("SDL_RENDER_DRIVER","software");
    env.insert("SDL_AUDIODRIVER","alsa");
    env.insert("LD_PRELOAD","/usr/lib/reread/apps/peggle-ds/sdl-reader.so");
    emulator.setProcessEnvironment(env);
    emulator.setWorkingDirectory("/var/lib/reader/apps/peggle-ds");
    emulator.setProgram("/usr/games/desmume-cli");
    emulator.setArguments({"--horizontal","--scale=1","--3d-engine=1",roms[selected]});
    emulator.start();
    update();
  }
protected:
  void button(reader_button id,int value) override {
    if(value!=1 || running)return;
    if(id==READER_BACK||id==READER_FAVORITES)close();
    else if(id==READER_OPTIONS) {status.clear();scan();}
    else if(id==READER_OK) launch();
    else if(id==READER_UP||id==READER_DOWN) {
      selected=qBound(0,selected+(id==READER_DOWN?1:-1),qMax(0,roms.size()-1));
      update();
    }
  }
  void draw(QPainter &p,const QRect &area) override {
    label(p,QRect(area.x(),area.y(),area.width(),45),
          "Peggle: Dual Shot",30,true,palette().color(QPalette::Link));
    const QRect body=area.adjusted(0,55,0,-60);
    QString info;
    if(running) {
      info="Nintendo DS emulator running.\n\n"
           "Favorites closes the emulator.\n"
           "Return to this screen when it exits.";
    } else if(roms.isEmpty()) {
      info="Upload your Peggle: Dual Shot .nds file to ReRead Files.\n\n"
           "Press Options to rescan.\n\n"
           "No Nintendo DS game files are included with this app.";
    } else {
      info=QString("Ready to play:\n\n%1\n\n"
                   "Press OK to launch.\n\n"
                   "D-pad: aim  |  OK: A / shoot\n"
                   "Zoom - / +: L / R  |  Options: Start\n"
                   "Back: B  |  Favorites: exit")
                .arg(QFileInfo(roms[selected]).fileName());
      if(roms.size()>1)info+=QString("\n\nGame %1 of %2").arg(selected+1).arg(roms.size());
    }
    if(!running && !diagnostic.isEmpty()) {
      info += "\n\nLast emulator output:\n" + diagnostic.right(1000);
    }
    text(p,body,info,roms.isEmpty()?23:20);
    if(!status.isEmpty())
      text(p,QRect(area.x(),area.bottom()-55,area.width(),55),status,20);
  }
public:
  PeggleDS(): Screen("Peggle DS") {
    setWindowTitle("Peggle DS");
    showHeader=false;
    controls={{READER_UP,"Select ROM"},{READER_OK,"Play"},
              {READER_OPTIONS,"Rescan"},{READER_BACK,"Home"}};
    connect(&emulator,&QProcess::readyReadStandardError,this,[this]{
      appendOutput(emulator.readAllStandardError());
    });
    connect(&emulator,&QProcess::readyReadStandardOutput,this,[this]{
      appendOutput(emulator.readAllStandardOutput());
    });
    connect(&emulator,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){
      if(e==QProcess::FailedToStart) {
        running=false;status="Emulator could not start. Check that desmume is installed.";
        update();
      }
    });
    connect(&emulator,static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
            this,[this](int code,QProcess::ExitStatus exit){
      appendOutput(emulator.readAllStandardError());
      appendOutput(emulator.readAllStandardOutput());
      running=false;
      status=(code==0&&exit==QProcess::NormalExit)?"Game closed.":
          QString("Emulator exited (code %1). See error below or emulator.log.").arg(code);
      scan();
    });
    scan();
  }
  ~PeggleDS() override {
    if(emulator.state()!=QProcess::NotRunning) {
      emulator.terminate();
      if(!emulator.waitForFinished(2500)) {
        emulator.kill();emulator.waitForFinished(2500);
      }
    }
  }
};
int main(int argc,char**argv){
  QApplication app(argc,argv);
  app.setApplicationName("reread-peggle-ds");
  app.setDesktopFileName("reread-peggle-ds");
  app.setFont(QFont("DejaVu Sans"));
  PeggleDS screen;screen.show();
  return app.exec();
}

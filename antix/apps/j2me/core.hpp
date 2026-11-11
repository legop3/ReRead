#pragma once
#include <QByteArray>
#include <QImage>
#include <QProcess>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QFile>
#include <functional>

// FreeJ2ME-Plus already provides a headless binary libretro protocol.
// The Java worker owns MIDP, audio and emulation; ReRead owns the *only*
// visible window, framebuffer presentation, buttons, Guide and lifecycle.
class JavaMECore final : public QObject {
  QProcess process;
  QTimer frameClock;
  QTimer startupWatch;
  QByteArray recentErrors;
  QString stderrPath;
  QByteArray pending;
  bool ready=false, awaitingFrame=false, stopping=false;
  QImage image;
  int rotation=0;
  QString jarPath, savePath, javaJar;
  int width=240, height=320, phone=0, fps=60;
  int frames=0;
  int encodingIndex=0, restartCount=0;
  void packet(int operation, quint32 size) {
    if(process.state()!=QProcess::Running)return;
    QByteArray command;
    command.reserve(5);
    command.append(char(operation));
    command.append(char((size>>24)&255));
    command.append(char((size>>16)&255));
    command.append(char((size>>8)&255));
    command.append(char(size&255));
    process.write(command);
  }
  void command(int operation, const QByteArray &payload={}) {
    if(process.state()!=QProcess::Running) return;
    const quint32 size=quint32(payload.size());
    QByteArray command;
    command.reserve(5+payload.size());
    command.append(char(operation));
    command.append(char((size>>24)&255));
    command.append(char((size>>16)&255));
    command.append(char((size>>8)&255));
    command.append(char(size&255));
    command.append(payload);
    process.write(command);
  }
  void configure() {
    // Libretro.java expects 42 pipe-delimited option tokens, positions 0..41.
    QStringList cfg;
    for(int i=0;i<42;++i)cfg<<"0";
    cfg[0]="FJ2ME_LR_OPTS:";
    cfg[1]=QString::number(width);cfg[2]=QString::number(height);
    cfg[3]=QString::number(rotation);cfg[4]=QString::number(phone);
    cfg[5]=QString::number(fps);
    cfg[6]="1"; // Sound enabled; output uses the Reader's ALSA mixer
    cfg[9]="2"; // Log level
    cfg[22]="1"; // Override platform checks
    cfg[25]="200"; // DoJa default
    cfg[36]="7";cfg[37]="1";cfg[41]="4"; // renderer and synth defaults
    command(12,cfg.join("|").toUtf8());
  }
  void readData() {
    pending+=process.readAllStandardOutput();
    if(!ready) {
      const int end=pending.indexOf("+READY\n");
      if(end<0) {
        if(pending.size()>4096) {fail("Java ME core returned an invalid startup response.");stop();}
        return;
      }
      pending.remove(0,end+7);
      ready=true;
      command(11,savePath.toUtf8());
      command(10,jarPath.toUtf8());
      configure();
      command(13,{});
      frameClock.start(qMax(16,1000/fps));
    }
    while(ready && pending.size()>=16) {
      if(quint8(pending[0])!=0xFE) {
        // A game may log text on stdout. Keep the binary protocol in sync.
        const int marker=pending.indexOf(char(0xFE));
        if(marker<0){if(pending.size()>32768)pending.clear();break;}
        pending.remove(0,marker);
        if(pending.size()<16)break;
      }
      const int w=(quint8(pending[1])<<8)|quint8(pending[2]);
      const int h=(quint8(pending[3])<<8)|quint8(pending[4]);
      if(w<1||h<1||w>854||h>854){fail("Java ME core sent an invalid frame size.");stop();return;}
      const qsizetype bytes=qsizetype(w)*h*3;
      if(pending.size()<16+bytes)break;
      if(quint8(pending[14])==1) {
        const int requested=int(quint8(pending[15]));
        if(requested==encodingIndex || requested>2 || restartCount>=2) {
          fail("Java ME requested an unsupported encoding restart.");
          stop();return;
        }
        ++restartCount;
        encodingIndex=requested;
        restartJava();
        return;
      }
      rotation=quint8(pending[5])%4;
      const auto *pixels=reinterpret_cast<const uchar*>(pending.constData()+16);
      QImage source(pixels,w,h,w*3,QImage::Format_RGB888);
      image=source.copy(); // Must own buffer after removing pending bytes.
      pending.remove(0,16+int(bytes));
      awaitingFrame=false;
      ++frames;
      if(frames==1)startupWatch.stop();
      if(onFrame)onFrame();
      if(quint8(pending.isEmpty()?char(0):pending[0])==0xFE)continue;
    }
  }
public:
  std::function<void()> onFrame;
  std::function<void(const QString &)> onFailure;
  std::function<void(int)> onExited;
  explicit JavaMECore(QObject *parent=nullptr):QObject(parent) {
    process.setProcessChannelMode(QProcess::SeparateChannels);
    frameClock.setTimerType(Qt::PreciseTimer);
    startupWatch.setSingleShot(true);
    connect(&startupWatch,&QTimer::timeout,this,[this]{
      if(process.state()==QProcess::Running && frames==0){
        fail("Java ME did not produce a frame. "+QString::fromLocal8Bit(recentErrors));
        stop();
      }
    });
    connect(&process,&QProcess::readyReadStandardError,this,[this]{
      const QByteArray chunk=process.readAllStandardError();
      recentErrors+=chunk;
      if(recentErrors.size()>16384)recentErrors=recentErrors.right(16384);
      QFile log(stderrPath);
      if(log.open(QIODevice::WriteOnly|QIODevice::Append))log.write(chunk);
    });
    connect(&frameClock,&QTimer::timeout,this,[this]{
      if(!ready || awaitingFrame)return;
      awaitingFrame=true;
      command(15,{});
    });
    connect(&process,&QProcess::readyReadStandardOutput,this,[this]{readData();});
    connect(&process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error) {
      if(error==QProcess::FailedToStart && onFailure)onFailure(process.errorString());
    });
    connect(&process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
        this,[this](int code,QProcess::ExitStatus){
          frameClock.stop();startupWatch.stop();ready=false;awaitingFrame=false;
          const bool wasStopped=stopping;
          stopping=false;
          if(!wasStopped && onExited)onExited(code);
        });
  }
  bool running()const{return process.state()!=QProcess::NotRunning;}
  bool hasFrame()const{return !image.isNull();}
  const QImage &frame()const{return image;}
  int frameRotation()const{return rotation;}
  int frameCount()const{return frames;}
  QString errors() const {return QString::fromLocal8Bit(recentErrors);}
  void launch(const QString &jar, const QString &storage, const QString &emulator,
              int w,int h,int angle,int layout,int cap) {
    stop();
    jarPath=jar;savePath=storage;javaJar=emulator;
    width=w;height=h;rotation=angle;phone=layout;fps=qBound(10,cap,60);
    image=QImage();pending.clear();frames=0;ready=false;awaitingFrame=false;
    encodingIndex=0;restartCount=0;
    QDir().mkpath(storage);
    recentErrors.clear();stderrPath=storage+"last-run.log";
    QFile log(stderrPath);if(log.open(QIODevice::WriteOnly|QIODevice::Truncate))log.close();
    restartJava();
  }
  void key(int index,bool press) {
    if(!ready || index<0 || index>23)return;
    packet(press?3:2,quint32(index));
  }
  void pause(bool value) {
    if(value){frameClock.stop();}
    else if(ready)frameClock.start(qMax(16,1000/fps));
  }
  void stop() {
    stopping=true;frameClock.stop();startupWatch.stop();
    if(process.state()!=QProcess::NotRunning){
      process.closeWriteChannel();
      process.terminate();
      if(!process.waitForFinished(1000)){
        process.kill();
        process.waitForFinished(1000);
      }
    }
    ready=false;awaitingFrame=false;pending.clear();
    stopping=false;
  }
  ~JavaMECore() override {stop();}
private:
  void restartJava() {
    stop();
    static const QStringList encodings={"ISO_8859_1","Shift_JIS","EUC_KR"};
    process.setWorkingDirectory(savePath);
    process.setProgram("/usr/bin/java");
    process.setArguments({"-Xms16m","-Xmx128m",
      "-Dfile.encoding="+encodings.value(encodingIndex,"ISO_8859_1"),
      "-jar",javaJar,QString::number(width),QString::number(height)});
    process.start();
    startupWatch.start(30000);
  }
  void fail(const QString &why){if(onFailure)onFailure(why);}
};

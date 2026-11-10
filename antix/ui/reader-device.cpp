#include "reader-device.hpp"
#include <QtDBus>
namespace ReRead {
QJsonObject readJson(const QString &path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly)
             ? QJsonDocument::fromJson(f.readAll()).object()
             : QJsonObject();
}
void writeJson(const QString &path, const QJsonObject &value) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(value).toJson(QJsonDocument::Compact));
    f.commit();
  }
}
static void runCommand(const QString &program, const QStringList &args,
                       std::function<void(QByteArray, int)> done, QObject *owner) {
  auto p = new QProcess(owner ? owner : QCoreApplication::instance());
  QObject::connect(
      p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), owner ? owner : p,
      [p, done](int code, QProcess::ExitStatus) {
        auto out = p->readAllStandardOutput();
        auto error = p->readAllStandardError();
        if (code && !error.isEmpty())
          qWarning() << p->program() << error;
        if (done)
          done(out, code);
        p->deleteLater();
      });
  QObject::connect(p, &QProcess::errorOccurred, owner ? owner : p,
                   [p, done](QProcess::ProcessError e) {
                     if (e == QProcess::FailedToStart) {
                       if (done)
                         done({}, -1);
                       p->deleteLater();
                     }
                   });
  p->start(program, args);
}
void command(const QString &program, const QStringList &args,
             std::function<void(QByteArray)> done, QObject *owner) {
  runCommand(program, args, [done](QByteArray out, int) { if (done) done(out); }, owner);
}
// The foreground adapter handles navigation only. Linux services own system
// keys, idle policy and the backlight; apps observe state and login1 lifecycle.
struct Device::State {
  Device &owner;
  QFileSystemWatcher status;
  bool active=false, locked=false, dimmed=false, sleeping=false;
  int noticeSequence=-1;
  std::function<void(reader_button,int)> buttonHandler;
  std::function<void(bool)> idleHandler,holdHandler;
  std::function<void(const QString &)> noticeHandler;
  std::function<void()> resumeHandler;
  explicit State(Device &device):owner(device) {
    status.addPath("/run/reader-session.json");
    QObject::connect(&status,&QFileSystemWatcher::fileChanged,&owner,[this]{
      status.addPath("/run/reader-session.json");readStatus();
    });
    readStatus();
    QDBusConnection::systemBus().connect("org.freedesktop.login1","/org/freedesktop/login1",
      "org.freedesktop.login1.Manager","PrepareForSleep",&owner,SLOT(sleepChanged(bool)));
  }
  void readStatus() {
    auto data=readJson("/run/reader-session.json");
    bool nextLocked=data.value("locked").toBool(),nextDimmed=data.value("dimmed").toBool();
    if(nextLocked!=locked) {locked=nextLocked;if(active&&holdHandler)holdHandler(locked);}
    if(nextDimmed!=dimmed) {dimmed=nextDimmed;if(active&&!sleeping&&idleHandler)idleHandler(dimmed);}
    int seq=data.value("notice_sequence").toInt();
    if(active&&noticeSequence>=0&&seq!=noticeSequence&&noticeHandler)
      noticeHandler(data.value("volume").toString());
    noticeSequence=seq;
  }
  void dispatch(reader_button id,int value){
    if(!active||id==READER_NONE)return;
    if(id==READER_POWER||id==READER_VOLUME_UP||id==READER_VOLUME_DOWN)return;
    if(id==READER_HOLD)return; // The session owns Hold; X11 may synthesize releases on wake.
    if(!locked&&buttonHandler)buttonHandler(id,value);
  }
};
Device::Device(QObject *parent):QObject(parent),state(new State(*this)){}
Device::~Device()=default;
void Device::setActive(bool value){
  if(state->active==value)return;
  state->active=value;
  if(value){state->readStatus();
    if(state->idleHandler)state->idleHandler(state->dimmed||state->sleeping);
  }else if(state->idleHandler)state->idleHandler(true);
}
bool Device::isActive()const{return state->active;}
bool Device::isLocked()const{return state->locked;}
void Device::activity(){
  if(state->active){QFile marker("/run/reader-activity");if(marker.open(QIODevice::WriteOnly))marker.write("1");}
}
void Device::dispatch(reader_button id,int value){state->dispatch(id,value);}
void Device::onButton(std::function<void(reader_button,int)> h){state->buttonHandler=std::move(h);}
void Device::onIdle(std::function<void(bool)> h){state->idleHandler=std::move(h);}
void Device::onHold(std::function<void(bool)> h){state->holdHandler=std::move(h);}
void Device::onNotice(std::function<void(const QString &)> h){state->noticeHandler=std::move(h);}
void Device::onResume(std::function<void()> h){state->resumeHandler=std::move(h);}
void Device::sleepChanged(bool value){
  state->sleeping=value;
  if(!value)state->readStatus();
  if(state->active){
    if(state->idleHandler)state->idleHandler(value||state->dimmed);
    if(!value&&state->resumeHandler)state->resumeHandler();
  }
}
} // namespace ReRead

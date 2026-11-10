// Framebuffer-session integration: evdev activity -> console activity, Linux
// backlight dimming and UI status. Elogind owns suspend policy and inhibitors.
#include "reader-device.hpp"
#include <QtDBus>
#include <linux/input.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
using namespace ReRead;
static const QString statusPath = "/run/reader-session.json";
class HomeProcess : public QProcess {
  pid_t parentPid = getpid();
protected:
  void setupChildProcess() override {
    sigset_t empty; sigemptyset(&empty); sigprocmask(SIG_SETMASK,&empty,nullptr);
    if(prctl(PR_SET_PDEATHSIG,SIGTERM)!=0 || getppid()!=parentPid) _exit(127);
  }
};
class Session : public QObject {
  Q_OBJECT
  struct Input { int fd; QSocketNotifier *notifier; };
  QList<Input> inputs;
  QTimer discovery, dim;
  HomeProcess home;
  QDBusInterface manager;
  QString sessionPath, policy;
  QDBusUnixFileDescriptor externalPowerInhibitor;
  bool dimmed = false, locked = false, sleeping = false;
  QString volume;
  int noticeSequence = 0;
  int tty = -1, oldKeyboardMode = -1;
  void publish() {
    writeJson(statusPath, {{"dimmed",dimmed},{"locked",locked},
                          {"volume",volume},{"notice_sequence",noticeSequence}});
  }
  void brightness(int percent) {
    QFile level("/sys/class/backlight/reader-panel/brightness");
    if (level.open(QIODevice::WriteOnly)) level.write(QByteArray::number(qBound(10,percent,100)) + "\n");
  }
  void activity() {
    if (sleeping) return;
    // elogind uses tty atime for console sessions. evdev bypasses tty input
    // while K_OFF is active, so record that activity here, as a tty read would.
    const timespec times[2] = {{0,UTIME_NOW},{0,UTIME_OMIT}};
    if (tty >= 0 && futimens(tty,times) != 0)
      qWarning("Cannot update console activity: %s",strerror(errno));
    if (dimmed) {
      dimmed = false;
      brightness(readJson("/etc/reader/display.json").value("brightness_percent").toInt(100));
      publish();
    }
    int seconds = readJson("/etc/reader/display.json").value("idle_dim_seconds").toInt(120);
    if (seconds > 0) dim.start(qMin(seconds,86400)*1000); else dim.stop();
  }
  void updatePowerInhibitor() {
    auto config = readJson("/etc/reader/display.json");
    bool block = config.value("auto_suspend_battery_only").toBool(false) &&
                 config.value("auto_suspend_seconds").toInt(0) > 0 &&
                 readJson("/run/reader-battery.json").value("external_power").toBool(false);
    if (block && !externalPowerInhibitor.isValid()) {
      // An idle-only lock leaves the Power button and explicit sleep available.
      QDBusReply<QDBusUnixFileDescriptor> reply = manager.call("Inhibit",
          "idle", "ReRead", "Automatic sleep is battery-only", "block");
      if (reply.isValid()) externalPowerInhibitor = reply.value();
      else qWarning() << "Cannot inhibit automatic sleep:" << reply.error().message();
    } else if (!block && externalPowerInhibitor.isValid()) {
      activity(); // Start a fresh idle interval when unplugged.
      externalPowerInhibitor = QDBusUnixFileDescriptor();
    }
  }
  void configure() {
    if (sleeping) return;
    // Clear a stale tty idle timestamp before enabling/changing idle actions.
    activity();
    int seconds = qBound(0, readJson("/etc/reader/display.json")
                               .value("auto_suspend_seconds").toInt(0),86400);
    QString next = "[Login]\nHandlePowerKey=suspend\nHandlePowerKeyLongPress=ignore\n"
                   "HandleLidSwitch=ignore\nKillUserProcesses=no\nIdleAction=" +
                   QString(seconds ? "suspend" : "ignore") + "\nIdleActionSec=" +
                   QString::number(qMax(1,seconds)) + "s\n";
    if (next != policy) {
      QFile current("/etc/elogind/logind.conf.d/60-reread.conf");
      bool changed = !current.open(QIODevice::ReadOnly) || current.readAll() != next.toUtf8();
      if (changed) {
        QSaveFile file(current.fileName());
        if (!file.open(QIODevice::WriteOnly) || file.write(next.toUtf8()) < 0 || !file.commit())
          qFatal("Cannot update elogind policy");
        // elogind 255 ReloadConfig does not arm/re-arm the idle timer. A normal
        // service restart applies it; elogind restores sessions/inhibitor FIFOs.
        if (QProcess::execute("sv", {"-w","10","restart","elogind"}) != 0)
          qFatal("Cannot apply elogind policy");
      }
      policy = next;
    }
    updatePowerInhibitor();
  }

  void discover() {
    for (const auto &name : QDir("/dev/input").entryList({"event*"},QDir::System|QDir::Files)) {
      QString path = "/dev/input/"+name;
      bool found = false;
      for (auto input : inputs) {
        char link[256] = {};
        int n = readlink(("/proc/self/fd/"+QString::number(input.fd)).toLocal8Bit(),link,255);
        if (n > 0 && path == QString::fromLocal8Bit(link,n)) found = true;
      }
      if (found) continue;
      int fd = open(path.toLocal8Bit(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
      if (fd < 0) continue;
      unsigned long types = 0;
      ioctl(fd,EVIOCGBIT(0,sizeof(types)),&types);
      if (!(types & (1UL<<EV_KEY))) { close(fd); continue; }
      auto notifier = new QSocketNotifier(fd,QSocketNotifier::Read,this);
      inputs.append({fd,notifier});
      connect(notifier,&QSocketNotifier::activated,this,[this,fd](int) {
        input_event events[32]; ssize_t size;
        while ((size=read(fd,events,sizeof(events))) > 0)
          for (int i=0;i<size/int(sizeof(input_event));++i) {
            auto event=events[i];
            if (event.type != EV_KEY) continue;
            if (event.code == 152 && event.value != 2) {
              locked = event.value == 0; publish();
            }
            if (!locked || event.code == KEY_POWER || event.code == 152) activity();
          }
        if (size == 0 || (size < 0 && (errno == ENODEV || errno == EIO))) {
          for (int i=0;i<inputs.size();++i) if (inputs[i].fd == fd) {
            inputs[i].notifier->setEnabled(false); inputs[i].notifier->deleteLater(); close(fd); inputs.removeAt(i); break;
          }
        }
      });
    }
  }
public slots:
  void sleepChanged(bool value) {
    sleeping = value;
    if (value) dim.stop();
    else { dimmed = false; publish(); configure(); }
  }
public:
  explicit Session(QObject *parent=nullptr) : QObject(parent),
    manager("org.freedesktop.login1","/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",QDBusConnection::systemBus()) {
    QDBusReply<QDBusObjectPath> result = manager.call("GetSessionByPID",uint(getpid()));
    if (!result.isValid()) qFatal("Framebuffer session is not registered: %s",qPrintable(result.error().message()));
    sessionPath = result.value().path();
    QDBusConnection::systemBus().connect("org.freedesktop.login1","/org/freedesktop/login1",
       "org.freedesktop.login1.Manager","PrepareForSleep",this,SLOT(sleepChanged(bool)));
    const char *console = ttyname(STDIN_FILENO);
    tty = console ? open(console,O_RDWR|O_CLOEXEC) : -1;
    if (tty < 0) qFatal("Framebuffer session has no controlling console");
    if (tty >= 0 && ioctl(tty,KDGKBMODE,&oldKeyboardMode) == 0) ioctl(tty,KDSKBMODE,K_OFF);
    dim.setSingleShot(true);
    connect(&dim,&QTimer::timeout,this,[this]{ dimmed=true; brightness(10); publish(); });
    auto settings = new QFileSystemWatcher(this);
    settings->addPath("/etc/reader");
    connect(settings,&QFileSystemWatcher::directoryChanged,this,[this]{ configure(); });
    QFile marker("/run/reader-activity");marker.open(QIODevice::WriteOnly);marker.close();
    auto extraInput = new QFileSystemWatcher(this);
    extraInput->addPath("/run/reader-activity");
    connect(extraInput,&QFileSystemWatcher::fileChanged,this,[this]{activity();});
    auto audio = new QFileSystemWatcher(this);
    audio->addPath("/run/reader-audio-status");
    connect(audio,&QFileSystemWatcher::fileChanged,this,[this,audio]{
      audio->addPath("/run/reader-audio-status");
      QFile file("/run/reader-audio-status");
      if (file.open(QIODevice::ReadOnly)) {
        QString next=QString::fromUtf8(file.readAll()).trimmed();
        if (next != volume) { volume=next; ++noticeSequence; publish(); }
      }
    });
    auto power = new QFileSystemWatcher(this);
    power->addPath("/run/reader-battery.json");
    connect(power,&QFileSystemWatcher::fileChanged,this,[this,power]{
      power->addPath("/run/reader-battery.json"); updatePowerInhibitor();
    });
    discover(); publish(); configure();
    connect(&discovery,&QTimer::timeout,this,[this]{discover();}); discovery.start(5000);
    // Inherit a blocked termination signal into this process only; unblock it in
    // the child before exec, and forward shutdown through Qt's event loop.
    sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGTERM);sigaddset(&mask,SIGINT);
    sigprocmask(SIG_BLOCK,&mask,nullptr);
    int signalFd=signalfd(-1,&mask,SFD_CLOEXEC|SFD_NONBLOCK);
    auto signalNotifier=new QSocketNotifier(signalFd,QSocketNotifier::Read,this);
    connect(signalNotifier,&QSocketNotifier::activated,this,[this,signalFd](int){
      signalfd_siginfo info;while(read(signalFd,&info,sizeof(info))>0) {}
      home.terminate();QTimer::singleShot(2000,&home,&QProcess::kill);
    });
    home.setProcessChannelMode(QProcess::ForwardedChannels);
    home.setInputChannelMode(QProcess::ForwardedInputChannel);
    connect(&home,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,
            [](int code,QProcess::ExitStatus){QCoreApplication::exit(code);});
    connect(&home,&QProcess::errorOccurred,this,[](QProcess::ProcessError e){
      if(e==QProcess::FailedToStart) QCoreApplication::exit(1);
    });
    home.start("/usr/local/bin/reader-launcher",QStringList{});
  }
  ~Session() override {
    home.terminate(); if(!home.waitForFinished(2000)) {home.kill();home.waitForFinished();}
    for (auto input:inputs) { delete input.notifier; close(input.fd); }
    brightness(readJson("/etc/reader/display.json").value("brightness_percent").toInt(100));
    if(tty>=0) {if(oldKeyboardMode>=0)ioctl(tty,KDSKBMODE,oldKeyboardMode);close(tty);}
    QFile::remove(statusPath);
  }
};
int main(int argc,char **argv) {QCoreApplication app(argc,argv);Session session;return app.exec();}
#include "session.moc"

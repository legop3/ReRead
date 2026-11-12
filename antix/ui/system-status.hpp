#pragma once
#include "reader-ui.hpp"

namespace ReRead {
// One presentation for Home and Guide, using the existing battery/ALSA state,
// Qt's filesystem statistics and wpa_supplicant's normal control client.
class SystemStatus final : public QObject {
  Screen &screen;
  QTimer timer;
  QProcess wifi;
  QString network="Wi-Fi: checking", volume="Volume: —", storage="Storage: —";
  QJsonObject battery;
  QColor networkColor=Qt::white, storageColor=Qt::white;
  bool wanted() const {
    return !screen.property("reread-idle").toBool() && screen.isActiveWindow() && (screen.helpVisible() ||
        (screen.property("reread-home").toBool() && screen.title=="ReRead" && screen.showHeader));
  }
public:
  explicit SystemStatus(Screen &owner):screen(owner) {
    timer.setInterval(5000);
    connect(&timer,&QTimer::timeout,this,[this]{if(wanted())refresh();else timer.stop();});
    connect(&wifi,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,
      [this](int code,QProcess::ExitStatus) {
        const QString reply=QString::fromUtf8(wifi.readAllStandardOutput());
        QString next=code==0?"Wi-Fi: disconnected":"Wi-Fi: unavailable";
        networkColor=Qt::white;
        if(code==0 && reply.contains("wpa_state=COMPLETED")) {
          networkColor=QColor("#00bfff");
          QString ssid;
          for(const auto &line:reply.split('\n'))if(line.startsWith("ssid="))ssid=line.mid(5).trimmed();
          next=ssid.isEmpty()?"Wi-Fi: connected":"Wi-Fi: "+ssid;
        }
        if(next!=network){network=next;screen.update();}
      });
  }
  ~SystemStatus() override {
    timer.stop();wifi.disconnect(this);
    if(wifi.state()!=QProcess::NotRunning){wifi.kill();wifi.waitForFinished(1000);}
  }
  void refresh() {
    battery=readJson("/run/reader-battery.json");
    QFile audio("/run/reader-audio-status");
    if(audio.open(QIODevice::ReadOnly))volume=QString::fromUtf8(audio.readAll()).trimmed().replace("Volume: ","Vol ");
    QStorageInfo disk(QDir("/var/lib/reader/files").exists()?"/var/lib/reader/files":"/");
    if(disk.isValid() && disk.isReady()) {
      const double free=double(disk.bytesAvailable())/(1024*1024);
      const double fraction=disk.bytesTotal()>0?double(disk.bytesAvailable())/disk.bytesTotal():1;
      storageColor=fraction<0.05?QColor("#ff3030"):fraction<0.10?QColor("#ffd000"):QColor(Qt::white);
      storage=free>=1024?QString::number(free/1024,'f',1)+" GiB free":QString::number(qRound(free))+" MiB free";
    }
    if(wifi.state()==QProcess::NotRunning) {
      QString interface;
      for(const auto &name:QDir("/sys/class/net").entryList(QDir::Dirs|QDir::NoDotAndDotDot))
        if(QFileInfo::exists("/sys/class/net/"+name+"/wireless")){interface=name;break;}
      if(interface.isEmpty())network="Wi-Fi: no adapter";
      else {
        wifi.start("wpa_cli",{"-i",interface,"status"});
        QTimer::singleShot(2000,&wifi,[this]{if(wifi.state()!=QProcess::NotRunning)wifi.kill();});
      }
    }
    screen.update();
  }
  void paint(QPainter &p,QRect area) {
    if(!timer.isActive() && wanted()){refresh();timer.start();}
    const bool known=battery.contains("capacity_percent");
    int charge=battery.value("capacity_percent").toInt();
    const QString state=battery.value("status").toString().toLower();
    const QString power=(state=="charging"?"Charge ":state=="full"?"Full ":battery.value("external_power").toBool()?"AC ":"Battery ")+
        (known?QString::number(charge)+"%":"—");
    const QStringList fields={power,network,volume,storage,QTime::currentTime().toString("HH:mm")};
    const int widths[]={165,qMax(70,area.width()-575),150,180,80};
    p.save();
    for(int i=0,x=area.left();i<fields.size();x+=widths[i++]) {
      QRect cell(x,area.top(),widths[i]-8,area.height());
      if(i==0) {
        QColor color=!known?QColor(Qt::white):charge<=10?QColor("#ff3030"):charge<=20?QColor("#ffd000"):QColor("#00df40");
        p.fillRect(cell.adjusted(0,4,0,-4),color);
        screen.label(p,cell.adjusted(6,0,-6,0),fields[i],20,true,Qt::black);
      } else if(i<4) {
        const QColor color=i==1?networkColor:i==2?(volume.contains("muted")?QColor(Qt::white):QColor("#ffd000")):storageColor;
        p.fillRect(cell.adjusted(0,4,0,-4),color);
        screen.label(p,cell.adjusted(6,0,-6,0),fields[i],20,true,Qt::black);
      } else screen.label(p,cell,fields[i],20,false,Qt::white);
    }
    p.restore();
  }
};
}

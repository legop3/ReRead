#include "reader-ui.hpp"
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QApplication>
#include <QPainter>

using namespace ReRead;
class PeggleImport : public Screen {
  QStringList images;
  int selected = 0;
  QString status;
  QProcess process;
  bool busy = false;

  void scan() {
    images.clear();
    QDirIterator it("/var/lib/reader/files", QStringList() << "*.iso" << "*.ISO",
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext() && images.size() < 100) images << it.next();
    images.sort(Qt::CaseInsensitive);
    selected = qBound(0, selected, qMax(0, images.size()-1));
    update();
  }
  void startImport() {
    if (busy || images.isEmpty()) return;
    busy = true;
    status = "Extracting Peggle from ISO…";
    controls = {{READER_BACK,"Return"},{READER_GUIDE,"Help"}};
    process.setProgram("/usr/bin/python3");
    process.setArguments({"/usr/lib/reread/apps/peggle/import-iso.py",
                         images[selected], "--delete-iso"});
    process.start();
    update();
  }
protected:
  void button(reader_button id, int value) override {
    if (!value) return;
    if (id == READER_UP || id == READER_DOWN) {
      if (!busy && !images.isEmpty()) {
        selected=qBound(0,selected+(id==READER_DOWN?1:-1),images.size()-1);
        update();
      }
      return;
    }
    if(value!=1) return;
    if (id==READER_BACK || id==READER_FAVORITES) {
      if (!busy) close();
    } else if (id==READER_OK && !busy) startImport();
    else if (id==READER_OPTIONS && !busy) { status.clear(); scan(); }
  }
  void draw(QPainter &p, const QRect &area) override {
    const auto accent=palette().color(QPalette::Link);
    label(p,QRect(area.x(),area.y(),area.width(),46),"Install Peggle Deluxe",30,true,accent);
    QRect main=area.adjusted(0,60,0,-60);
    QString message;
    if (busy) {
      message = "Importing game files. Leave this screen open.\n\n"
                "The ISO is removed only after a successful import.";
    } else if (images.isEmpty()) {
      message = "Upload peggle_retail_na.iso into Files using the USB web interface.\n\n"
                "Then press Options to scan again.";
    } else {
      message = QString("Press OK to import and remove this ISO after success:\n\n%1\n\n"
                        "Peggle's game files require roughly 17 MB.\n"
                        "Wine and controls are not installed by this app.")
                    .arg(QFileInfo(images[selected]).fileName());
      if (images.size()>1)
        message += QString("\n\nISO %1 of %2 (Up/Down selects)").arg(selected+1).arg(images.size());
    }
    text(p,main,message,23);
    if (!status.isEmpty())
      text(p,QRect(area.x(),area.bottom()-55,area.width(),55),status,20);
  }
public:
  PeggleImport():Screen("Peggle installer") {
    setWindowTitle("Peggle installer");
    showHeader=false;
    controls={{READER_UP,"Select ISO"},{READER_OK,"Import"},{READER_OPTIONS,"Rescan"},
              {READER_BACK,"Home"}};
    connect(&process,&QProcess::readyReadStandardOutput,this,[this]{
      const auto lines=QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed().split('\n');
      if(!lines.isEmpty()) status=lines.last().left(180);
      update();
    });
    connect(&process,static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
            this,[this](int code,QProcess::ExitStatus how){
      busy=false;
      if(code==0 && how==QProcess::NormalExit) {
        status="Game files imported. Wine runtime and game controls still need testing.";
      } else {
        auto error=QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        status="Import failed: "+error.right(190);
      }
      controls={{READER_UP,"Select ISO"},{READER_OK,"Import"},{READER_OPTIONS,"Rescan"},
                {READER_BACK,"Home"}};
      scan();
    });
    scan();
  }
  ~PeggleImport() override {
    if (process.state()!=QProcess::NotRunning) {
      process.kill();
      process.waitForFinished(3000);
    }
  }
};
int main(int argc,char **argv) {
  QApplication app(argc,argv);
  app.setApplicationName("reread-peggle-installer");
  app.setDesktopFileName("reread-peggle-installer");
  app.setFont(QFont("DejaVu Sans"));
  PeggleImport window;
  window.show();
  return app.exec();
}

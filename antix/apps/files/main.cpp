#include "reader-ui.hpp"
#include <QMimeDatabase>
using namespace ReRead;

class Files : public Screen {
  QString root, directory, page="browse", target;
  QStringList names, labels, details, lines;
  QFileSystemWatcher watcher;
  QMap<QString,int> positions;
  int selected=0, action=0, sort=0, scroll=0, zoom=1;
  QPoint pan;
  QImage image;
  bool busy=false;
  QString path() const { return QDir(root).filePath(directory); }
  QString chosen() const { return QDir(path()).filePath(names.value(selected)); }
  static QString size(qint64 bytes) {
    if(bytes<1024) return QString::number(bytes)+" B";
    if(bytes<1048576) return QString::number(bytes/1024.0,'f',1)+" KiB";
    return QString::number(bytes/1048576.0,'f',1)+" MiB";
  }
  QString relative(const QString &file) const { return QDir(root).relativeFilePath(file); }
  bool contained(const QString &file) const {
    auto canonical=QFileInfo(file).canonicalFilePath();
    return !canonical.isEmpty() && canonical.startsWith(root+"/");
  }
  void refresh(const QString &keep={}) {
    if(!QFileInfo(path()).isDir()) directory.clear();
    auto order=sort==1?QDir::Time:sort==2?QDir::Size:QDir::Name;
    names=QDir(path()).entryList(QDir::Dirs|QDir::Files|QDir::NoDotAndDotDot|QDir::NoSymLinks, QDir::DirsFirst|order);
    labels.clear(); details.clear();
    for(const auto &name:names) {
      QFileInfo entry(QDir(path()).filePath(name));
      labels << (entry.isDir()?"▸ ":"")+name;
      details << (entry.isDir()?"Folder":size(entry.size()))+" · "+entry.lastModified().toString("dd MMM yyyy HH:mm");
    }
    if(!keep.isEmpty() && names.contains(keep)) selected=names.indexOf(keep);
    selected=qBound(0,selected,qMax(0,names.size()-1));
    if(!watcher.directories().isEmpty()) watcher.removePaths(watcher.directories());
    watcher.addPath(path());
    chrome();
  }
  void chrome() {
    showHeader=page!="image"; showControls=true; controlsOverlay=page=="image";
    title=page=="confirm"?"Delete permanently?":page=="options"?"File actions":page=="details"?"Details":"Files";
    subtitle=directory.isEmpty()?"Shared files":directory;
    if(page=="browse") {
      subtitle += " · "+QString::number(names.size())+" items";
      controls={{READER_OK,"Open"},{READER_OPTIONS,"Actions"},{READER_BACK,directory.isEmpty()?"Home":"Up"}};
    } else if(page=="options") controls={{READER_OK,"Select"},{READER_BACK,"Files"}};
    else if(page=="confirm") controls={{READER_OK,"Select"},{READER_BACK,"Cancel"}};
    else if(page=="image") controls={{READER_ZOOM_IN,"Zoom"},{READER_OPTIONS,"Actions"},{READER_BACK,"Files"}};
    else controls={{READER_UP,"Scroll"},{READER_OPTIONS,"Actions"},{READER_BACK,"Files"}};
    update();
  }
  void browse() { page="browse"; image=QImage(); refresh(); }
  void open() {
    if(names.isEmpty()) return;
    target=chosen();
    if(!contained(target)) { toast("This item is no longer available."); refresh(); return; }
    if(QFileInfo(target).isDir()) {
      positions[directory]=selected; directory=relative(target); selected=positions.value(directory); browse(); return;
    }
    QImageReader reader(target); reader.setAutoTransform(true);
    if(reader.canRead()) {
      const auto limit=content().size()*4;
      if(reader.size().width()>limit.width() || reader.size().height()>limit.height())
        reader.setScaledSize(reader.size().scaled(limit,Qt::KeepAspectRatio));
      image=reader.read();
      if(image.isNull()) { toast("Unable to read this image."); return; }
      page="image"; zoom=1; pan={}; chrome(); return;
    }
    const auto mime=QMimeDatabase().mimeTypeForFile(target);
    if(mime.name().startsWith("text/") || QStringList{"json","log","md","csv"}.contains(QFileInfo(target).suffix().toLower())) {
      QFile file(target);
      if(!file.open(QIODevice::ReadOnly)) { toast(file.errorString()); return; }
      const bool truncated=file.size()>262144;
      lines=QString::fromUtf8(file.read(262144)).split('\n');
      if(truncated) lines << "[Preview limited to 256 KiB. Download the file for the full text.]";
      page="text"; scroll=0; chrome(); return;
    }
    page="details"; chrome(); toast("No viewer for this type. Download it through the web Files page.");
  }
  void options() {
    if(page=="browse") target=names.isEmpty()?QString():chosen();
    page="options"; action=0; chrome();
  }
  void remove() {
    if(!contained(target) || QFileInfo(target).isSymLink()) {
      toast("The item moved or disappeared. Nothing deleted."); browse(); return;
    }
    busy=true; title="Deleting…"; subtitle=relative(target); labels.clear(); details.clear(); update();
    QTimer::singleShot(30,this,[this] {
      const bool ok=QFileInfo(target).isDir()?QDir(target).removeRecursively():QFile::remove(target);
      busy=false; browse(); toast(ok?"Deleted.":"Could not delete everything. Refresh and check the remaining files.");
    });
  }
  void button(reader_button id,int value) override {
    if(!value || busy) return;
    if(value==2 && id!=READER_UP && id!=READER_DOWN && id!=READER_LEFT && id!=READER_RIGHT) return;
    if(id==READER_FAVORITES) { QApplication::quit(); return; }
    if(id==READER_GUIDE) { toast("OK opens · Options shows actions · Back goes up · Favorites returns Home"); return; }
    if(id==READER_BACK) {
      if(page!="browse") { browse(); return; }
      if(directory.isEmpty()) { QApplication::quit(); return; }
      directory=QFileInfo(directory).path(); if(directory==".") directory.clear();
      selected=positions.value(directory); refresh(); return;
    }
    if(id==READER_OPTIONS && page!="confirm") { options(); return; }
    if(page=="confirm" || page=="options") {
      const int count=page=="confirm"?2:4;
      if(id==READER_UP || id==READER_DOWN) { action=qBound(0,action+(id==READER_DOWN?1:-1),count-1);update(); }
      if(id==READER_OK && value==1) {
        if(page=="confirm") { if(action==1) remove(); else browse(); }
        else if(action==0) { if(target.isEmpty()) toast("Select a file or folder first."); else {page="details";scroll=0;chrome();} }
        else if(action==1) { if(target.isEmpty()) toast("Select a file or folder first.");else {page="confirm";action=0;chrome();} }
        else if(action==2) { sort=(sort+1)%3; browse(); }
        else browse();
      }
      return;
    }
    if(page=="image") {
      if(id==READER_ZOOM_IN || id==READER_ZOOM_OUT) {
        zoom=qBound(1,zoom*(id==READER_ZOOM_IN?2:1)/ (id==READER_ZOOM_OUT?2:1),4); pan={}; update();
      } else if(id==READER_LEFT || id==READER_RIGHT || id==READER_UP || id==READER_DOWN) {
        if(zoom>1) {
          pan+=QPoint(id==READER_LEFT?80:id==READER_RIGHT?-80:0,id==READER_UP?60:id==READER_DOWN?-60:0);
          const auto scaled=image.size().scaled(content().size(),Qt::KeepAspectRatio)*zoom;
          pan.setX(qBound(-qMax(0,(scaled.width()-content().width())/2),pan.x(),qMax(0,(scaled.width()-content().width())/2)));
          pan.setY(qBound(-qMax(0,(scaled.height()-content().height())/2),pan.y(),qMax(0,(scaled.height()-content().height())/2)));
          update();
        } else if(id==READER_LEFT || id==READER_RIGHT) {
          int step=id==READER_RIGHT?1:-1;
          for(int next=selected+step;next>=0&&next<names.size();next+=step) {
            QImageReader candidate(QDir(path()).filePath(names[next]));
            if(candidate.canRead()) {selected=next;open();break;}
          }
        }
      }
      return;
    }
    if(page=="text" || page=="details") {
      if(id==READER_UP || id==READER_DOWN) {scroll=qBound(0,scroll+(id==READER_DOWN?1:-1),qMax(0,lines.size()-1));update();}
      return;
    }
    if(id==READER_UP || id==READER_DOWN) { selected=qBound(0,selected+(id==READER_DOWN?1:-1),qMax(0,names.size()-1));update(); }
    if(id==READER_OK && value==1) open();
  }
  void draw(QPainter &p,const QRect &r) override {
    if(busy) {text(p,r,"Deleting "+relative(target)+"…",28);return;}
    if(page=="browse") {
      if(names.isEmpty()) text(p,r,"This folder is empty.\nAdd files using the USB web interface.",26);
      else list(p,r,labels,details,selected);
    } else if(page=="options") {
      list(p,r,{"Details","Delete…","Sort: "+QStringList{"Name","Newest","Size"}[sort],"Refresh"},
           {target.isEmpty()?"No item selected":QFileInfo(target).fileName(),"Permanently remove the selected item","Folders stay first","Reload this folder"},action);
    } else if(page=="confirm") {
      list(p,r,{"Cancel","Delete permanently"},{"Keep "+QFileInfo(target).fileName(),QFileInfo(target).isDir()?"Delete this folder and ALL its contents. No undo.":"Delete "+QFileInfo(target).fileName()+". No undo."},action);
    } else if(page=="image") {
      const auto scaled=image.size().scaled(r.size(),Qt::KeepAspectRatio)*zoom;
      p.setClipRect(r); p.drawImage(QRect(QPoint(r.center().x()-scaled.width()/2,r.center().y()-scaled.height()/2)+pan,scaled),image);
    } else if(page=="details") {
      QFileInfo file(target);
      QString info=relative(target)+"\n\n"+(file.isDir()?"Folder":QMimeDatabase().mimeTypeForFile(target).comment()+" · "+size(file.size()));
      info+="\nModified: "+file.lastModified().toString("dd MMM yyyy HH:mm");
      if(file.isDir()) info+="\n"+QString::number(QDir(target).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).size())+" items directly inside";
      text(p,r,info,25);
    } else {
      p.setClipRect(r); auto font=p.font();font.setPixelSize(23);p.setFont(font);p.setPen(QColor("#eef3f8"));
      int y=r.top();
      for(int i=scroll;i<lines.size()&&y<r.bottom();++i) {
        QRect bounds=p.boundingRect(QRect(r.left(),y,r.width(),10000),Qt::TextWordWrap,lines[i].isEmpty()?" ":lines[i]);
        p.drawText(bounds,Qt::TextWordWrap,lines[i]);y+=qMax(29,bounds.height()+5);
      }
    }
  }
public:
  Files():Screen("Files") {
    root=qEnvironmentVariable("REREAD_FILES_DIR","/var/lib/reader/files"); QDir().mkpath(root);
    root=QFileInfo(root).canonicalFilePath(); refresh();
    connect(&watcher,&QFileSystemWatcher::directoryChanged,this,[this] {
      if(page=="browse"&&!busy)refresh(names.value(selected));
    });
  }
};
int main(int argc,char **argv) {
  // Screen already reads evdev; Qt's LinuxFB keyboard handler would deliver
  // each navigation press a second time.
  qputenv("QT_QPA_FB_DISABLE_INPUT","1");
  QApplication app(argc,argv);app.setFont(QFont("DejaVu Sans"));
  Files window;window.showFullScreen();return app.exec();
}

#include <gio/gio.h>
#include "reader-ui.hpp"
#include <QMimeDatabase>
using namespace ReRead;

class Files : public Screen {
  QString root, directory, page="browse", target;
  QStringList names, labels, details, lines;
  QFileSystemWatcher watcher;
  QMap<QString,int> positions;
  int selected=0, action=0, sort=0, scroll=0;
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
    showHeader=true; showControls=true; controlsOverlay=false;
    title=page=="confirm"?"Delete permanently?":page=="options"?"File actions":page=="details"?"Details":"Files";
    subtitle=directory.isEmpty()?"Shared files":directory;
    if(page=="browse") {
      subtitle += " · "+QString::number(names.size())+" items";
      controls={{READER_OK,"Open"},{READER_OPTIONS,"Actions"},{READER_BACK,directory.isEmpty()?"Home":"Up"}};
    } else if(page=="options") controls={{READER_OK,"Select"},{READER_BACK,"Files"}};
    else if(page=="confirm") controls={{READER_OK,"Select"},{READER_BACK,"Cancel"}};
    else controls={{READER_UP,"Scroll"},{READER_OPTIONS,"Actions"},{READER_BACK,"Files"}};
    update();
  }
  void browse() { page="browse"; refresh(); }
  void open() {
    if(names.isEmpty()) return;
    target=chosen();
    if(!contained(target)) { toast("This item is no longer available."); refresh(); return; }
    if(QFileInfo(target).isDir()) {
      positions[directory]=selected; directory=relative(target); selected=positions.value(directory); browse(); return;
    }
    auto context=g_app_launch_context_new();
    // A separately launched app must not inherit Files' private app identity.
    g_app_launch_context_unsetenv(context,"READER_APP_ID");
    g_app_launch_context_unsetenv(context,"READER_APP_DATA_DIR");
    GError *error=nullptr;
    auto uri=QUrl::fromLocalFile(target).toEncoded();
    bool opened=g_app_info_launch_default_for_uri(uri.constData(),context,&error);
    g_object_unref(context);
    if(opened)return; // Files stays mapped; X returns focus here when the viewer closes.
    bool missing=error && g_error_matches(error,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED);
    QString message=error?QString::fromUtf8(error->message):QStringLiteral("Unable to open this file.");
    if(error)g_error_free(error);
    if(!missing){toast(message);return;}
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
    if(page=="text" || page=="details") {
      if(id==READER_UP || id==READER_DOWN) {scroll=qBound(0,scroll+(id==READER_DOWN?1:-1),qMax(0,lines.size()-1));update();}
      return;
    }
    if(id==READER_UP || id==READER_DOWN) { selected=qBound(0,selected+(id==READER_DOWN?1:-1),qMax(0,names.size()-1));update(); }
    if(id==READER_OK && value==1) open();
  }
  void entryIcon(QPainter &p,const QRect &r,bool folder,bool selected=false) {
    const QIcon icon("/usr/share/icons/reread/scalable/apps/"+QString(folder?"folder.svg":"text-x-generic.svg"));
    auto pixmap=icon.pixmap(r.size());
    if(selected) {
      QPainter tint(&pixmap);tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
      tint.fillRect(pixmap.rect(),palette().color(QPalette::HighlightedText));
    }
    p.drawPixmap(r,pixmap);
  }
  void drawBrowser(QPainter &p,const QRect &r) {
    auto label=[&](const QRect &box,const QString &value,int px,QColor ink,bool bold=false) {
      Screen::label(p,box,value,px,bold,ink,Qt::ElideMiddle);
    };
    const QColor ink=palette().color(QPalette::Text),accent=palette().color(QPalette::Link);
    label(QRect(r.x(),r.y(),r.width()-150,36),directory.isEmpty()?"Shared files /":"Shared files / "+directory,24,accent,true);
    label(QRect(r.right()-140,r.y(),140,36),QString::number(names.size())+" items",21,ink);
    auto layout=listInfo(p,r.adjusted(0,76,0,0),names.size(),selected,50,2.0/3);
    const int split=layout.info.left()-18;
    const int width=layout.entries.width();
    label(QRect(r.x()+46,r.y()+42,width-180,28),"Name",18,ink);
    label(QRect(split-145,r.y()+42,128,28),"Kind / size",18,ink);
    for(int i=layout.first;i<qMin(layout.first+layout.visible,names.size());++i) {
      const QRect row=listInfoRow(p,layout,i,selected);
      const bool focus=i==selected;const auto color=focus?palette().color(QPalette::HighlightedText):ink;
      QFileInfo file(QDir(path()).filePath(names[i]));
      entryIcon(p,QRect(row.x()+7,row.y()+9,30,30),file.isDir(),focus);
      label(QRect(row.x()+46,row.y(),row.width()-182,row.height()),names[i],24,color,focus);
      label(QRect(row.right()-128,row.y(),120,row.height()),file.isDir()?"Folder":size(file.size()),19,color);
    }
    if(names.isEmpty())text(p,QRect(r.x()+12,r.y()+85,width-24,r.height()-90),"Empty folder\nUpload files through the web interface.",24);
    const QRect side=layout.info.adjusted(0,-20,0,0);
    if(names.isEmpty()) {text(p,side,"Shared storage\n\nFiles and applications use this same folder.",23);return;}
    QFileInfo file(chosen());
    entryIcon(p,QRect(side.x(),side.y(),64,64),file.isDir());
    label(QRect(side.x(),side.y()+76,side.width(),34),file.fileName(),24,ink,true);
    QString info=file.isDir()?"Folder":QMimeDatabase().mimeTypeForFile(file.filePath()).comment()+"\n"+size(file.size());
    info+="\n\nModified\n"+file.lastModified().toString("d MMM yyyy\nHH:mm");
    auto normal=p.font();normal.setBold(false);p.setFont(normal);
    text(p,QRect(side.x(),side.y()+120,side.width(),150),info,21);
    buttonIcon(p,QRect(side.x(),side.bottom()-56,26,26),READER_OK);
    label(QRect(side.x()+34,side.bottom()-55,side.width()-34,26),file.isDir()?"Open folder":"Open file",20,accent);
    label(QRect(side.x(),side.bottom()-27,side.width(),26),QString::number(selected+1)+" of "+QString::number(names.size())+" · "+QStringList{"Name","Newest","Size"}[sort]+" order",18,ink);
  }
  void draw(QPainter &p,const QRect &r) override {
    if(busy) {text(p,r,"Deleting "+relative(target)+"…",28);return;}
    if(page=="browse") {
      drawBrowser(p,r);
    } else if(page=="options") {
      list(p,r,{"Details","Delete…","Sort: "+QStringList{"Name","Newest","Size"}[sort],"Refresh"},
           {target.isEmpty()?"No item selected":QFileInfo(target).fileName(),"Permanently remove the selected item","Folders stay first","Reload this folder"},action);
    } else if(page=="confirm") {
      list(p,r,{"Cancel","Delete permanently"},{"Keep "+QFileInfo(target).fileName(),QFileInfo(target).isDir()?"Delete this folder and ALL its contents. No undo.":"Delete "+QFileInfo(target).fileName()+". No undo."},action);
    } else if(page=="details") {
      QFileInfo file(target);
      QString info=relative(target)+"\n\n"+(file.isDir()?"Folder":QMimeDatabase().mimeTypeForFile(target).comment()+" · "+size(file.size()));
      info+="\nModified: "+file.lastModified().toString("dd MMM yyyy HH:mm");
      if(file.isDir()) info+="\n"+QString::number(QDir(target).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).size())+" items directly inside";
      text(p,r,info,25);
    } else {
      p.fillRect(r,palette().base()); p.setClipRect(r); auto font=p.font();font.setPixelSize(23);p.setFont(font);p.setPen(palette().text().color());
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
  QApplication app(argc,argv);app.setFont(QFont("DejaVu Sans"));
  Files window;window.show();return app.exec();
}

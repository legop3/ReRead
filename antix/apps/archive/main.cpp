#include <gio/gio.h>
#include "reader-ui.hpp"
#include <QtNetwork>
#include <QtWidgets>
#include <QStorageInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QElapsedTimer>
#include <cmath>
#include <algorithm>
#include <memory>
#include <csignal>
#include <unistd.h>
using namespace ReRead;

namespace {

QString value(const QJsonValue &v) {
  if (v.isString()) return v.toString();
  if (v.isDouble()) return QString::number(v.toDouble());
  if (v.isArray()) {
    QStringList parts; for (const auto &x:v.toArray()) if(x.isString()) parts << x.toString();
    return parts.join(", ");
  }
  return {};
}
QString plain(QString s) {
  s.replace(QRegularExpression("<[^>]*>")," ");
  s.replace("&amp;","&").replace("&quot;","\"").replace("&#39;","'")
    .replace("&lt;","<").replace("&gt;",">").replace("&nbsp;"," ");
  return s.simplified();
}
QString human(qint64 n) {
  if (n<=0) return "Unknown size";
  if(n<1024*1024) return QString::number(n/1024.0,'f',0)+" KB";
  if(n<1024LL*1024*1024) return QString::number(n/(1024.0*1024),'f',1)+" MB";
  return QString::number(n/(1024.0*1024*1024),'f',2)+" GB";
}
int durationValue(QString v) {
  if(v.contains(':')) {
    int result=0;
    for(const auto &n:v.split(':')) result=result*60+n.toInt();
    return result;
  }
  return qMax(0,int(v.toDouble()));
}
QUrl fileUrl(const QString &id,const QString &name) {
  return QUrl("https://archive.org/download/"+QString::fromUtf8(QUrl::toPercentEncoding(id))+
              "/"+QString::fromUtf8(QUrl::toPercentEncoding(name,"/")));
}
QUrl coverUrl(const QString &id) {
  return QUrl("https://archive.org/services/img/"+QString::fromUtf8(QUrl::toPercentEncoding(id)));
}
QString sanitize(QString name) {
  name=QFileInfo(name).fileName();
  name.replace(QRegularExpression("[^a-zA-Z0-9._ -]"),"_");
  while(name.startsWith('.'))name.remove(0,1);
  return name.left(120).isEmpty()?"archive-file":name.left(120);
}
bool isImage(QString s) {
  s=s.toLower();
  return s.endsWith(".jpg")||s.endsWith(".jpeg")||s.endsWith(".png")||s.endsWith(".webp");
}
bool isText(QString s) { s=s.toLower(); return s.endsWith(".txt")||s.endsWith(".text"); }
bool isAudio(QString s) {
  s=QFileInfo(s).suffix().toLower();return QStringList{"mp3","ogg","oga","wav","flac","opus"}.contains(s);
}
}
struct Entry {QString id,title,creator,date,kind;};
struct File {QString name,format; qint64 size=0; int seconds=0;};
class Archive : public Screen {
  friend struct ArchiveTest;
  enum Page {HOME, BROWSE, ITEM, FILES, DOWNLOAD_CONFIRM, DOWNLOADING, INFO};
  Page page=HOME, downloadReturn=FILES;
  QNetworkAccessManager network;
  QNetworkReply *api=nullptr, *transfer=nullptr;
  QMap<QString,QImage> thumbs, fileThumbs;
  QSet<QString> fileThumbPending, fileThumbFailed;
  QList<QNetworkReply*> fileThumbJobs;
  QList<QNetworkReply*> coverJobs;
  QSet<QString> coverPending, failedCovers;
  QList<Entry> entries;
  QList<File> files;
  QStringList favorites;
  QString category, collectionId, itemId, itemTitle, creator, itemDate, description, status;
  int selected=0, gridStart=0, fileStart=0, searchPage=1, sortIndex=0;
  int browseSelected=0, browseGridStart=0;
  bool loadingMore=false, hasMore=false;
  static constexpr int resultsPerPage=24;
  File selectedFile, downloadFile;
  std::unique_ptr<QSaveFile> saveFile;
  QString downloadPath;
  qint64 received=0, expected=0;
  bool loaded=false;
  QStringList sections={"Audio","Books","Images","Software","Collections","Favorites"};
  QStringList types={"audio","texts","image","software","collection",""};
  QString storagePath() const {return qEnvironmentVariable("READER_APP_DATA_DIR","/var/lib/reader/apps/reread-archive");}
  QString sharedPath() const {return qEnvironmentVariable("REREAD_FILES_DIR","/var/lib/reader/files");}
  QColor bg, panel, outline, ink, muted, accent, blue;
  QColor color(bool active) const {return active?accent:ink;}
  void txt(QPainter &p,const QRect &r,const QString &str,int px=23,QColor c=QColor(),
           int align=Qt::AlignLeft|Qt::AlignVCenter,bool bold=false) const {
    if(!c.isValid())c=ink;
    QFont f=p.font();f.setPixelSize(px);f.setBold(bold);p.setFont(f);
    p.setPen(c);
    p.drawText(r,align|Qt::TextWordWrap,str);
  }
  QString fitText(const QString &s,int px,int width) const {
    QFont f=font();f.setPixelSize(px);
    return QFontMetrics(f).elidedText(s,Qt::ElideRight,qMax(10,width));
  }
  void round(QPainter &p,const QRect &r,QColor c,int radius=3) const {
    p.setPen(Qt::NoPen);p.setBrush(c);p.drawRoundedRect(r,radius,radius);
  }
  void icon(QPainter &p,const QRect &rect,const QString &kind,QColor c=QColor()) const {
    if(!c.isValid())c=accent;
    p.save();p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(c,3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    auto r=rect.adjusted(8,8,-8,-8);
    if(kind=="Audio"||kind=="audio") {
      p.drawEllipse(r.adjusted(6,5,-6,-5));
      p.drawLine(r.center().x(),r.top()+6,r.center().x(),r.bottom()-5);
      p.drawEllipse(QRect(r.center().x()-9,r.bottom()-13,14,10));
    } else if(kind=="Books"||kind=="texts") {
      p.drawRoundedRect(r.adjusted(4,1,-4,-1),3,3);
      p.drawLine(r.center().x(),r.top()+1,r.center().x(),r.bottom()-2);
      p.drawLine(r.left()+8,r.top()+11,r.center().x()-6,r.top()+11);
      p.drawLine(r.center().x()+6,r.top()+11,r.right()-8,r.top()+11);
    } else if(kind=="Images"||kind=="image") {
      p.drawRoundedRect(r,4,4);p.drawEllipse(QRect(r.left()+8,r.top()+8,9,9));
      QPolygon shape;shape<<QPoint(r.left()+5,r.bottom()-6)<<QPoint(r.center().x(),r.center().y())
                          <<QPoint(r.right()-5,r.bottom()-6);p.drawPolyline(shape);
    } else if(kind=="Favorites") {
      QPolygonF star;
      for(int i=0;i<10;++i){double a=-3.14159265358979323846/2+i*3.14159265358979323846/5;double rad=(i%2)?r.width()*.20:r.width()*.45;
        star<<QPointF(r.center().x()+cos(a)*rad,r.center().y()+sin(a)*rad);}
      p.drawPolygon(star);
    } else if(kind=="Software"||kind=="software") {
      p.drawRoundedRect(r.adjusted(4,4,-4,-4),3,3);
      p.drawLine(r.center().x(),r.top()+9,r.center().x(),r.bottom()-9);
      p.drawLine(r.center().x()-9,r.center().y(),r.center().x()+9,r.center().y());
    } else {
      p.drawRoundedRect(r.adjusted(1,7,-1,-7),3,3);
      p.drawRect(r.adjusted(9,1,-9,-1));
    }
    p.restore();
  }
  void imageIn(QPainter &p,const QRect &r,const QImage &img,bool crop) const {
    if(img.isNull())return;
    QSize scaled=img.size();
    scaled.scale(r.size(),crop?Qt::KeepAspectRatioByExpanding:Qt::KeepAspectRatio);
    QRect target(QPoint(0,0),scaled);target.moveCenter(r.center());
    p.save();p.setClipRect(r);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(target,img);p.restore();
  }
  void card(QPainter &p,const QRect &r,const QString &name,const QString &sub,
            const QImage &img,bool active,const QString &symbol) const {
    p.fillRect(r,bg);
    p.setPen(QPen(active?accent:ink,active?3:1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(1,1,-2,-2));
    QRect cover=r.adjusted(5,5,-5,-65);
    p.fillRect(cover,bg);
    if(!img.isNull())imageIn(p,cover,img,true);
    else {
      icon(p,QRect(cover.center().x()-29,cover.center().y()-29,58,58),symbol,ink);
    }
    txt(p,QRect(r.x()+9,r.bottom()-57,r.width()-18,34),
        fitText(name,22,r.width()-22),22,accent,Qt::AlignLeft|Qt::AlignVCenter,true);
    txt(p,QRect(r.x()+9,r.bottom()-27,r.width()-18,18),
        fitText(sub,18,r.width()-22),18,ink);
  }

  void abortApi() {if(api){api->disconnect(this);api->abort();api->deleteLater();api=nullptr;}}
  void abortCovers() {
    for(auto r:coverJobs){r->disconnect(this);r->abort();r->deleteLater();}
    coverJobs.clear();coverPending.clear();
  }
    void showStatus(const QString &s) {status=s;update();}
  void loadFavorites(){
    QFile f(storagePath()+"/favorites.json");if(!f.open(QIODevice::ReadOnly))return;
    auto arr=QJsonDocument::fromJson(f.readAll()).array();
    for(auto v:arr)if(v.isString()&&favorites.size()<200)favorites<<v.toString();
  }
  void storeFavorites() {
    QDir().mkpath(storagePath());
    QSaveFile f(storagePath()+"/favorites.json");
    if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonArray::fromStringList(favorites)).toJson(QJsonDocument::Compact));f.commit();}
  }
  void fetch(QUrl url,const QString &kind,int cap=3*1024*1024) {
    abortApi();loaded=false;
    if(kind!="search" || !loadingMore)showStatus("Loading from archive.org...");
    else update();
    QNetworkRequest req(url);req.setRawHeader("User-Agent","ReRead-Archive/0.2");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    auto r=network.get(req);api=r;
    connect(r,&QNetworkReply::downloadProgress,this,[r,cap](qint64 done,qint64) {if(done>cap)r->abort();});
    connect(r,&QNetworkReply::finished,this,[this,r,kind,cap]{
      if(api!=r){r->deleteLater();return;}api=nullptr;
      QByteArray bytes=r->readAll();
      QString problem=r->errorString();
      bool ok=r->error()==QNetworkReply::NoError && bytes.size()<=cap;
      r->deleteLater();loaded=true;
      if(!ok){
        if(kind=="search" && loadingMore){
          loadingMore=false;
          --searchPage;
          hasMore=false;
          showStatus("Could not load more results. Press Down at the end to retry.");
        } else showStatus("Unable to load: "+problem);
        return;
      }
      status.clear();
      if(kind=="open") {
        const QString path=cachedFile(fileUrl(itemId,selectedFile.name));
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())
          status="Cannot cache this file. Check available storage.";
        else {trimCache(path);launchFile(QUrl::fromLocalFile(path),selectedFile.name);}
      } else {
        QJsonParseError err;
        auto doc=QJsonDocument::fromJson(bytes,&err);
        if(err.error!=QJsonParseError::NoError||!doc.isObject()){showStatus("Invalid Archive response");return;}
        auto root=doc.object();
        if(kind=="search") {
          auto response=root.value("response").toObject();
          auto results=response.value("docs").toArray();
          const int found=int(response.value("numFound").toDouble(0));
          const bool append=loadingMore;
          loadingMore=false;
          if(!append){entries.clear();gridStart=0;selected=0;}
          QSet<QString> seen;
          for(const auto &e:entries)seen.insert(e.id);
          for(const auto &raw:results){
            auto o=raw.toObject();
            Entry e{value(o.value("identifier")),plain(value(o.value("title"))),
                    plain(value(o.value("creator"))),value(o.value("year")),value(o.value("mediatype"))};
            if(e.id.isEmpty()||seen.contains(e.id))continue;
            if(e.title.isEmpty())e.title=e.id;
            seen.insert(e.id);entries<<e;
          }
          hasMore=results.size()==resultsPerPage && (found<=0 || entries.size()<found);
          if(entries.isEmpty())status="Nothing found in this collection.";
          requestVisibleCovers();
        } else if(kind=="metadata") {
          files.clear();auto meta=root.value("metadata").toObject();
          itemTitle=plain(value(meta.value("title")));
          if(itemTitle.isEmpty())itemTitle=itemId;
          creator=plain(value(meta.value("creator")));
          itemDate=value(meta.value("year"));
          description=plain(value(meta.value("description")));
          for(auto file:root.value("files").toArray()){
            auto o=file.toObject();File f;
            f.name=value(o.value("name"));f.format=value(o.value("format"));
            f.size=value(o.value("size")).toLongLong();
            f.seconds=durationValue(value(o.value("length")));
            QString lower=f.name.toLower();
            if(f.name.isEmpty()||lower.endsWith(".xml")||lower.endsWith(".sqlite")||
               lower.endsWith(".torrent")||lower.endsWith(".md5")||
               lower.endsWith(".sha1")||lower.endsWith(".json")||
               lower.endsWith(".zip")||lower.endsWith(".7z"))continue;
            files<<f;
          }
          std::stable_sort(files.begin(),files.end(),[](const File&a,const File&b){
            auto priority=[](const File&f){if(isAudio(f.name))return 0;if(isText(f.name))return 1;
             if(isImage(f.name))return 2;
              return 3;
            };
            return priority(a)<priority(b);
          });
          requestCover(itemId);
        }
      }
      update();
    });
  }
  void requestFilePreviews() {
    if(page!=FILES || files.isEmpty())return;
    const int visible=activeFiles();
    for(int i=fileStart;i<qMin(fileStart+visible,files.size());++i) {
      const File &file=files[i];
      if(!isImage(file.name)||file.size<=0||file.size>650*1024)continue;
      const QString key=file.name;
      if(fileThumbs.contains(key)||fileThumbFailed.contains(key)||
         fileThumbPending.contains(key)||fileThumbJobs.size()>=2)continue;
      fileThumbPending.insert(key);
      const QString sourceItem=itemId;
      QNetworkRequest req(fileUrl(itemId,key));
      req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
      auto job=network.get(req);fileThumbJobs<<job;
      connect(job,&QNetworkReply::downloadProgress,this,[job](qint64 n,qint64) {
        if(n>700*1024)job->abort();
      });
      connect(job,&QNetworkReply::finished,this,[this,job,key,sourceItem] {
        fileThumbJobs.removeAll(job);
        fileThumbPending.remove(key);
        if(sourceItem==itemId && job->error()==QNetworkReply::NoError) {
          QByteArray data=job->readAll();
          if(data.size()<=700*1024) {
            QBuffer buf(&data);buf.open(QIODevice::ReadOnly);
            QImageReader reader(&buf);
            QSize original=reader.size();
            if(original.isValid())
              reader.setScaledSize(original.scaled(150,110,Qt::KeepAspectRatio));
            QImage image=reader.read();
            if(!image.isNull()) {
              fileThumbs.insert(key,image);
              if(fileThumbs.size()>12)fileThumbs.erase(fileThumbs.begin());
            }
          }
        }
        if(sourceItem==itemId && !fileThumbs.contains(key))
          fileThumbFailed.insert(key);
        job->deleteLater();
        if(page==FILES)requestFilePreviews();
        update();
      });
    }
  }
  void requestCover(const QString &id) {
    if(id.isEmpty()||thumbs.contains(id)||failedCovers.contains(id)||coverPending.contains(id)||coverJobs.size()>=4)return;
    coverPending.insert(id);
    QNetworkRequest req(coverUrl(id));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    auto r=network.get(req);coverJobs<<r;
    connect(r,&QNetworkReply::downloadProgress,this,[r](qint64 n,qint64){if(n>1024*1024)r->abort();});
    connect(r,&QNetworkReply::finished,this,[this,r,id]{
      coverJobs.removeAll(r);coverPending.remove(id);
      if(r->error()==QNetworkReply::NoError){
        QByteArray bytes=r->readAll();
        if(bytes.size()<1024*1024){
          QBuffer b(&bytes);b.open(QIODevice::ReadOnly);
          QImageReader reader(&b);
          QSize original=reader.size();
          if(original.isValid())reader.setScaledSize(original.scaled(320,220,Qt::KeepAspectRatio));
          QImage img=reader.read();
          if(!img.isNull()){thumbs[id]=img;if(thumbs.size()>32)thumbs.erase(thumbs.begin());}
        }
      }
      if(!thumbs.contains(id))failedCovers.insert(id);
      r->deleteLater();requestVisibleCovers();update();
    });
  }
  void requestVisibleCovers() {
    if(page==ITEM||page==FILES){
      requestCover(itemId);
      if(page==FILES)requestFilePreviews();
      return;
    }
    if(page!=BROWSE)return;
    for(int i=gridStart;i<qMin(gridStart+6,entries.size()) && coverJobs.size()<4;++i)
      requestCover(entries[i].id);
  }
  void fetchSearchPage() {
    QString type=types.value(sections.indexOf(category),"audio");
    QString query=collectionId.isEmpty()?"mediatype:"+type:"collection:"+collectionId;
    QUrl url("https://archive.org/advancedsearch.php");
    QUrlQuery q;
    q.addQueryItem("q",query);q.addQueryItem("output","json");
    for(const auto &f:{"identifier","title","creator","year","mediatype"})
      q.addQueryItem("fl[]",f);
    q.addQueryItem("rows",QString::number(resultsPerPage));
    q.addQueryItem("page",QString::number(searchPage));
    q.addQueryItem("sort[]",sortIndex==0?"downloads desc":"publicdate desc");
    url.setQuery(q);fetch(url,"search");
  }
  void maybeLoadMore() {
    if(page!=BROWSE || category=="Favorites" || api || loadingMore || !hasMore)
      return;
    if(selected<entries.size()-6)return;
    loadingMore=true;
    ++searchPage;
    fetchSearchPage();
  }
  void search() {
    abortApi();abortCovers();
    page=BROWSE;selected=0;gridStart=0;entries.clear();
    thumbs.clear();failedCovers.clear();
    searchPage=1;loadingMore=false;hasMore=false;loaded=false;status.clear();
    if(category=="Favorites"){
      loaded=true;
      for(const auto &id:favorites)entries<<Entry{id,id,"Saved item","",""};
      requestVisibleCovers();update();return;
    }
    fetchSearchPage();
  }
  void openItem(int index) {
    if(index<0||index>=entries.size())return;
    const auto e=entries[index];itemId=e.id;itemTitle=e.title;creator=e.creator;itemDate=e.date;
    browseSelected=selected;browseGridStart=gridStart;
    description.clear();files.clear();fileThumbs.clear();
    fileThumbFailed.clear();selected=0;page=ITEM;requestCover(itemId);
    fetch(QUrl("https://archive.org/metadata/"+QString::fromUtf8(QUrl::toPercentEncoding(itemId))),"metadata",12*1024*1024);
  }
  QString cachedFile(const QUrl &url) const {
    auto key=QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(),QCryptographicHash::Sha256).toHex());
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)+"/reread-archive/files/"+
      key+"/"+sanitize(QFileInfo(url.path()).fileName());
  }
  void trimCache(const QString &keep) {
    QDir root(QFileInfo(keep).dir().absolutePath());root.cdUp();
    QList<QFileInfo> entries;qint64 total=0;
    for(const auto &folder:root.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot))
      for(const auto &file:QDir(folder.absoluteFilePath()).entryInfoList(QDir::Files)) {
        entries.append(file);total+=file.size();
      }
    std::sort(entries.begin(),entries.end(),[](const QFileInfo &a,const QFileInfo &b){return a.lastModified()<b.lastModified();});
    for(const auto &file:entries) {
      if(total<=64*1024*1024)break;
      if(file.absoluteFilePath()!=keep && QFile::remove(file.absoluteFilePath())) {
        total-=file.size();QDir().rmdir(file.absolutePath());
      }
    }
  }
  void launchFile(const QUrl &url,const QString &name) {
    const QByteArray filename=name.toUtf8();
    gchar *type=g_content_type_guess(filename.constData(),nullptr,0,nullptr);
    GAppInfo *handler=g_app_info_get_default_for_type(type,!url.isLocalFile());g_free(type);
    if(!handler){showStatus("No default viewer for this file. Install one through Software or download it.");return;}
    auto context=g_app_launch_context_new();
    g_app_launch_context_unsetenv(context,"READER_APP_ID");g_app_launch_context_unsetenv(context,"READER_APP_DATA_DIR");
    QByteArray uri=url.toEncoded();GList argument{uri.data(),nullptr,nullptr};GError *error=nullptr;
    bool opened=g_app_info_launch_uris(handler,&argument,context,&error);
    g_object_unref(context);g_object_unref(handler);
    status=opened?QString():error?QString::fromUtf8(error->message):"Unable to launch the default viewer.";
    if(error)g_error_free(error);
    update();
  }
  void openFile(const File &file) {
    selectedFile=file;const QUrl url=fileUrl(itemId,file.name);
    if(isAudio(file.name)){launchFile(url,file.name);return;}
    if(!isImage(file.name) && !isText(file.name)) {
      showStatus("No preview for this type. Options can download it, then Files can open it.");return;
    }
    QString cached=cachedFile(url);
    if(QFileInfo(cached).isFile()) {
      QFile touch(cached);if(touch.open(QIODevice::ReadOnly))touch.setFileTime(QDateTime::currentDateTime(),QFileDevice::FileModificationTime);
      launchFile(QUrl::fromLocalFile(cached),file.name);return;
    }
    trimCache(cached);
    QStorageInfo disk(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation));
    if(disk.isValid() && disk.bytesAvailable()<128LL*1024*1024+14*1024*1024) {
      showStatus("Not enough storage for a preview. Keeping 128 MiB free.");return;
    }
    fetch(url,"open",isImage(file.name)?14*1024*1024:7*1024*1024);
  }
    void downloadSelected() {
    downloadFile=selectedFile;downloadReturn=page;page=DOWNLOAD_CONFIRM;status.clear();update();
  }
  void beginDownload(){
    QString root=sharedPath()+"/Internet Archive";
    if(!QDir().mkpath(root)){status="Cannot create shared Files directory";update();return;}
    QStorageInfo storage(root);
    const qint64 free=storage.bytesAvailable();
    const qint64 reserve=128LL*1024*1024;
    if(!storage.isValid()||!storage.isReady()||free<reserve||
       (downloadFile.size>0 && downloadFile.size>free-reserve)){
      status="Not enough storage: keeping 128 MB free";update();return;
    }
    QString base=sanitize(downloadFile.name);
    downloadPath=root+"/"+base;
    for(int n=1;QFileInfo::exists(downloadPath) && n<1000;++n){
      QFileInfo f(base);downloadPath=root+"/"+f.completeBaseName()+"-"+QString::number(n)+
          (f.suffix().isEmpty()?"":"."+f.suffix());
    }
    saveFile.reset(new QSaveFile(downloadPath));
    if(!saveFile->open(QIODevice::WriteOnly)){saveFile.reset();status="Cannot open download destination";update();return;}
    received=0;expected=downloadFile.size;page=DOWNLOADING;status.clear();
    QNetworkRequest req(fileUrl(itemId,downloadFile.name));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    transfer=network.get(req);
    auto r=transfer;
    connect(r,&QNetworkReply::readyRead,this,[this,r,reserve]{
      if(!saveFile)return;
      QByteArray chunk=r->readAll();
      QStorageInfo disk(sharedPath());
      if(!disk.isValid()||disk.bytesAvailable()<reserve ||
         saveFile->write(chunk)!=chunk.size()){
        r->abort();status="Download stopped: insufficient space or write error";return;
      }
      received+=chunk.size();update();
    });
    connect(r,&QNetworkReply::downloadProgress,this,[this](qint64,qint64 total){
      if(total>0)expected=total;
      update();
    });
    connect(r,&QNetworkReply::finished,this,[this,r]{
      if(transfer!=r){r->deleteLater();return;}
      if(saveFile && r->bytesAvailable()){
        QByteArray chunk=r->readAll();
        if(saveFile->write(chunk)!=chunk.size()){r->abort();status="Download write failed";}
        else received+=chunk.size();
      }
      bool good=r->error()==QNetworkReply::NoError && saveFile &&
                (expected<=0||received==expected);
      if(good)good=saveFile->commit();
      if(saveFile && !good)saveFile->cancelWriting();
      saveFile.reset();transfer=nullptr;
      if(!good && status.isEmpty())status="Download failed or incomplete";
      if(good)status="Saved to Files / Internet Archive / "+QFileInfo(downloadPath).fileName();
      r->deleteLater();page=FILES;update();
    });
    update();
  }
  void cancelDownload() {
    if(transfer){transfer->disconnect(this);transfer->abort();transfer->deleteLater();transfer=nullptr;}
    if(saveFile){saveFile->cancelWriting();saveFile.reset();}
    page=FILES;status="Download canceled, partial file removed";update();
  }
  void back(){
    if(page==DOWNLOADING){cancelDownload();return;}
    if(api)abortApi();
    status.clear();
    if(page==HOME){close();return;}
    if(page==BROWSE){page=HOME;collectionId.clear();}
    else if(page==ITEM){
      page=BROWSE;selected=browseSelected;gridStart=browseGridStart;
      requestVisibleCovers();
    }
    else if(page==FILES||page==INFO){page=ITEM;}
    else if(page==DOWNLOAD_CONFIRM)page=downloadReturn;
    update();
  }
  int activeFiles() const {return qMax(1,(content().height()-100)/58);}
  void navigate(int dx,int dy) {
    if(page==HOME){
      selected=qBound(0,selected+dx+dy*3,5);
    } else if(page==ITEM) {
      selected=qBound(0,selected+dx+dy,2);
    } else if(page==BROWSE){
      if(entries.isEmpty())return;
      selected=qBound(0,selected+dx+dy*3,entries.size()-1);
      if(selected<gridStart)gridStart=(selected/3)*3;
      if(selected>=gridStart+6)gridStart=(selected/3-1)*3;
      gridStart=qMax(0,gridStart);
      requestVisibleCovers();
      if(api==nullptr && !loadingMore && !hasMore &&
         status.startsWith("Could not load more"))hasMore=true;
      maybeLoadMore();
    } else if(page==FILES){
      selected=qBound(0,selected+dx+dy,qMax(0,files.size()-1));
      fileStart=(selected/activeFiles())*activeFiles();
      requestFilePreviews();
    }
    update();
  }
  void contextualControls() {
    if(page==HOME)controls={{READER_UP,"Move"},{READER_OK,"Open"},{READER_BACK,"Exit"}};
    else if(page==BROWSE)controls={{READER_UP,"Browse"},{READER_OK,"Open"},{READER_OPTIONS,"Sort"},{READER_BACK,"Back"}};
    else if(page==ITEM)controls={{READER_UP,"Action"},{READER_OK,"Select"},{READER_BACK,"Results"}};
    else if(page==FILES)controls={{READER_OK,"Open"},{READER_OPTIONS,"Download"},{READER_BACK,"Item"}};
    else if(page==DOWNLOAD_CONFIRM)controls={{READER_OK,"Download"},{READER_BACK,"Cancel"}};
    else if(page==DOWNLOADING)controls={{READER_BACK,"Cancel"}};
    else controls={{READER_BACK,"Return"}};
  }

protected:
  void draw(QPainter &p,const QRect &r) override {
    p.fillRect(r,bg);
    contextualControls();
    QRect area=r.adjusted(8,8,-8,-8);
    if(page==HOME) {
      const QStringList sub={
        "Music, radio & recordings","Books, magazines & documents",
        "Photos, art & illustrations","Computer history","Curated archives",
        "Saved items"
      };
      const QColor tileColors[]={
        QColor("#00bfff"),QColor("#ffd000"),QColor("#bf70ff"),
        QColor("#00df40"),QColor("#ffffff"),QColor("#ff5050")
      };
      const int gap=12;
      const int cw=(area.width()-2*gap)/3;
      const int ch=(area.height()-gap)/2;
      for(int i=0;i<6;++i) {
        QRect box(area.x()+(i%3)*(cw+gap),area.y()+(i/3)*(ch+gap),cw,ch);
        p.fillRect(box,bg);
        p.setPen(QPen(i==selected?accent:outline,i==selected?3:1));
        p.drawRect(box.adjusted(1,1,-2,-2));
        QRect picture(box.x()+8,box.y()+8,box.width()-16,box.height()-69);
        p.setPen(QPen(tileColors[i],2));p.drawRect(picture.adjusted(1,1,-1,-1));
        int glyph=qMin(78,picture.height()-10);
        icon(p,QRect(picture.center().x()-glyph/2,picture.center().y()-glyph/2,
                     glyph,glyph),sections[i],tileColors[i]);
        txt(p,QRect(box.x()+12,box.bottom()-60,box.width()-24,34),
            sections[i],25,accent,Qt::AlignLeft|Qt::AlignVCenter,true);
        txt(p,QRect(box.x()+12,box.bottom()-29,box.width()-24,23),
            fitText(sub[i],19,box.width()-24),19,muted);
      }
    } else if(page==BROWSE) {
      // Full-width covers: the small display has no room for a website sidebar.
      txt(p,QRect(area.x()+4,area.y(),area.width()-190,41),
          collectionId.isEmpty()?category:category+" collection",29,ink,
          Qt::AlignLeft|Qt::AlignVCenter,true);
      QString sort=sortIndex==0?"Popular":"Recent";
      txt(p,QRect(area.right()-189,area.y()+3,180,36),
          sort+" · "+QString::number(entries.size())+(loadingMore?" · Loading...":""),18,muted,
          Qt::AlignRight|Qt::AlignVCenter);
      p.setPen(QPen(outline));p.drawLine(area.x(),area.y()+45,area.right(),area.y()+45);
      int gap=12;
      int cw=(area.width()-2*gap)/3;
      int top=area.y()+55;
      int ch=(area.bottom()-top-gap)/2;
      for(int i=gridStart;i<qMin(gridStart+6,entries.size());++i){
        int j=i-gridStart;
        QRect box(area.x()+(j%3)*(cw+gap),top+(j/3)*(ch+gap),cw,ch);
        const Entry &e=entries[i];
        card(p,box,e.title,e.creator.isEmpty()?e.date:e.creator,
             thumbs.value(e.id),i==selected,e.kind);
      }
      if(entries.isEmpty())txt(p,QRect(area.x(),top,area.width(),140),
          status.isEmpty()?"No items available":status,23,muted,Qt::AlignCenter);
    } else if(page==ITEM) {
      QRect left(area.x()+7,area.y()+9,235,area.height()-20);
      QRect right(left.right()+22,area.y()+2,area.right()-left.right()-28,area.height()-5);
      p.fillRect(left,bg);
      if(!thumbs.value(itemId).isNull())
        imageIn(p,left.adjusted(8,8,-8,-8),thumbs.value(itemId),false);
      else icon(p,QRect(left.center().x()-45,left.center().y()-45,90,90),"Collections",muted);
      txt(p,QRect(right.x(),right.y()+5,right.width(),76),itemTitle,30,ink,
          Qt::AlignLeft|Qt::AlignTop,true);
      txt(p,QRect(right.x(),right.y()+87,right.width(),28),
          creator+(itemDate.isEmpty()?"":"  ·  "+itemDate),18,muted);
      p.setPen(QPen(outline));p.drawLine(right.x(),right.y()+126,right.right(),right.y()+126);
      QStringList actions={"Browse available files",
        favorites.contains(itemId)?"Remove from favorites":"Add to favorites","About this item"};
      int y=right.y()+143;
      for(int i=0;i<actions.size();++i){
        QRect row(right.x(),y+i*47,right.width()-3,42);
        if(i==selected)p.fillRect(row,palette().highlight());
        p.setPen(QPen(i==selected?accent:outline));
        p.drawRect(row.adjusted(0,0,-1,-1));
        txt(p,row.adjusted(15,0,-10,0),
            QString(i==selected?"›  ":"   ")+actions[i],23,
            i==selected?palette().color(QPalette::HighlightedText):ink);
      }
      txt(p,QRect(right.x()+1,right.y()+298,right.width()-4,70),
          fitText(description,20,right.width()*3),20,muted,Qt::AlignLeft|Qt::AlignTop);
    } else if(page==FILES) {
      txt(p,QRect(area.x()+4,area.y(),area.width()-8,39),
          fitText(itemTitle,25,area.width()-12),25,ink,Qt::AlignLeft|Qt::AlignVCenter,true);
      p.fillRect(QRect(area.x(),area.y()+47,area.width(),32),bg);
      txt(p,QRect(area.x()+10,area.y()+47,area.width()-20,32),
          "Files and available formats",19,muted);
      auto layout=listInfo(p,area.adjusted(0,84,0,0),files.size(),selected,58);
      for(int i=layout.first;i<qMin(files.size(),layout.first+layout.visible);++i) {
        const auto row=listInfoRow(p,layout,i,selected);
        const auto foreground=palette().color(i==selected?QPalette::HighlightedText:QPalette::Text);
        const QRect preview(row.x()+7,row.y()+6,44,44);
        const QImage thumbnail=fileThumbs.contains(files[i].name)?fileThumbs.value(files[i].name):thumbs.value(itemId);
        if(!thumbnail.isNull())imageIn(p,preview,thumbnail,false);
        else icon(p,preview,isAudio(files[i].name)?"Audio":isText(files[i].name)?"Books":
                  isImage(files[i].name)?"Images":"Software",foreground);
        label(p,QRect(row.x()+61,row.y(),row.width()-69,29),files[i].name,23,i==selected,foreground);
        label(p,QRect(row.x()+61,row.y()+29,row.width()-69,25),files[i].format+" · "+human(files[i].size),18,false,foreground);
      }
      if(selected>=0 && selected<files.size()) {
        const auto &file=files[selected];const auto side=layout.info;
        const QImage preview=fileThumbs.contains(file.name)?fileThumbs.value(file.name):thumbs.value(itemId);
        const QRect picture(side.x(),side.y(),side.width(),qMin(160,side.height()/2));
        if(!preview.isNull())imageIn(p,picture,preview,false);
        else {
          const int glyph=qMin(96,qMin(picture.width(),picture.height())-12);
          icon(p,QRect(picture.center()-QPoint(glyph/2,glyph/2),QSize(glyph,glyph)),
               isAudio(file.name)?"Audio":isText(file.name)?"Books":isImage(file.name)?"Images":"Software",accent);
        }
        label(p,QRect(side.x(),picture.bottom()+12,side.width(),34),file.name,24,true,ink);
        text(p,QRect(side.x(),picture.bottom()+55,side.width(),110),file.format+"\n"+human(file.size),22,ink);
        buttonIcon(p,QRect(side.x(),side.bottom()-42,28,28),READER_OK);
        label(p,QRect(side.x()+38,side.bottom()-44,side.width()-38,32),"Open in default viewer",19,false,accent);
      }
      if(files.isEmpty())txt(p,area,loaded?"No files listed":"Loading files...",22,muted,Qt::AlignCenter);
    } else if(page==DOWNLOAD_CONFIRM||page==DOWNLOADING) {
      QRect dialog=area.adjusted(120,35,-120,-35);
      p.fillRect(dialog,bg);
      p.setPen(QPen(outline));p.drawRect(dialog.adjusted(0,0,-1,-1));
      p.fillRect(QRect(dialog.x(),dialog.y(),dialog.width(),45),bg);
      txt(p,QRect(dialog.x()+18,dialog.y()+3,dialog.width()-36,38),
          page==DOWNLOAD_CONFIRM?"Download file":"Downloading file",25,ink);
      txt(p,QRect(dialog.x()+22,dialog.y()+66,dialog.width()-44,60),downloadFile.name,21,ink);
      txt(p,QRect(dialog.x()+22,dialog.y()+137,dialog.width()-44,34),
          page==DOWNLOAD_CONFIRM?human(downloadFile.size):
          human(received)+" / "+human(expected),19,muted);
      if(page==DOWNLOAD_CONFIRM)
        txt(p,QRect(dialog.x()+22,dialog.y()+188,dialog.width()-44,65),
            "Save to Files / Internet Archive. No files are stored by browsing.",18,muted);
      else {
        QRect bar(dialog.x()+22,dialog.y()+212,dialog.width()-44,13);
        p.fillRect(bar,outline);
        if(expected>0)p.fillRect(QRect(bar.x(),bar.y(),
          int(bar.width()*qMin(1.0,double(received)/expected)),bar.height()),accent);
      }
    } else if(page==INFO) {
      txt(p,QRect(area.x()+14,area.y()+10,area.width()-28,36),
          "About this item",24,ink,Qt::AlignLeft|Qt::AlignVCenter,true);
      p.setPen(QPen(outline));p.drawLine(area.x()+14,area.y()+55,area.right()-14,area.y()+55);
      txt(p,area.adjusted(18,70,-18,-4),
          description.isEmpty()?"No description provided.":description,22,ink,
          Qt::AlignLeft|Qt::AlignTop);
    }
    if(!status.isEmpty()&&
       page!=DOWNLOAD_CONFIRM&&page!=DOWNLOADING) {
      QRect note(area.x()+4,area.bottom()-30,area.width()-8,30);
      p.fillRect(note,Qt::black);
      txt(p,note.adjusted(8,0,-8,0),fitText(status,15,note.width()-16),15,QColor("#ffd000"));
    }
  }

  void button(reader_button key,int state) override {
    if(state!=1 && state!=2)return;
    if(key==READER_FAVORITES&&state==1){close();return;}
    if(key==READER_BACK&&state==1){back();return;}
    if(page==DOWNLOADING)return;
    if(key==READER_LEFT)navigate(-1,0);
    else if(key==READER_RIGHT)navigate(1,0);
    else if(key==READER_UP)navigate(0,-1);
    else if(key==READER_DOWN)navigate(0,1);
    if(key==READER_OPTIONS&&state==1){
      if(page==BROWSE&&category!="Favorites"){sortIndex=1-sortIndex;search();}
      else if(page==FILES&&selected>=0&&selected<files.size()){selectedFile=files[selected];downloadSelected();}
      return;
    }
    if(key!=READER_OK||state!=1||api)return;
    if(page==HOME){
      category=sections.value(selected);collectionId.clear();searchPage=1;search();
    } else if(page==BROWSE)openItem(selected);
    else if(page==ITEM) {
      if(selected==0) {
        auto it=std::find_if(entries.begin(),entries.end(),[this](const Entry&e){return e.id==itemId;});
        if(it!=entries.end()&&it->kind=="collection"){
          collectionId=itemId;category=itemTitle;searchPage=1;search();
        } else {
          page=FILES;selected=0;fileStart=0;
          requestVisibleCovers();update();
        }
      } else if(selected==1) {
        if(favorites.contains(itemId))favorites.removeAll(itemId);
        else if(favorites.size()<200)favorites<<itemId;
        storeFavorites();update();
      } else {page=INFO;update();}
    } else if(page==FILES && selected<files.size())openFile(files[selected]);
    else if(page==DOWNLOAD_CONFIRM)beginDownload();
  }
public:
  Archive():Screen("Internet Archive") {
    showHeader=false;
    bg=palette().color(QPalette::Window);panel=palette().color(QPalette::Base);
    outline=palette().color(QPalette::Mid);ink=palette().color(QPalette::Text);
    muted=ink;accent=palette().color(QPalette::Link);blue=accent;
    loadFavorites();loaded=true;
  }
  ~Archive() override {
    abortApi();abortCovers();
    for(auto job:fileThumbJobs){job->disconnect(this);job->abort();job->deleteLater();}
    if(transfer)transfer->abort();
    if(saveFile)saveFile->cancelWriting();
  }
};
int main(int argc,char **argv) {
  QApplication app(argc,argv);app.setFont(QFont("DejaVu Sans"));
  Archive window;window.show();return app.exec();
}

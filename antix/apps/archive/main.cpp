#include "reader-ui.hpp"
#include "audio.hpp"
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
const QColor bg("#111b28"), panel("#1d2b3b"), outline("#385166");
const QColor ink("#f4efdf"), muted("#a7b6c4"), accent("#f6bb6c"), blue("#68cbd0");
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
QString clockString(int s) {return QString("%1:%2").arg(qMax(0,s)/60).arg(qMax(0,s)%60,2,10,QChar('0'));}
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
  s=s.toLower();return s.endsWith(".mp3");
}
}
struct Entry {QString id,title,creator,date,kind;};
struct File {QString name,format; qint64 size=0; int seconds=0;};
class Archive : public Screen {
  enum Page {HOME, BROWSE, ITEM, FILES, VIEW, READER, PLAYER, DOWNLOAD_CONFIRM, DOWNLOADING, INFO};
  Page page=HOME, downloadReturn=FILES;
  QNetworkAccessManager network;
  QNetworkReply *api=nullptr, *transfer=nullptr;
  QMap<QString,QImage> thumbs;
  QList<QNetworkReply*> coverJobs;
  QSet<QString> coverPending, failedCovers;
  QList<Entry> entries;
  QList<File> files;
  QStringList favorites;
  QString category, collectionId, itemId, itemTitle, creator, itemDate, description, status;
  QString currentFile, currentFormat, bookText;
  int selected=0, gridStart=0, fileStart=0, searchPage=1, sortIndex=0;
  int imageZoom=1, imageX=0, imageY=0, fontSize=24, lineStart=0;
  QImage viewedImage;
  QStringList bookLines;
  File selectedFile, downloadFile;
  std::unique_ptr<QSaveFile> saveFile;
  QString downloadPath;
  qint64 received=0, expected=0;
  ArchiveAudio *audio=nullptr;
  QElapsedTimer audioTimer;
  QTimer playbackTick;
  int audioOffset=0, audioDuration=0;
  bool paused=false, loaded=false;
  QStringList sections={"Audio","Books","Images","Software","Collections","Favorites"};
  QStringList types={"audio","texts","image","software","collection",""};
  QString storagePath() const {return qEnvironmentVariable("READER_APP_DATA_DIR","/var/lib/reader/apps/reread-archive");}
  QString sharedPath() const {return qEnvironmentVariable("REREAD_FILES_DIR","/var/lib/reader/files");}
  QColor color(bool active) const {return active?accent:ink;}
  void txt(QPainter &p,const QRect &r,const QString &str,int px=23,QColor c=ink,
           int align=Qt::AlignLeft|Qt::AlignVCenter,bool bold=false) const {
    QFont f=p.font();f.setPixelSize(px);f.setBold(bold);p.setFont(f);
    p.setPen(c);
    p.drawText(r,align|Qt::TextWordWrap,str);
  }
  QString fitText(const QString &s,int px,int width) const {
    QFont f=font();f.setPixelSize(px);
    return QFontMetrics(f).elidedText(s,Qt::ElideRight,qMax(10,width));
  }
  void round(QPainter &p,const QRect &r,QColor c,int radius=13) const {
    p.setPen(Qt::NoPen);p.setBrush(c);p.drawRoundedRect(r,radius,radius);
  }
  void icon(QPainter &p,const QRect &rect,const QString &kind,QColor c=accent) const {
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
    round(p,r,active?QColor("#31465a"):panel);
    if(active){p.setBrush(Qt::NoBrush);p.setPen(QPen(accent,3));p.drawRoundedRect(r.adjusted(1,1,-1,-1),13,13);}
    QRect cover=r.adjusted(7,7,-7,-61);round(p,cover,QColor("#2a3e52"),8);
    if(!img.isNull()) imageIn(p,cover,img,true);
    else icon(p,QRect(cover.center().x()-34,cover.center().y()-34,68,68),symbol,QColor("#88abc2"));
    txt(p,QRect(r.x()+12,r.bottom()-52,r.width()-24,29),fitText(name,21,r.width()-25),21,ink,Qt::AlignLeft|Qt::AlignVCenter,true);
    txt(p,QRect(r.x()+12,r.bottom()-25,r.width()-24,21),fitText(sub,16,r.width()-24),16,muted);
  }
  int elapsed() const {return audioOffset+(audio && !paused && audioTimer.isValid()?int(audioTimer.elapsed()/1000):0);}
  void abortApi() {if(api){api->disconnect(this);api->abort();api->deleteLater();api=nullptr;}}
  void abortCovers() {
    for(auto r:coverJobs){r->disconnect(this);r->abort();r->deleteLater();}
    coverJobs.clear();coverPending.clear();
  }
  void stopAudio() {
    if(audio) {
      ArchiveAudio *worker=audio;
      audio=nullptr;
      worker->stop();
      worker->wait();
      worker->deleteLater();
    }
    audioTimer.invalidate();
    paused=false;
    playbackTick.stop();
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
    abortApi();loaded=false;showStatus("Loading from archive.org...");
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
      if(!ok){showStatus("Unable to load: "+problem);return;}
      status.clear();
      if(kind=="image") {
        QBuffer buffer(&bytes);buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);reader.setAutoTransform(true);
        QSize native=reader.size();
        if(native.isValid()&&(native.width()>2048||native.height()>2048))
          reader.setScaledSize(native.scaled(2048,2048,Qt::KeepAspectRatio));
        viewedImage=reader.read();
        if(viewedImage.isNull())status="Image couldn't be decoded. You can download the original.";
      } else if(kind=="text") {
        // Keep text in memory, not in device Files. Pre-layout for fast page turns.
        bookText=QString::fromUtf8(bytes);
        if(bookText.startsWith(QChar(0xfeff)))bookText.remove(0,1);
        lineStart=0;layoutBook();
      } else {
        QJsonParseError err;
        auto doc=QJsonDocument::fromJson(bytes,&err);
        if(err.error!=QJsonParseError::NoError||!doc.isObject()){showStatus("Invalid Archive response");return;}
        auto root=doc.object();
        if(kind=="search") {
          entries.clear();gridStart=0;selected=0;
          for(auto entry:root.value("response").toObject().value("docs").toArray()){
            auto o=entry.toObject();
            Entry e{value(o.value("identifier")),plain(value(o.value("title"))),
                    plain(value(o.value("creator"))),value(o.value("year")),value(o.value("mediatype"))};
            if(e.id.isEmpty())continue;
            if(e.title.isEmpty())e.title=e.id;
            entries<<e;
          }
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
    if(page==ITEM||page==FILES||page==PLAYER||page==VIEW){requestCover(itemId);return;}
    if(page!=BROWSE)return;
    for(int i=gridStart;i<qMin(gridStart+6,entries.size()) && coverJobs.size()<4;++i)
      requestCover(entries[i].id);
  }
  void search() {
    page=BROWSE;selected=0;gridStart=0;entries.clear();abortCovers();thumbs.clear();failedCovers.clear();
    if(category=="Favorites"){
      loaded=true;status.clear();
      for(const auto &id:favorites)entries<<Entry{id,id,"Saved item","",""};
      requestVisibleCovers();update();return;
    }
    QString type=types.value(sections.indexOf(category),"audio");
    QString query=collectionId.isEmpty()?"mediatype:"+type:"collection:"+collectionId;
    QUrl url("https://archive.org/advancedsearch.php");
    QUrlQuery q;
    q.addQueryItem("q",query);q.addQueryItem("output","json");
    for(const auto &f:{"identifier","title","creator","year","mediatype"})q.addQueryItem("fl[]",f);
    q.addQueryItem("rows","24");q.addQueryItem("page",QString::number(searchPage));
    q.addQueryItem("sort[]",sortIndex==0?"downloads desc":"publicdate desc");
    url.setQuery(q);fetch(url,"search");
  }
  void openItem(int index) {
    if(index<0||index>=entries.size())return;
    const auto e=entries[index];itemId=e.id;itemTitle=e.title;creator=e.creator;itemDate=e.date;
    description.clear();files.clear();selected=0;page=ITEM;requestCover(itemId);
    fetch(QUrl("https://archive.org/metadata/"+QString::fromUtf8(QUrl::toPercentEncoding(itemId))),"metadata",12*1024*1024);
  }
  void openFile(const File &f) {
    selectedFile=f;currentFile=f.name;currentFormat=f.format;
    imageZoom=1;imageX=0;imageY=0;viewedImage=QImage();bookText.clear();bookLines.clear();
    if(isAudio(f.name)){page=PLAYER;audioOffset=0;audioDuration=f.seconds;startAudio();return;}
    if(isImage(f.name)){page=VIEW;fetch(fileUrl(itemId,f.name),"image",14*1024*1024);return;}
    if(isText(f.name)){page=READER;fetch(fileUrl(itemId,f.name),"text",7*1024*1024);return;}
    status="No in-app viewer for this file type. Options can download it.";update();
  }
  void layoutBook() {
    bookLines.clear();int px=fontSize;
    QFont f=font();f.setFamily("DejaVu Serif");f.setPixelSize(px);
    QFontMetrics fm(f);
    int width=qMax(100,safeArea().width()-100);
    QString input=bookText;
    input.replace("\r\n","\n");
    for(const auto &paragraph:input.split('\n')){
      QString remaining=paragraph.trimmed();
      if(remaining.isEmpty()){bookLines<<"";continue;}
      while(!remaining.isEmpty()){
        int n=remaining.size();
        // Bounded binary search avoids quadratic wrapping on long lines.
        int low=1,high=n;
        while(low<high){int middle=(low+high+1)/2;
          if(fm.horizontalAdvance(remaining.left(middle))<=width)low=middle;
          else high=middle-1;}
        n=low;
        if(n<remaining.size()){int split=remaining.lastIndexOf(' ',n);
          if(split>0)n=split;}
        bookLines<<remaining.left(n).trimmed();
        remaining=remaining.mid(n).trimmed();
        if(bookLines.size()>40000){bookLines<<"[Text too long to display completely]";return;}
      }
    }
  }
  void startAudio(){
    stopAudio();
    status.clear();
    if(!currentFile.toLower().endsWith(".mp3")){
      status="Only MP3 is supported for streaming; other formats can be downloaded.";
      update();
      return;
    }
    // Estimate byte offset from known duration and size for a small,
    // approximate network seek. Beginning playback starts at byte zero.
    qint64 offset=0;
    if(audioOffset>0 && audioDuration>0 && selectedFile.size>0)
      offset=qMax(qint64(0),selectedFile.size*qint64(audioOffset)/audioDuration-4096);
    auto worker=new ArchiveAudio(fileUrl(itemId,currentFile),offset,this,
      [this](const QString &message){status=message;update();});
    audio=worker;
    connect(worker,&QThread::finished,this,[this,worker]{
      if(audio==worker){audio=nullptr;audioTimer.invalidate();playbackTick.stop();}
      worker->deleteLater();
      update();
    });
    paused=false;
    audioTimer.start();
    worker->start();
    playbackTick.start(900);
    update();
  }
  void toggleAudio(){
    if(!audio){startAudio();return;}
    if(paused){
      paused=false;
      audio->setPaused(false);
      audioTimer.start();
    } else {
      audioOffset=elapsed();
      paused=true;
      audio->setPaused(true);
      audioTimer.invalidate();
    }
    update();
  }
  void seek(int seconds){
    int position=qMax(0,elapsed()+seconds);
    if(audioDuration>0)position=qMin(position,qMax(0,audioDuration-1));
    audioOffset=position;
    startAudio();
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
    else if(page==ITEM){page=BROWSE;requestVisibleCovers();}
    else if(page==FILES||page==INFO){page=ITEM;}
    else if(page==VIEW||page==READER||page==PLAYER){stopAudio();page=FILES;}
    else if(page==DOWNLOAD_CONFIRM)page=downloadReturn;
    update();
  }
  int activeFiles() const {return qMax(1,(safeArea().height()-140)/58);}
  void navigate(int dx,int dy) {
    if(page==HOME){
      selected=qBound(0,selected+dx+dy*3,5);
    } else if(page==BROWSE){
      if(entries.isEmpty())return;
      selected=qBound(0,selected+dx+dy*3,entries.size()-1);
      if(selected<gridStart)gridStart=(selected/3)*3;
      if(selected>=gridStart+6)gridStart=(selected/3-1)*3;
      gridStart=qMax(0,gridStart);
      requestVisibleCovers();
    } else if(page==FILES){
      selected=qBound(0,selected+dx+dy,qMax(0,files.size()-1));
      if(selected<fileStart)fileStart=selected;
      if(selected>=fileStart+activeFiles())fileStart=selected-activeFiles()+1;
    } else if(page==VIEW){
      if(imageZoom>1){imageX+=dx*90;imageY+=dy*90;}
    } else if(page==READER){
      int lines=qMax(1,(safeArea().height()-120)/(fontSize+11));
      lineStart=qBound(0,lineStart+(dx+dy)*lines,qMax(0,bookLines.size()-lines));
    }
    update();
  }
  void paintChrome(QPainter &p,const QRect &safe) {
    txt(p,QRect(safe.x()+10,safe.y(),safe.width()-20,45),"INTERNET  ARCHIVE",30,ink,
        Qt::AlignLeft|Qt::AlignVCenter,true);
    txt(p,QRect(safe.right()-235,safe.y()+4,225,37),
      page==HOME?"EXPLORE":page==BROWSE?category.toUpper():page==ITEM?"ITEM":
      page==FILES?"FILES":page==VIEW?"IMAGE":page==READER?"READER":
      page==PLAYER?"LISTEN":page==DOWNLOADING?"DOWNLOADING":"ARCHIVE",
      17,accent,Qt::AlignRight|Qt::AlignVCenter,true);
    p.setPen(QPen(outline,1));p.drawLine(safe.x()+8,safe.y()+49,safe.right()-8,safe.y()+49);
    int bottom=safe.bottom()-46;
    p.drawLine(safe.x()+8,bottom,safe.right()-8,bottom);
    QString shortcuts=page==HOME?"ARROWS  Browse      OK  Open      BACK  Home":
      page==BROWSE?"ARROWS  Explore      OK  Open      PLAY  Next page      OPTIONS  Sort":
      page==ITEM?"OK  Open      BACK  Browse":
      page==FILES?"OK  View / listen      OPTIONS  Download      BACK  Item":
      page==VIEW?"ZOOM +/-  Scale      ARROWS  Pan      OPTIONS  Download":
      page==READER?"UP/DOWN  Pages      ZOOM +/-  Font      OPTIONS  Download":
      page==PLAYER?"PLAY / OK  Pause      LEFT/RIGHT  Seek      OPTIONS  Download":
      page==DOWNLOAD_CONFIRM?"OK  Download      BACK  Cancel":
      page==DOWNLOADING?"BACK  Cancel download":
      "BACK  Return";
    txt(p,QRect(safe.x()+12,bottom+3,safe.width()-20,38),shortcuts,17,muted);
  }
protected:
  void draw(QPainter &p,const QRect &r) override {
    p.fillRect(r,bg);
    const QRect safe=r;
    paintChrome(p,safe);
    QRect body=safe.adjusted(8,61,-8,-54);
    if(page==HOME) {
      txt(p,QRect(body.x()+10,body.y(),body.width(),38),"Explore the collections",27,ink,Qt::AlignLeft|Qt::AlignVCenter,true);
      int top=body.y()+49, gap=13, cellW=(body.width()-gap*2)/3,cellH=(body.height()-58-gap)/2;
      for(int i=0;i<6;++i) {
        QRect c(body.x()+(i%3)*(cellW+gap),top+(i/3)*(cellH+gap),cellW,cellH);
        round(p,c,i==selected?QColor("#334b5c"):panel);
        if(i==selected){p.setBrush(Qt::NoBrush);p.setPen(QPen(accent,3));p.drawRoundedRect(c.adjusted(1,1,-1,-1),13,13);}
        icon(p,QRect(c.x()+19,c.y()+20,80,80),sections[i]);
        txt(p,QRect(c.x()+111,c.y()+20,c.width()-124,42),sections[i],25,ink,Qt::AlignLeft|Qt::AlignVCenter,true);
        QString sub=QStringList{"Recordings & radio","Books & magazines","Photography & art","Historical programs","Curated archives","Your saved items"}[i];
        txt(p,QRect(c.x()+111,c.y()+62,c.width()-124,48),sub,17,muted);
      }
    } else if(page==BROWSE) {
      txt(p,QRect(body.x()+4,body.y(),body.width(),33),
          (collectionId.isEmpty()?category:"Collection: "+category)+
          (sortIndex==0?"    ·    Most downloaded":"    ·    Recently added"),21,muted);
      if(entries.isEmpty())txt(p,body,status.isEmpty()?"Nothing here yet":status,25,muted,Qt::AlignCenter);
      int top=body.y()+39,gap=11,cw=(body.width()-2*gap)/3,ch=(body.height()-45-gap)/2;
      for(int i=gridStart;i<qMin(gridStart+6,entries.size());++i){
        int j=i-gridStart;QRect c(body.x()+(j%3)*(cw+gap),top+(j/3)*(ch+gap),cw,ch);
        const Entry &e=entries[i];
        card(p,c,e.title,e.creator.isEmpty()?(e.date.isEmpty()?e.kind:e.date):e.creator,
             thumbs.value(e.id),i==selected,e.kind);
      }
      txt(p,QRect(body.right()-175,body.y(),170,30),
          QString("%1–%2 / %3").arg(entries.isEmpty()?0:gridStart+1)
            .arg(qMin(gridStart+6,entries.size())).arg(entries.size()),16,accent,Qt::AlignRight);
    } else if(page==ITEM) {
      QRect cover(body.x()+12,body.y()+12,245,body.height()-22);
      round(p,cover,panel);
      if(!thumbs.value(itemId).isNull())imageIn(p,cover.adjusted(8,8,-8,-8),thumbs.value(itemId),false);
      else icon(p,QRect(cover.center().x()-50,cover.center().y()-50,100,100),"Collections");
      int x=cover.right()+25,w=body.right()-x-5;
      txt(p,QRect(x,body.y()+9,w,75),itemTitle,29,ink,Qt::AlignLeft|Qt::AlignTop,true);
      txt(p,QRect(x,body.y()+93,w,30),creator+(itemDate.isEmpty()?"":"  ·  "+itemDate),20,muted);
      txt(p,QRect(x,body.y()+133,w,80),fitText(description,19,w*3),19,muted,Qt::AlignLeft|Qt::AlignTop);
      QStringList actions={collectionId.isEmpty()&&
          std::any_of(entries.begin(),entries.end(),[this](const Entry &e){return e.id==itemId&&e.kind=="collection";})
          ?"Browse collection":"Browse files",favorites.contains(itemId)?"Remove favorite":"Add favorite","About this item"};
      for(int i=0;i<actions.size();++i){
        QRect b(x,body.y()+230+i*48,w-4,41);
        round(p,b,i==selected?QColor("#4d5755"):panel,7);
        txt(p,b.adjusted(15,0,-12,0),(i==selected?"›  ":"   ")+actions[i],21,i==selected?accent:ink);
      }
      if(!loaded)txt(p,QRect(x,body.bottom()-29,w,25),"Loading item details...",17,muted);
    } else if(page==FILES) {
      txt(p,QRect(body.x()+7,body.y(),body.width(),39),fitText(itemTitle,24,body.width()-15),24,ink,Qt::AlignLeft|Qt::AlignVCenter,true);
      int top=body.y()+46,step=58,visible=qMax(1,(body.bottom()-top)/step);
      for(int i=fileStart;i<qMin(files.size(),fileStart+visible);++i) {
        QRect row(body.x()+4,top+(i-fileStart)*step,body.width()-8,step-5);
        round(p,row,i==selected?QColor("#3b5063"):panel,8);
        icon(p,QRect(row.x()+7,row.y()+5,45,43),
             isAudio(files[i].name)?"Audio":isText(files[i].name)?"Books":
             isImage(files[i].name)?"Images":"Software",i==selected?accent:muted);
        txt(p,QRect(row.x()+62,row.y()+4,row.width()-280,26),
            fitText(files[i].name,21,row.width()-285),21,ink);
        txt(p,QRect(row.x()+62,row.y()+30,row.width()-280,20),
            fitText(files[i].format,15,row.width()-285),15,muted);
        txt(p,QRect(row.right()-205,row.y(),190,row.height()),human(files[i].size),18,muted,Qt::AlignRight|Qt::AlignVCenter);
      }
      if(files.isEmpty())txt(p,body,loaded?"No available files":"Loading file list...",23,muted,Qt::AlignCenter);
      txt(p,QRect(body.right()-130,body.y()+6,122,32),QString::number(files.size())+" files",17,accent,Qt::AlignRight);
    } else if(page==VIEW) {
      QRect view=body.adjusted(0,5,0,-5);
      if(viewedImage.isNull())txt(p,view,status.isEmpty()?"Loading image...":status,22,muted,Qt::AlignCenter);
      else {
        QSize sz=viewedImage.size();sz.scale(view.size(),Qt::KeepAspectRatio);
        sz*=imageZoom;
        QRect dest(QPoint(0,0),sz);dest.moveCenter(view.center()+QPoint(imageX,imageY));
        p.save();p.setClipRect(view);p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(dest,viewedImage);p.restore();
        round(p,QRect(view.right()-110,view.y()+8,99,32),QColor("#25384b"));
        txt(p,QRect(view.right()-102,view.y()+10,88,28),QString::number(imageZoom)+"× zoom",18,ink,Qt::AlignCenter);
      }
    } else if(page==READER) {
      round(p,body,QColor("#e7dfcb"));
      QRect pageRect=body.adjusted(38,14,-38,-16);
      QFont f=p.font();f.setFamily("DejaVu Serif");f.setPixelSize(fontSize);p.setFont(f);
      p.setPen(QColor("#25313d"));
      int step=fontSize+11,visible=qMax(1,pageRect.height()/step);
      for(int i=0;i<visible&&lineStart+i<bookLines.size();++i)
        p.drawText(pageRect.left(),pageRect.top()+fontSize+i*step,bookLines[lineStart+i]);
      txt(p,QRect(pageRect.right()-165,pageRect.bottom()-17,160,20),
          QString("%1 / %2").arg(qMin(lineStart+visible,bookLines.size())).arg(bookLines.size()),
          15,QColor("#536373"),Qt::AlignRight);
      if(bookLines.isEmpty())
        txt(p,pageRect,status.isEmpty()?"Loading text...":status,24,QColor("#334355"),Qt::AlignCenter);
    } else if(page==PLAYER) {
      QRect cover(body.x()+26,body.y()+20,body.height()-40,body.height()-40);
      round(p,cover,panel);
      if(!thumbs.value(itemId).isNull())imageIn(p,cover.adjusted(8,8,-8,-8),thumbs.value(itemId),true);
      else icon(p,cover.adjusted(70,70,-70,-70),"Audio");
      int x=cover.right()+35,w=body.right()-x-8;
      txt(p,QRect(x,body.y()+20,w,80),fitText(currentFile,28,w*2),28,ink,Qt::AlignLeft|Qt::AlignTop,true);
      txt(p,QRect(x,body.y()+102,w,34),fitText(itemTitle,20,w),20,muted);
      int progress=qMax(0,elapsed());
      txt(p,QRect(x,body.y()+163,w,40),audio?paused?"PAUSED":"NOW PLAYING":"STOPPED",20,accent,Qt::AlignLeft|Qt::AlignVCenter,true);
      QRect bar(x,body.y()+220,w,11);round(p,bar,QColor("#395165"),5);
      if(audioDuration>0)round(p,QRect(bar.x(),bar.y(),int(bar.width()*qMin(1.0,double(progress)/audioDuration)),bar.height()),accent,5);
      txt(p,QRect(x,body.y()+241,w,32),clockString(progress)+"     /     "+(audioDuration?clockString(audioDuration):"--:--"),21,ink);
      txt(p,QRect(x,body.y()+295,w,35),"⏯  Pause / resume     ◀▶  ±10 sec",19,muted);
      if(!status.isEmpty())txt(p,QRect(x,body.y()+340,w,30),status,17,accent);
    } else if(page==DOWNLOAD_CONFIRM||page==DOWNLOADING) {
      QRect box=body.adjusted(90,44,-90,-46);round(p,box,panel);
      QString title=page==DOWNLOAD_CONFIRM?"Save this file?":"Downloading";
      txt(p,QRect(box.x()+26,box.y()+22,box.width()-52,48),title,30,ink,Qt::AlignLeft|Qt::AlignVCenter,true);
      txt(p,QRect(box.x()+26,box.y()+79,box.width()-52,65),downloadFile.name,22,ink);
      txt(p,QRect(box.x()+26,box.y()+145,box.width()-52,35),
          page==DOWNLOAD_CONFIRM?human(downloadFile.size):
          human(received)+" / "+human(expected),21,muted);
      if(page==DOWNLOAD_CONFIRM)
        txt(p,QRect(box.x()+26,box.y()+210,box.width()-52,80),
            "Saves to Files / Internet Archive. Browsing never saves media.",20,muted);
      else {
        QRect bar=box.adjusted(26,223,-26,-77);bar.setHeight(16);round(p,bar,outline,6);
        if(expected>0)round(p,QRect(bar.x(),bar.y(),int(bar.width()*qMin(1.0,double(received)/expected)),bar.height()),accent,6);
      }
    } else if(page==INFO) {
      txt(p,body,description.isEmpty()?"No description provided.":description,24,ink,Qt::AlignLeft|Qt::AlignTop);
    }
    if(!status.isEmpty() && page!=VIEW && page!=READER && page!=PLAYER &&
       page!=DOWNLOAD_CONFIRM && page!=DOWNLOADING){
      round(p,QRect(body.x()+10,body.bottom()-42,body.width()-20,36),QColor("#314258"),7);
      txt(p,QRect(body.x()+20,body.bottom()-42,body.width()-40,36),
          fitText(status,17,body.width()-40),17,accent);
    }
  }
  void button(reader_button key,int state) override {
    if(state!=1 && state!=2)return;
    if(key==READER_FAVORITES&&state==1){stopAudio();close();return;}
    if(key==READER_BACK&&state==1){back();return;}
    if(page==DOWNLOADING)return;
    if(page==PLAYER) {
      if((key==READER_PLAY||key==READER_OK)&&state==1)toggleAudio();
      else if(key==READER_LEFT&&state==1)seek(-10);
      else if(key==READER_RIGHT&&state==1)seek(10);
    } else if(page==READER) {
      if(key==READER_UP)navigate(0,-1);
      else if(key==READER_DOWN)navigate(0,1);
      else if(key==READER_ZOOM_IN&&state==1){fontSize=qMin(36,fontSize+2);layoutBook();update();}
      else if(key==READER_ZOOM_OUT&&state==1){fontSize=qMax(18,fontSize-2);layoutBook();update();}
    } else if(page==VIEW) {
      if(key==READER_ZOOM_IN&&state==1){imageZoom=qMin(6,imageZoom+1);update();}
      else if(key==READER_ZOOM_OUT&&state==1){imageZoom=qMax(1,imageZoom-1);if(imageZoom==1)imageX=imageY=0;update();}
      else if(key==READER_LEFT)navigate(-1,0);
      else if(key==READER_RIGHT)navigate(1,0);
      else if(key==READER_UP)navigate(0,-1);
      else if(key==READER_DOWN)navigate(0,1);
    } else {
      if(key==READER_LEFT)navigate(-1,0);
      else if(key==READER_RIGHT)navigate(1,0);
      else if(key==READER_UP)navigate(0,-1);
      else if(key==READER_DOWN)navigate(0,1);
    }
    if(key==READER_OPTIONS&&state==1){
      if(page==BROWSE&&category!="Favorites"){sortIndex=1-sortIndex;search();}
      else if(page==FILES&&selected>=0&&selected<files.size()){selectedFile=files[selected];downloadSelected();}
      else if(page==VIEW||page==READER||page==PLAYER)downloadSelected();
      return;
    }
    if(key==READER_PLAY&&state==1&&page==BROWSE&&category!="Favorites"){
      ++searchPage;search();return;
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
        } else {page=FILES;selected=0;fileStart=0;update();}
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
    showHeader=false;showControls=false;
    loadFavorites();loaded=true;
    connect(&playbackTick,&QTimer::timeout,this,[this]{if(page==PLAYER)update();});
  }
  ~Archive() override {abortApi();abortCovers();stopAudio();if(transfer)transfer->abort();if(saveFile)saveFile->cancelWriting();}
};
int main(int argc,char **argv) {
  qputenv("QT_QPA_FB_DISABLE_INPUT","1");
  QApplication app(argc,argv);app.setFont(QFont("DejaVu Sans"));
  Archive window;window.showFullScreen();return app.exec();
}

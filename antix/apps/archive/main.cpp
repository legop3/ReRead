#include "reader-ui.hpp"
#include <QtNetwork>
#include <QtCore>
#include <QtWidgets>
#include <algorithm>
using namespace ReRead;

static QString field(const QJsonValue &v) {
  if (v.isString()) return v.toString();
  if (v.isArray()) { QStringList s; for (const auto &x:v.toArray()) if (x.isString()) s << x.toString(); return s.join(", "); }
  if (v.isDouble()) return QString::number(v.toDouble());
  return {};
}
static QString clean(QString x) {
  x.remove(QRegularExpression("<[^>]*>")); x.replace("&amp;","&"); x.replace("&quot;","\""); x.replace("&#39;","'");
  return x.simplified();
}
static QString urlFile(const QString &id, const QString &name) {
  return "https://archive.org/download/" + QString::fromUtf8(QUrl::toPercentEncoding(id)) + "/" +
    QString::fromUtf8(QUrl::toPercentEncoding(name, "/"));
}
struct Entry { QString id,title,creator,year,kind; };
struct Media { QString name,format; qint64 bytes=0; };

class Archive : public Screen {
  QNetworkAccessManager network;
  QNetworkReply *reply=nullptr;
  QProcess *video=nullptr, *audio=nullptr;
  QByteArray frameBytes, imageBytes;
  QImage picture;
  QString page="home", category, itemId, itemTitle, description, book, message, query;
  QStringList labels, details;
  QList<Entry> entries;
  QList<Media> files;
  QStringList favorites;
  int selected=0, resultPage=1, textOffset=0, frameCount=0;
  bool busy=false, playing=false;
  const QStringList kinds={"movies","audio","texts","image","software","etree"};
  const QStringList categories={"Videos","Audio","Books & texts","Images","Software","Live music","Favorites"};
  QString dataFile() const { return qEnvironmentVariable("READER_APP_DATA_DIR", "/var/lib/reader/apps/reread-archive") + "/favorites.json"; }
  void save() {
    QDir().mkpath(QFileInfo(dataFile()).absolutePath());
    QSaveFile f(dataFile());
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(QJsonArray::fromStringList(favorites)).toJson(QJsonDocument::Compact)); f.commit(); }
  }
  void stopPlayback() {
    playing=false;
    for (auto p:{video,audio}) if (p) { p->disconnect(this); p->kill(); p->deleteLater(); }
    video=nullptr;audio=nullptr; frameBytes.clear();
  }
  void request(const QUrl &url, const QString &purpose, int maximum=5*1024*1024) {
    if (reply) { reply->abort(); reply->deleteLater(); reply=nullptr; }
    busy=true; message="Loading from Internet Archive..."; refresh();
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent","ReRead-Archive/0.1 (+https://github.com/legop3/ReRead)");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    auto r=network.get(req); reply=r;
    connect(r,&QNetworkReply::readyRead,this,[this,r,maximum] {
      if (r->bytesAvailable()+r->property("readTotal").toLongLong()>maximum) r->abort();
      else r->setProperty("readTotal",r->property("readTotal").toLongLong()+r->bytesAvailable());
    });
    connect(r,&QNetworkReply::finished,this,[this,r,purpose,maximum] {
      if (reply!=r) { r->deleteLater();return; }
      reply=nullptr;busy=false;
      QByteArray bytes=r->readAll();
      bool ok=r->error()==QNetworkReply::NoError && bytes.size()<=maximum;
      QString error=r->errorString();r->deleteLater();
      if (!ok) {message="Network error or item too large: "+error;refresh();return;}
      message.clear();
      if (purpose=="image") {
        picture=QImage::fromData(bytes); if (picture.isNull()) message="Image format unavailable";
      } else if (purpose=="text") {
        book=QString::fromUtf8(bytes); textOffset=0;
      } else {
        QJsonParseError parse;
        QJsonDocument d=QJsonDocument::fromJson(bytes,&parse);
        if (parse.error!=QJsonParseError::NoError || !d.isObject()) message="Archive returned invalid metadata";
        else if (purpose=="search") {
          entries.clear();
          auto results=d.object().value("response").toObject().value("docs").toArray();
          for (const auto &v:results) {
            auto o=v.toObject(); Entry e;
            e.id=field(o.value("identifier"));e.title=clean(field(o.value("title")));
            e.creator=clean(field(o.value("creator")));e.year=field(o.value("year"));e.kind=field(o.value("mediatype"));
            if (!e.id.isEmpty()) {if(e.title.isEmpty())e.title=e.id;entries<<e;}
          }
        } else if (purpose=="metadata") {
          auto o=d.object(); auto m=o.value("metadata").toObject();
          itemTitle=clean(field(m.value("title")));description=clean(field(m.value("description")));
          files.clear();
          for (const auto &v:o.value("files").toArray()) {
            auto f=v.toObject(); Media a;
            a.name=field(f.value("name"));a.format=field(f.value("format"));
            a.bytes=field(f.value("size")).toLongLong();
            QString low=a.name.toLower();
            if (a.name.isEmpty() || low.endsWith(".xml") || low.endsWith(".sqlite") ||
                low.endsWith(".torrent") || low.endsWith(".json") || low.endsWith(".gz") ||
                low.endsWith(".zip") || low.endsWith(".7z") || low.endsWith(".rar") ||
                low.endsWith(".md5") || low.endsWith(".sha1")) continue;
            files<<a;
          }
          std::stable_sort(files.begin(),files.end(),[](const Media&a,const Media&b) {
            auto score=[](const Media&f) {
              QString s=f.name.toLower();
              if(s.endsWith(".mp3"))return 0;
              if(s.endsWith(".ogg"))return 1;
              if(s.endsWith(".mp4"))return 2;
              if(s.endsWith(".ogv"))return 3;
              if(s.endsWith("_djvu.txt") || s.endsWith(".txt"))return 4;
              if(s.endsWith(".jpg") || s.endsWith(".jpeg") || s.endsWith(".png"))return 5;
              return 6;
            };return score(a)<score(b);
          });
        }
      }
      refresh();
    });
  }
  void search() {
    if(category=="Favorites") {
      entries.clear();
      for(const auto &id:favorites) entries<<Entry{id,id,"","",""};
      page="results";selected=0;refresh();return;
    }
    QString kind=kinds.value(categories.indexOf(category),"movies");
    query="mediatype:"+kind+" AND (publicdate:[1900-01-01T00:00:00Z TO *])";
    QUrl u("https://archive.org/advancedsearch.php");
    QUrlQuery q;q.addQueryItem("q",query);q.addQueryItem("output","json");
    q.addQueryItem("fl[]","identifier");q.addQueryItem("fl[]","title");q.addQueryItem("fl[]","creator");
    q.addQueryItem("fl[]","year");q.addQueryItem("fl[]","mediatype");
    q.addQueryItem("rows","20");q.addQueryItem("page",QString::number(resultPage));
    q.addQueryItem("sort[]","downloads desc");u.setQuery(q);
    request(u,"search");
  }
  void loadItem(const QString &id) {
    itemId=id;itemTitle=id;description.clear();files.clear();picture=QImage();
    page="item";selected=0;
    request(QUrl("https://archive.org/metadata/"+QString::fromUtf8(QUrl::toPercentEncoding(id))),"metadata",9*1024*1024);
  }
  void loadFile(const Media &f) {
    stopPlayback();picture=QImage();book.clear();frameBytes.clear();textOffset=0;
    QString name=f.name.toLower();QUrl u(urlFile(itemId,f.name));
    if(name.endsWith(".txt") || name.endsWith(".log") || name.endsWith(".csv")) {
      page="reading";request(u,"text",6*1024*1024);return;
    }
    if(name.endsWith(".jpg") || name.endsWith(".jpeg") || name.endsWith(".png") || name.endsWith(".webp")) {
      page="image";request(u,"image",12*1024*1024);return;
    }
    if(name.endsWith(".mp3") || name.endsWith(".ogg") || name.endsWith(".flac") ||
       name.endsWith(".m4a") || name.endsWith(".wav") || name.endsWith(".opus") ||
       name.endsWith(".mp4") || name.endsWith(".ogv") || name.endsWith(".webm") ||
       name.endsWith(".mkv") || name.endsWith(".avi") || name.endsWith(".mpeg")) {
      bool hasVideo=name.endsWith(".mp4") || name.endsWith(".ogv") ||
        name.endsWith(".webm") || name.endsWith(".mkv") || name.endsWith(".avi") || name.endsWith(".mpeg");
      page="playing";playing=true;message.clear();frameCount=0;
      // FFmpeg streams directly to ALSA; it never writes media files.
      audio=new QProcess(this);
      connect(audio,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){message="FFmpeg could not start";refresh();});
      audio->start("ffmpeg",{"-nostdin","-loglevel","error","-i",u.toString(QUrl::FullyEncoded),
                             "-vn","-f","alsa","default:CARD=MID"});
      if(hasVideo) {
        video=new QProcess(this);
        connect(video,&QProcess::readyReadStandardOutput,this,[this] {
          frameBytes+=video->readAllStandardOutput();
          if(frameBytes.size()>4*1024*1024) {frameBytes.clear();return;}
          // Process complete JPEG frames from FFmpeg's MJPEG image pipe.
          while(true) {
            int start=frameBytes.indexOf(QByteArray::fromHex("ffd8"));
            if(start<0) {frameBytes.clear();break;}
            if(start)frameBytes.remove(0,start);
            int end=frameBytes.indexOf(QByteArray::fromHex("ffd9"),2);
            if(end<0)break;
            QImage next=QImage::fromData(frameBytes.left(end+2),"JPEG");
            if(!next.isNull()){picture=next;++frameCount;update();}
            frameBytes.remove(0,end+2);
          }
        });
        connect(video,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
                this,[this](int,QProcess::ExitStatus){if(playing){playing=false;refresh();}});
        video->start("ffmpeg",{"-nostdin","-loglevel","error","-i",u.toString(QUrl::FullyEncoded),
                               "-an","-vf","fps=5,scale=480:360:force_original_aspect_ratio=decrease",
                               "-q:v","7","-f","image2pipe","-vcodec","mjpeg","pipe:1"});
      } else {
        connect(audio,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
          this,[this](int,QProcess::ExitStatus){if(playing){playing=false;refresh();}});
      }
      refresh();return;
    }
    message="This format cannot be streamed on ReRead";refresh();
  }
  void refresh() {
    title=page=="home"?"Internet Archive":page=="results"?category:page=="item"?itemTitle:
          page=="files"?"Available files":page=="reading"?"Read":page=="image"?"Image":"Playing";
    subtitle=busy?"Loading...":message;
    controls={{READER_BACK,page=="home"?"Home":"Back"},{READER_UP,"Up"},{READER_DOWN,"Down"},{READER_OK,"Select"}};
    labels.clear();details.clear();
    if(page=="home")labels=categories;
    if(page=="results") {
      for(const auto &e:entries) {labels<<e.title;details<<(e.creator+(e.year.isEmpty()?"":" · "+e.year));}
      labels<<"Next page";details<<"Load more results";
    }
    if(page=="item") {
      labels<<"Browse files" <<(favorites.contains(itemId)?"Remove favorite":"Add favorite")<<"Description";
      details<<"Stream supported files · no downloads"<<"Stored as an identifier only"<<"Read item details";
    }
    if(page=="files")for(const auto &f:files) {
      labels<<f.name;details<<(f.format+(f.bytes?" · "+QString::number(f.bytes/1024)+" KB":""));
    }
    selected=qBound(0,selected,qMax(0,labels.size()-1));update();
  }
  void goBack() {
    if(page=="home"){close();return;}
    stopPlayback(); if(reply){reply->abort();reply->deleteLater();reply=nullptr;busy=false;}
    message.clear();
    if(page=="results"){page="home";selected=0;}
    else if(page=="item"){page="results";selected=0;}
    else if(page=="files" || page=="description"){page="item";selected=0;}
    else {page="files";selected=0;}
    refresh();
  }
protected:
  void draw(QPainter &p,const QRect &area) override {
    if(page=="home" || page=="results" || page=="item" || page=="files") {
      if(busy && labels.isEmpty()) {text(p,area,"Contacting archive.org...",24);return;}
      if(labels.isEmpty()) {text(p,area,message.isEmpty()?"No items found. Press Back.":message,22);return;}
      list(p,area,labels,details,selected);return;
    }
    if(page=="description" || page=="reading") {
      QString content=page=="description"?description:book;
      if(content.isEmpty()){text(p,area,busy?"Loading text...":"No readable text available",22);return;}
      QFont f=font();f.setPixelSize(22);p.setFont(f);p.setPen(Qt::white);
      QFontMetrics fm(f);int width=qMax(80,area.width()-12);
      QStringList lines;
      for(const auto &para:content.split('\n')) {
        QString s=para.trimmed();if(s.isEmpty()){lines<<"";continue;}
        while(!s.isEmpty()) {
          int at=s.size();while(at>1 && fm.horizontalAdvance(s.left(at))>width)--at;
          if(at<s.size()) {int space=s.lastIndexOf(' ',at);if(space>0)at=space;}
          lines<<s.left(at).trimmed();s=s.mid(at).trimmed();
          if(lines.size()>25000)break;
        }
        if(lines.size()>25000)break;
      }
      int visible=qMax(1,area.height()/29);textOffset=qBound(0,textOffset,qMax(0,lines.size()-visible));
      for(int i=0;i<visible && textOffset+i<lines.size();++i)
        p.drawText(area.x()+4,area.y()+24+i*29,lines[textOffset+i]);
      return;
    }
    if(page=="image" || page=="playing") {
      if(!picture.isNull()) {
        QSize size=picture.size();size.scale(area.size(),Qt::KeepAspectRatio);
        QRect rect(QPoint(0,0),size);rect.moveCenter(area.center());
        p.drawImage(rect,picture);
      } else text(p,area,busy?"Loading image...":page=="playing"?
                           (playing?"Streaming audio...":"Playback stopped"):"No image available",24);
      return;
    }
  }
  void button(reader_button key,int value) override {
    if(value!=1 && value!=2)return;
    if(key==READER_FAVORITES){stopPlayback();close();return;}
    if(key==READER_BACK && value==1){goBack();return;}
    if(key==READER_UP || key==READER_DOWN) {
      int delta=key==READER_UP?-1:1;
      if(page=="description" || page=="reading")textOffset=qMax(0,textOffset+delta*7);
      else selected=qBound(0,selected+delta,qMax(0,labels.size()-1));
      update();return;
    }
    if(key!=READER_OK || value!=1 || busy)return;
    if(page=="home") {
      category=categories.value(selected);resultPage=1;entries.clear();selected=0;page="results";search();
    } else if(page=="results") {
      if(selected==entries.size()){++resultPage;selected=0;search();}
      else if(selected<entries.size())loadItem(entries[selected].id);
    } else if(page=="item") {
      if(selected==0){page="files";selected=0;refresh();}
      else if(selected==1){if(favorites.contains(itemId))favorites.removeAll(itemId);else if(favorites.size()<300)favorites<<itemId;save();refresh();}
      else {page="description";textOffset=0;refresh();}
    } else if(page=="files" && selected<files.size())loadFile(files[selected]);
    else if(page=="playing"){stopPlayback();refresh();}
  }
  void idleChanged(bool dimmed) override {if(dimmed && playing){stopPlayback();refresh();}}
public:
  Archive():Screen("Internet Archive") {
    QFile f(dataFile());if(f.open(QIODevice::ReadOnly)){
      auto values=QJsonDocument::fromJson(f.readAll()).array();
      for(const auto &v:values)if(v.isString() && favorites.size()<300)favorites<<v.toString();
    }
    refresh();
  }
  ~Archive() override {stopPlayback();if(reply)reply->abort();}
};
int main(int argc,char **argv) {
  qputenv("QT_QPA_FB_DISABLE_INPUT","1");
  QApplication app(argc,argv);app.setFont(QFont("DejaVu Sans"));
  Archive window;window.showFullScreen();return app.exec();
}

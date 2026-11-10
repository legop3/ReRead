#include "reader-ui.hpp"
#include <QtWidgets>
#include <QtNetwork>
#include <QImageReader>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
#include <memory>
using namespace ReRead;

namespace {
struct Host {
  QString name, url, username, key;
  QStringList searches;
};
struct Post {
  int id=0;
  QString preview, sample, original, ext, rating, tags;
  bool favorite=false;
};
const QColor bg("#fafafa"), ink("#252525"), muted("#777777"), blue("#1769aa");
QString str(const QJsonValue &v) { return v.isString()?v.toString():QString(); }
QString cleanHost(QString s) {
  QUrl u(s.trimmed());
  if(!u.isValid()||u.scheme()!="https"||u.host().isEmpty()||
     !u.userInfo().isEmpty()||u.hasQuery()||u.hasFragment()||
     (!u.path().isEmpty()&&u.path()!="/"))return {};
  return QString("https://%1%2").arg(u.host().toLower(),u.port()>0?":"+QString::number(u.port()):"");
}
QUrl imageUrl(const QString &s) {
  QUrl u(s);
  // Never render a URL provided by a remote server unless it is HTTPS.
  return u.scheme()=="https"&&u.host().size()>0&&!u.userInfo().size()?u:QUrl();
}
QString safeName(QString name) {
  name.replace(QRegularExpression("[^a-zA-Z0-9_.-]"),"_");
  return name.left(90);
}
}

class Booru : public Screen {
  enum Page { HOME, GALLERY, VIEW, INFO, CONFIRM, DOWNLOADING };
  Page page=HOME;
  QNetworkAccessManager net;
  QList<Host> hosts;
  QList<Post> posts;
  QMap<int,QImage> thumbnails;
  QSet<int> pendingThumbnails, failedThumbnails;
  QList<QNetworkReply*> thumbJobs;
  QNetworkReply *api=nullptr, *imageJob=nullptr, *transfer=nullptr;
  std::unique_ptr<QSaveFile> saving;
  QImage full;
  QString status, search, downloadName;
  int hostIndex=0, homeIndex=0, homeFirst=0, selected=0, first=0, pageNumber=1, zoom=0;
  int panX=0,panY=0;
  qint64 received=0,total=0;
  bool more=false, favoritePending=false;
  const double zooms[8]={1.0,1.25,1.5,2,2.75,4,6,8};

  QString dataDir() const {return qEnvironmentVariable("READER_APP_DATA_DIR","/var/lib/reader/apps/reread-booru");}
  QString filesDir() const {return qEnvironmentVariable("REREAD_FILES_DIR","/var/lib/reader/files");}
  const Host &host() const {return hosts[hostIndex];}
  QStringList sections() const {
    QStringList s={"Recent","My favorites"};
    for(const auto &x:host().searches)s<<x;
    return s;
  }
  void text(QPainter &p,QRect r,QString s,int px=23,QColor c=ink,bool bold=false,int align=Qt::AlignLeft|Qt::AlignVCenter) const {
    QFont f=p.font();f.setPixelSize(px);f.setBold(bold);p.setFont(f);p.setPen(c);
    p.drawText(r,align|Qt::TextWordWrap,s);
  }
  void imageIn(QPainter &p,QRect target,const QImage &img,bool crop=false) const {
    if(img.isNull())return;
    QSize sz=img.size();sz.scale(target.size(),crop?Qt::KeepAspectRatioByExpanding:Qt::KeepAspectRatio);
    QRect dest(QPoint(),sz);dest.moveCenter(target.center());
    p.save();p.setClipRect(target);p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(dest,img);p.restore();
  }
  void stop(QNetworkReply *&r) {
    if(r){r->disconnect(this);r->abort();r->deleteLater();r=nullptr;}
  }
  QNetworkRequest request(QUrl u,bool authenticated) {
    QNetworkRequest req(u);
    req.setRawHeader("User-Agent","ReRead-Booru/0.1 (+https://github.com/legop3/ReRead; "+host().username.toUtf8()+")");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    req.setRawHeader("Accept","application/json");
    if(authenticated&&!host().username.isEmpty()&&!host().key.isEmpty()&&u.host()==QUrl(host().url).host())
      req.setRawHeader("Authorization","Basic "+(host().username+":"+host().key).toUtf8().toBase64());
    return req;
  }
  bool parseConfig() {
    QFile f(dataDir()+"/config.json");
    if(f.exists()) {
      if(!f.open(QIODevice::ReadOnly)){status="Cannot read config.json";return false;}
      QJsonParseError err;
      QJsonDocument doc=QJsonDocument::fromJson(f.readAll(),&err);
      if(err.error!=QJsonParseError::NoError||!doc.isObject()){status="Invalid config.json";return false;}
      for(auto v:doc.object().value("hosts").toArray()) {
        auto h=v.toObject();
        Host x;
        x.url=cleanHost(str(h.value("url")));
        if(x.url.isEmpty())continue;
        x.name=str(h.value("name"));if(x.name.isEmpty())x.name=QUrl(x.url).host();
        x.username=str(h.value("username"));x.key=str(h.value("api_key"));
        for(auto t:h.value("searches").toArray())if(t.isString()&&!t.toString().isEmpty()&&x.searches.size()<20)
          x.searches<<t.toString().left(180);
        hosts<<x;
        if(hosts.size()>=8)break;
      }
      if(hosts.isEmpty()){status="No valid HTTPS hosts in config.json";return false;}
    } else {
      Host x;x.name="e926";x.url="https://e926.net";hosts<<x;
    }
    return true;
  }
  void fetchPage() {
    stop(api);
    for(auto r:thumbJobs){r->disconnect(this);r->abort();r->deleteLater();}
    thumbJobs.clear();pendingThumbnails.clear();failedThumbnails.clear();thumbnails.clear();
    posts.clear();selected=0;first=0;full=QImage();
    status="Loading posts...";update();
    QUrl u(host().url+"/posts.json");QUrlQuery q;
    q.addQueryItem("limit","30");q.addQueryItem("page",QString::number(pageNumber));
    if(!search.isEmpty())q.addQueryItem("tags",search);
    u.setQuery(q);
    auto r=net.get(request(u,true));api=r;
    connect(r,&QNetworkReply::downloadProgress,this,[r](qint64 n,qint64){if(n>1536*1024)r->abort();});
    connect(r,&QNetworkReply::finished,this,[this,r]{
      if(api!=r){r->deleteLater();return;}api=nullptr;
      QByteArray b=r->readAll();int statusCode=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      QString error=r->errorString();bool ok=r->error()==QNetworkReply::NoError&&statusCode==200&&b.size()<=1536*1024;
      r->deleteLater();
      if(!ok){status="Posts request failed (HTTP "+QString::number(statusCode)+"): "+error;update();return;}
      QJsonParseError e;auto doc=QJsonDocument::fromJson(b,&e);
      if(e.error!=QJsonParseError::NoError||!doc.isObject()){status="Invalid API response";update();return;}
      auto items=doc.object().value("posts").toArray();
      for(auto v:items) {
        auto o=v.toObject();Post p;p.id=o.value("id").toInt();
        if(p.id<=0)continue;
        p.preview=str(o.value("preview").toObject().value("url"));
        p.sample=str(o.value("sample").toObject().value("url"));
        auto file=o.value("file").toObject();p.original=str(file.value("url"));p.ext=str(file.value("ext")).toLower();
        p.rating=str(o.value("rating"));p.favorite=o.value("is_favorited").toBool();
        auto tag=o.value("tags").toObject();QStringList all;
        for(const auto &x:tag)for(auto v:x.toArray())if(v.isString())all<<v.toString();
        p.tags=all.join(" ");
        posts<<p;
      }
      more=items.size()>=30;
      status=posts.isEmpty()?"No posts found.":QString();
      requestThumbs();update();
    });
  }
  void requestThumbs(){
    if(page!=GALLERY)return;
    for(int i=first;i<qMin(first+6,posts.size());++i){
      const Post &p=posts[i];
      if(thumbnails.contains(p.id)||pendingThumbnails.contains(p.id)||failedThumbnails.contains(p.id)||thumbJobs.size()>=3)continue;
      QUrl u=imageUrl(p.preview);if(!u.isValid())continue;
      int id=p.id;auto r=net.get(request(u,false));thumbJobs<<r;pendingThumbnails.insert(id);
      connect(r,&QNetworkReply::downloadProgress,this,[r](qint64 n,qint64){if(n>512*1024)r->abort();});
      connect(r,&QNetworkReply::finished,this,[this,r,id]{
        thumbJobs.removeAll(r);pendingThumbnails.remove(id);
        QByteArray b=r->readAll();bool ok=r->error()==QNetworkReply::NoError&&b.size()<=512*1024;
        r->deleteLater();
        if(!ok)failedThumbnails.insert(id);
        if(ok){
          QBuffer buffer(&b);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);
          reader.setAutoTransform(true);QSize sz=reader.size();
          if(sz.isValid())reader.setScaledSize(sz.scaled(320,260,Qt::KeepAspectRatio));
          QImage im=reader.read();if(!im.isNull())thumbnails.insert(id,im);
          else failedThumbnails.insert(id);
        }
        if(page==GALLERY){requestThumbs();update();}
      });
    }
  }
  Post *current(){return selected>=0&&selected<posts.size()?&posts[selected]:nullptr;}
  void loadFull() {
    stop(imageJob);full=QImage();zoom=0;panX=panY=0;
    Post *p=current();if(!p)return;
    QUrl u=imageUrl(!p->sample.isEmpty()?p->sample:p->original);
    if(!u.isValid()){status="No viewable image for this post";update();return;}
    status="Loading image...";update();
    auto r=net.get(request(u,false));imageJob=r;
    connect(r,&QNetworkReply::downloadProgress,this,[r](qint64 n,qint64){if(n>9*1024*1024)r->abort();});
    connect(r,&QNetworkReply::finished,this,[this,r]{
      if(imageJob!=r){r->deleteLater();return;}imageJob=nullptr;
      QByteArray b=r->readAll();bool ok=r->error()==QNetworkReply::NoError&&b.size()<=9*1024*1024;
      QString err=r->errorString();r->deleteLater();
      if(ok){
        QBuffer buf(&b);buf.open(QIODevice::ReadOnly);QImageReader reader(&buf);
        reader.setAutoTransform(true);
        auto sz=reader.size();
        if(sz.isValid()&&(sz.width()>2304||sz.height()>2304))
          reader.setScaledSize(sz.scaled(2304,2304,Qt::KeepAspectRatio));
        full=reader.read();
      }
      status=full.isNull()?(ok?"Unsupported image format":err):QString();
      update();
    });
  }
  void favorite(){
    Post *p=current();if(!p||favoritePending)return;
    if(host().username.isEmpty()||host().key.isEmpty()){status="Configure an account to manage favorites.";update();return;}
    int id=p->id;bool add=!p->favorite;favoritePending=true;status=add?"Adding favorite...":"Removing favorite...";update();
    QUrl url(host().url+(add?"/favorites.json":"/favorites/"+QString::number(id)+".json"));
    auto req=request(url,true);req.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
    auto r=add?net.post(req,"post_id="+QByteArray::number(id)):net.deleteResource(req);
    connect(r,&QNetworkReply::finished,this,[this,r,id,add]{
      favoritePending=false;
      int n=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      bool ok=r->error()==QNetworkReply::NoError&&n>=200&&n<300;
      r->deleteLater();
      if(ok){for(auto &p:posts)if(p.id==id)p.favorite=add;status=add?"Favorited":"Removed from favorites";}
      else status="Favorite request failed (HTTP "+QString::number(n)+")";
      update();
    });
  }
  void download(){
    Post *p=current();if(!p)return;
    QUrl u=imageUrl(p->original);
    if(!u.isValid()){status="No downloadable file";update();return;}
    QDir folder(filesDir()+"/Pictures/Booru");if(!folder.exists()&&!folder.mkpath(".")){status="Cannot create download folder";update();return;}
    QString extension=p->ext;
    if(!QRegularExpression("^[a-z0-9]{1,5}$").match(extension).hasMatch())extension="bin";
    downloadName=folder.filePath(safeName(QUrl(host().url).host()+"-"+QString::number(p->id)+"."+extension));
    if(QFile::exists(downloadName)){status="File already downloaded";update();return;}
    saving.reset(new QSaveFile(downloadName));if(!saving->open(QIODevice::WriteOnly)){saving.reset();status="Cannot write download";update();return;}
    page=DOWNLOADING;received=total=0;status="Downloading original...";update();
    auto r=net.get(request(u,false));transfer=r;
    connect(r,&QNetworkReply::readyRead,this,[this,r]{
      if(transfer!=r||!saving)return;
      QByteArray b=r->readAll();received+=b.size();
      if(received>64LL*1024*1024||saving->write(b)!=b.size())r->abort();
      update();
    });
    connect(r,&QNetworkReply::downloadProgress,this,[this](qint64,qint64 t){total=t;update();});
    connect(r,&QNetworkReply::finished,this,[this,r]{
      if(transfer!=r){r->deleteLater();return;}transfer=nullptr;
      QByteArray b=r->readAll();received+=b.size();
      bool ok=saving&&r->error()==QNetworkReply::NoError&&
              received<=64LL*1024*1024&&saving->write(b)==b.size()&&saving->commit();
      if(saving&&!ok)saving->cancelWriting();
      saving.reset();r->deleteLater();
      status=ok?"Saved to Files / Pictures / Booru":"Download failed";page=VIEW;update();
    });
  }
  void back(){
    if(page==DOWNLOADING){stop(transfer);if(saving)saving->cancelWriting();saving.reset();page=VIEW;}
    else if(page==INFO){page=VIEW;}
    else if(page==VIEW||page==CONFIRM){stop(imageJob);page=GALLERY;requestThumbs();}
    else if(page==GALLERY){stop(api);page=HOME;homeIndex=qBound(0,homeIndex,sections().size()-1);}
    else {close();return;}
    status.clear();update();
  }
  void navigate(int dx,int dy){
    if(page==HOME){
      int max=sections().size()-1;homeIndex=qBound(0,homeIndex+dx+dy,max);
      if(homeIndex<homeFirst)homeFirst=homeIndex;
      if(homeIndex>=homeFirst+7)homeFirst=homeIndex-6;
    }else if(page==GALLERY){
      selected=qBound(0,selected+dx+dy*3,qMax(0,posts.size()-1));
      if(selected<first)first=(selected/3)*3;
      if(selected>=first+6)first=qMax(0,(selected/3-1)*3);
      requestThumbs();
    }else if(page==VIEW){
      if(zoom>0){panX+=dx*70;panY+=dy*70;}else if(dx){selected=qBound(0,selected+dx,posts.size()-1);loadFull();}
    }
    update();
  }
protected:
  void draw(QPainter &p,const QRect &r) override {
    p.fillRect(r,bg);QRect area=r.adjusted(8,8,-8,-8);
    if(page==HOME){
      text(p,QRect(area.left()+12,area.top(),area.width()-24,39),"booru",32,ink,true);
      text(p,QRect(area.left()+12,area.top()+44,area.width()-24,29),host().name+"  •  "+host().url,19,muted);
      auto list=sections();
      for(int i=homeFirst;i<list.size();++i){
        int y=area.top()+92+(i-homeFirst)*48;
        if(y+45>area.bottom()-42)break;
        QRect line(area.left()+8,y,area.width()-16,44);
        if(i==homeIndex)p.fillRect(line,QColor("#dceeff"));
        p.setPen(QPen(i==homeIndex?blue:QColor("#dddddd"),i==homeIndex?2:1));p.drawRect(line);
        text(p,line.adjusted(13,0,-10,0),list[i],23,i==homeIndex?blue:ink,i==homeIndex);
      }
      text(p,QRect(area.left()+12,area.bottom()-36,area.width()-24,28),"OK open    Options change host    Back exit",18,muted);
    } else if(page==GALLERY){
      text(p,QRect(area.left()+8,area.top(),area.width()-16,36),search.isEmpty()?"Recent posts":search,24,ink,true);
      text(p,QRect(area.left()+8,area.top()+36,area.width()-16,23),host().name+"   Page "+QString::number(pageNumber)+"   •   OK view   Options next page",17,muted);
      QRect grid=area.adjusted(0,69,0,-35);
      int gap=10,cw=(grid.width()-2*gap)/3,ch=(grid.height()-gap)/2;
      for(int n=0;n<6;++n){
        int i=first+n;if(i>=posts.size())break;
        QRect cell(grid.x()+(n%3)*(cw+gap),grid.y()+(n/3)*(ch+gap),cw,ch);
        p.fillRect(cell,QColor("#ececec"));
        imageIn(p,cell.adjusted(4,4,-4,-31),thumbnails.value(posts[i].id),true);
        p.setPen(QPen(i==selected?blue:QColor("#cccccc"),i==selected?3:1));p.drawRect(cell.adjusted(1,1,-2,-2));
        text(p,QRect(cell.x()+8,cell.bottom()-27,cell.width()-16,24),
             "#"+QString::number(posts[i].id)+"   "+posts[i].rating+(posts[i].favorite?"  ★":""),17,ink,i==selected);
      }
      text(p,QRect(area.x()+10,area.bottom()-26,area.width()-20,26),
           "Left/Right/Up/Down navigate    Back searches",17,muted);
    } else if(page==VIEW||page==INFO||page==CONFIRM||page==DOWNLOADING){
      Post *post=current();
      if(page==INFO){
        text(p,QRect(area.x()+10,area.y(),area.width()-20,40),
             post?"Post #"+QString::number(post->id)+"  |  "+post->rating:"Post",26,ink,true);
        text(p,area.adjusted(13,52,-13,-46),post?post->tags:"",19,ink,false,Qt::AlignLeft|Qt::AlignTop);
        text(p,QRect(area.x()+10,area.bottom()-32,area.width()-20,30),"Back return",18,muted);
      }else{
        if(!full.isNull()){
          QSizeF sz=full.size();sz.scale(r.size(),Qt::KeepAspectRatio);
          double scale=zooms[zoom];QSizeF scaled(sz.width()*scale,sz.height()*scale);
          QRectF dest(0,0,scaled.width(),scaled.height());dest.moveCenter(QPointF(r.center().x()+panX,r.center().y()+panY));
          p.save();p.setClipRect(r);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(dest,full);p.restore();
        }
        if(page==DOWNLOADING){
          p.fillRect(QRect(area.x()+110,area.center().y()-42,area.width()-220,84),QColor("#222222"));
          text(p,QRect(area.x()+122,area.center().y()-37,area.width()-244,74),
               "Downloading... "+QString::number(received/1024)+" KB"+(total>0?" / "+QString::number(total/1024)+" KB":""),22,Qt::white,true);
        } else {
          p.fillRect(QRect(r.x(),r.bottom()-39,r.width(),39),QColor(0,0,0,195));
          text(p,QRect(r.x()+12,r.bottom()-38,r.width()-24,35),
               "OK favorite  •  Options download  •  Play details  •  +/- zoom  •  Back",17,Qt::white);
          if(post&&post->favorite)text(p,QRect(r.right()-85,r.top()+4,75,35),"★",31,blue,true);
        }
      }
    }
    if(!status.isEmpty()&&page!=DOWNLOADING){
      QRect note(r.left()+9,r.bottom()-74,r.width()-18,30);
      p.fillRect(note,QColor("#fff0d0"));
      text(p,note.adjusted(8,0,-8,0),QFontMetrics(font()).elidedText(status,Qt::ElideRight,note.width()-16),17,QColor("#704d15"));
    }
  }
  void button(reader_button key,int state) override {
    if(state!=1&&state!=2)return;
    if(key==READER_FAVORITES&&state==1){close();return;}
    if(key==READER_BACK&&state==1){back();return;}
    if(page==DOWNLOADING)return;
    if(key==READER_UP)navigate(0,-1);
    else if(key==READER_DOWN)navigate(0,1);
    else if(key==READER_LEFT)navigate(-1,0);
    else if(key==READER_RIGHT)navigate(1,0);
    else if(page==VIEW&&key==READER_ZOOM_IN&&state==1){zoom=qMin(7,zoom+1);update();}
    else if(page==VIEW&&key==READER_ZOOM_OUT&&state==1){zoom=qMax(0,zoom-1);if(!zoom)panX=panY=0;update();}
    else if(key==READER_OPTIONS&&state==1){
      if(page==HOME){hostIndex=(hostIndex+1)%hosts.size();homeIndex=homeFirst=0;status.clear();update();}
      else if(page==GALLERY){if(more){++pageNumber;fetchPage();}else {status="End of results";update();}}
      else if(page==VIEW)download();
    }else if(key==READER_PLAY&&state==1&&page==VIEW){page=INFO;update();}
    else if(key==READER_OK&&state==1) {
      if(page==HOME){
        if(homeIndex==1){
          if(host().username.isEmpty()){status="Set username and API key in config.json";update();return;}
          search="fav:"+host().username;
        } else search=homeIndex==0?QString():host().searches.value(homeIndex-2);
        pageNumber=1;page=GALLERY;fetchPage();
      }else if(page==GALLERY&&!posts.isEmpty()){page=VIEW;loadFull();}
      else if(page==VIEW)favorite();
    }
  }
public:
  Booru():Screen("booru"){
    showHeader=false;showControls=false;
    if(!parseConfig()){Host x;x.name="Configuration error";x.url="https://e926.net";hosts<<x;}
  }
  ~Booru() override {
    stop(api);stop(imageJob);stop(transfer);
    for(auto r:thumbJobs){r->disconnect(this);r->abort();r->deleteLater();}
    if(saving)saving->cancelWriting();
  }
};
int main(int argc,char **argv){
  qputenv("QT_QPA_FB_DISABLE_INPUT","1");
  QApplication app(argc,argv);app.setFont(QFont("DejaVu Sans"));
  Booru window;window.showFullScreen();return app.exec();
}

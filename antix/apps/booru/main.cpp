#include <gio/gio.h>
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

class Booru : public Screen {
  friend class BooruTest;
  enum Page { HOME, GALLERY, POST, INFO, CONFIRM, DOWNLOADING, SETTINGS, SEARCH };
  Page page=HOME;
  QNetworkAccessManager net;
  QList<Host> hosts;
  QList<Post> posts;
  QMap<int,QImage> thumbnails;
  QSet<int> pendingThumbnails, failedThumbnails;
  QList<QNetworkReply*> thumbJobs;
  QTimer thumbTimer;
  QHash<int,int> thumbAttempts;
  QHash<int,qint64> thumbRetryAt;
  qint64 lastThumbnailStart=0;
  bool thumbnailsPaused=false;
  QNetworkReply *api=nullptr, *tagApi=nullptr, *imageJob=nullptr, *transfer=nullptr;
  std::unique_ptr<QSaveFile> saving;
  QString status, search, baseSearch, downloadName;
  QString initialSearch;
  QStringList popularTags;
  QHash<QString,int> popularTagCounts;
  bool tagCatalogMode=false;
  QMap<QString,int> tagFilters;
  QStringList sorts={"Newest","Oldest","Top score","Most favorited","Hot","Random","Recently updated"};
  QStringList sortTerms={"","order:id","order:score","order:favcount","order:hot","order:random","order:updated"};
  QStringList ratings={"Any","Safe","Questionable","Explicit"};
  QStringList mediaTypes={"Any","Still images","JPEG","PNG","GIF","MP4"};
  QStringList scoreLevels={"Any","10+","50+","100+"};
  int sortIndex=0,ratingIndex=0,mediaIndex=0,scoreIndex=0,searchIndex=0,tagIndex=0;
  bool tagsFromSearch=false;
  int settingIndex=0, hostIndex=0, homeIndex=0, selected=0, first=0, pageNumber=1;
  qint64 received=0,total=0;
  bool autoOpenImages=false, more=false, loadingMore=false, favoritePending=false;
  bool apiRetryPending=false;
  int apiRetries=0;

  QString preferencesPath() const {return qEnvironmentVariable("READER_APP_DATA_DIR","/var/lib/reader/apps/reread-booru")+"/preferences.json";}
  QString filesDir() const {return qEnvironmentVariable("REREAD_FILES_DIR","/var/lib/reader/files");}
  const Host &host() const {return hosts[hostIndex];}
  QStringList sections() const {
    QStringList s={"Recent","My favorites"};
    for(const auto &x:host().searches)s<<x;
    return s;
  }
  QStringList currentTags() const {
    if(tagCatalogMode)return popularTags;
    if(selected<0||selected>=posts.size())return {};
    auto result=posts[selected].tags.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
    result.removeDuplicates();result.sort(Qt::CaseInsensitive);return result;
  }
  QString builtSearch() const {
    QStringList parts;
    for(const auto &token:baseSearch.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts)) {
      if(token.startsWith("order:") || (ratingIndex&&token.startsWith("rating:")) ||
         (mediaIndex&&token.startsWith("type:")) || (scoreIndex&&token.startsWith("score:")))
        continue;
      const QString bare=token.startsWith("-")?token.mid(1):token;
      if(tagFilters.contains(bare))continue;
      parts<<token;
    }
    for(auto it=tagFilters.cbegin();it!=tagFilters.cend();++it)
      if(it.value())parts << (it.value()<0?"-":"")+it.key();
    if(sortIndex)parts<<sortTerms[sortIndex];
    if(ratingIndex)parts<<"rating:"+QStringList{"","s","q","e"}[ratingIndex];
    if(mediaIndex==1)parts<<"-type:mp4"<<"-type:webm"<<"-type:swf";
    else if(mediaIndex>1)parts<<"type:"+QStringList{"","","jpg","png","gif","mp4"}[mediaIndex];
    if(scoreIndex)parts<<"score:>="+QStringList{"","10","50","100"}[scoreIndex];
    return parts.join(" ");
  }
  void fetchPopularTags(){
    stop(tagApi);popularTags.clear();popularTagCounts.clear();tagIndex=0;status="Loading popular tags...";update();
    QUrl url(host().url+"/tags.json");QUrlQuery q;
    q.addQueryItem("search[order]","count");q.addQueryItem("limit","60");url.setQuery(q);
    auto r=net.get(request(url,true));tagApi=r;
    connect(r,&QNetworkReply::downloadProgress,this,[r](qint64 n,qint64){if(n>256*1024)r->abort();});
    connect(r,&QNetworkReply::finished,this,[this,r]{
      if(tagApi!=r){r->deleteLater();return;}tagApi=nullptr;
      const int code=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      const QByteArray b=r->readAll();
      const bool ok=r->error()==QNetworkReply::NoError && code>=200 && code<300 && b.size()<=256*1024;
      r->deleteLater();
      if(ok){
        const auto doc=QJsonDocument::fromJson(b);
        for(const auto &v:doc.array()){
          const QString name=str(v.toObject().value("name"));
          if(!name.isEmpty() && popularTags.size()<60){
            popularTags<<name;popularTagCounts.insert(name,v.toObject().value("post_count").toInt());
          }
        }
      }
      status=!ok?"Unable to load popular tags (HTTP "+QString::number(code)+")":
                   popularTags.isEmpty()?"No popular tags available":QString();
      update();
    });
  }
  void initializeSearch(const QString &preset) {
    initialSearch=preset;tagFilters.clear();
    sortIndex=ratingIndex=mediaIndex=scoreIndex=0;
    QStringList remaining;
    for(const auto &token:preset.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts)){
      const int order=sortTerms.indexOf(token);
      if(order>0)sortIndex=order;
      else if(token=="rating:s"||token=="rating:safe")ratingIndex=1;
      else if(token=="rating:q"||token=="rating:questionable")ratingIndex=2;
      else if(token=="rating:e"||token=="rating:explicit")ratingIndex=3;
      else if(token=="type:jpg")mediaIndex=2;
      else if(token=="type:png")mediaIndex=3;
      else if(token=="type:gif")mediaIndex=4;
      else if(token=="type:mp4")mediaIndex=5;
      else if(token=="score:>=10")scoreIndex=1;
      else if(token=="score:>=50")scoreIndex=2;
      else if(token=="score:>=100")scoreIndex=3;
      else remaining<<token;
    }
    baseSearch=remaining.join(" ");
    search=builtSearch();
  }
  void applySearch() {
    const QString query=builtSearch();
    if(query.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts).size()>40){
      status="Search exceeds the server's 40-tag limit";update();return;
    }
    stop(tagApi);stop(api);loadingMore=false;apiRetryPending=false;
    search=query;page=GALLERY;chrome();fetchPage();
  }
  void chrome() {
    showHeader=false;

    if(page==SETTINGS) controls={{READER_UP,"Select"},{READER_OK,"Change"},{READER_BACK,"Searches"}};
    else if(page==SEARCH) controls={{READER_UP,"Select"},{READER_OK,"Change"},{READER_OPTIONS,"Apply"},{READER_BACK,"Results"}};
    else if(page==HOME) controls={{READER_OK,"Open"},{READER_OPTIONS,"Settings"},{READER_BACK,"Home"}};
    else if(page==GALLERY) controls={{READER_UP,"Browse"},{READER_OK,"View"},{READER_OPTIONS,"Search"},{READER_BACK,"Searches"}};
    else if(page==POST) controls={{READER_VIEW_TOGGLE,"Open image"},{READER_OK,"Favorite"},{READER_PLAY,"Details"},{READER_OPTIONS,"Save"},{READER_BACK,"Gallery"}};
    else if(page==INFO) controls={{READER_UP,"Tags"},{READER_OK,"Include / Exclude"},{READER_OPTIONS,"Search"},{READER_BACK,"Return"}};
    else if(page==DOWNLOADING) controls={{READER_BACK,"Cancel"}};
    else controls={{READER_BACK,"Gallery"}};
    update();
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
    req.setRawHeader("User-Agent","ReRead-Booru/0.6 (+https://github.com/legop3/ReRead; "+host().username.toUtf8()+")");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    req.setRawHeader("Accept","application/json");
    if(authenticated&&!host().username.isEmpty()&&!host().key.isEmpty()&&u.host()==QUrl(host().url).host())
      req.setRawHeader("Authorization","Basic "+(host().username+":"+host().key).toUtf8().toBase64());
    return req;
  }
  bool parseConfig() {
    QString path="/etc/reread/apps/booru.json";
    QFile f(path);
    if(f.exists()) {
      if(!f.open(QIODevice::ReadOnly)){status="Cannot read web Apps / Booru settings";return false;}
      QJsonParseError err;
      QJsonDocument doc=QJsonDocument::fromJson(f.readAll(),&err);
      if(err.error!=QJsonParseError::NoError||!doc.isObject()){status="Invalid web Apps / Booru settings";return false;}
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
      if(hosts.isEmpty()){status="No valid HTTPS hosts in web Apps / Booru settings";return false;}
    } else {
      Host x;x.name="e926";x.url="https://e926.net";hosts<<x;
    }
    return true;
  }
  void fetchPage(bool append=false,bool retry=false) {
    if(api)return;
    if(!retry){apiRetries=0;apiRetryPending=false;}
    loadingMore=append;
    if(!append){
      for(auto r:thumbJobs){r->disconnect(this);r->abort();r->deleteLater();}
      thumbTimer.stop();thumbJobs.clear();pendingThumbnails.clear();failedThumbnails.clear();thumbnails.clear();
      thumbAttempts.clear();thumbRetryAt.clear();
      posts.clear();selected=0;first=0;
      pageNumber=1;more=false;
    }
    status=append?QString():"Loading posts...";
    update();
    const int requestedPage=pageNumber;
    QUrl u(host().url+"/posts.json");QUrlQuery q;
    q.addQueryItem("limit","30");q.addQueryItem("page",QString::number(requestedPage));
    if(!search.isEmpty())q.addQueryItem("tags",search);
    u.setQuery(q);
    auto r=net.get(request(u,true));api=r;
    connect(r,&QNetworkReply::downloadProgress,this,[r](qint64 n,qint64){if(n>1536*1024)r->abort();});
    connect(r,&QNetworkReply::finished,this,[this,r,append]{
      if(api!=r){r->deleteLater();return;}api=nullptr;loadingMore=false;
      QByteArray b=r->readAll();int statusCode=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      QString error=r->errorString();bool ok=r->error()==QNetworkReply::NoError&&statusCode==200&&b.size()<=1536*1024;
      const QByteArray retryAfter=r->rawHeader("Retry-After");
      r->deleteLater();
      if(!ok){
        if((statusCode==429||statusCode==503)&&apiRetries<2){
          apiRetryPending=true;
          ++apiRetries;
          bool valid=false;const int seconds=retryAfter.toInt(&valid);
          const int delay=valid?qBound(1,seconds,60)*1000:apiRetries*3000;
          status="Server busy (HTTP "+QString::number(statusCode)+"); retrying...";
          update();
          const QString expectedSearch=search;
          const int expectedPage=pageNumber;
          QTimer::singleShot(delay,this,[this,append,expectedSearch,expectedPage]{
            if(!apiRetryPending)return;
            apiRetryPending=false;
            if((page==GALLERY||page==POST)&&search==expectedSearch&&
                pageNumber==expectedPage&&!api)fetchPage(append,true);
          });
          return;
        }
        if(append){--pageNumber;more=true;}
        status="Posts request failed (HTTP "+QString::number(statusCode)+"): "+error;
        update();return;
      }
      apiRetries=0;apiRetryPending=false;
      QJsonParseError e;auto doc=QJsonDocument::fromJson(b,&e);
      if(e.error!=QJsonParseError::NoError||!doc.isObject()){
        if(append){--pageNumber;more=true;}
        status="Invalid API response";update();return;
      }
      auto items=doc.object().value("posts").toArray();
      QSet<int> seen;
      for(const auto &existing:posts)seen.insert(existing.id);
      for(auto v:items) {
        auto o=v.toObject();Post p;p.id=o.value("id").toInt();
        if(p.id<=0||seen.contains(p.id))continue;
        seen.insert(p.id);
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
  void maybeLoadMore(){
    if((page!=GALLERY && page!=POST) || api || loadingMore || apiRetryPending || !more || posts.isEmpty())return;
    if(selected<posts.size()-6)return;
    ++pageNumber;
    fetchPage(true);
  }
  void requestThumbs(){
    if(page!=GALLERY||thumbnailsPaused)return;
    // e621 limits aggregate requests; pace CDN previews rather than opening bursts.
    // Keep two in flight at most and retry transient failures without a tight loop.
    const qint64 now=QDateTime::currentMSecsSinceEpoch();
    if(thumbJobs.size()>=2)return;
    const int interval=650;
    if(lastThumbnailStart>0 && now-lastThumbnailStart<interval){
      if(!thumbTimer.isActive())thumbTimer.start(int(lastThumbnailStart+interval-now));
      return;
    }
    qint64 soonest=0;
    for(int i=first;i<qMin(first+6,posts.size());++i){
      const Post &p=posts[i];
      if(thumbnails.contains(p.id)||pendingThumbnails.contains(p.id)||
         failedThumbnails.contains(p.id))continue;
      const QUrl u=imageUrl(p.preview);
      if(!u.isValid()){failedThumbnails.insert(p.id);continue;}
      if(thumbRetryAt.value(p.id)>now){
        const qint64 wait=thumbRetryAt.value(p.id)-now;
        if(!soonest||wait<soonest)soonest=wait;
        continue;
      }
      const int id=p.id;lastThumbnailStart=now;
      auto r=net.get(request(u,false));thumbJobs<<r;pendingThumbnails.insert(id);
      connect(r,&QNetworkReply::downloadProgress,this,[r](qint64 n,qint64){
        if(n>512*1024)r->abort();
      });
      connect(r,&QNetworkReply::finished,this,[this,r,id]{
        thumbJobs.removeAll(r);pendingThumbnails.remove(id);
        const int code=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray b=r->readAll();
        const bool ok=r->error()==QNetworkReply::NoError&&code>=200&&code<300&&b.size()<=512*1024;
        if(ok){
          QBuffer buffer(&b);buffer.open(QIODevice::ReadOnly);
          QImageReader reader(&buffer);reader.setAutoTransform(true);
          const QSize native=reader.size();
          if(native.isValid())reader.setScaledSize(native.scaled(320,260,Qt::KeepAspectRatio));
          const QImage im=reader.read();
          if(!im.isNull()){
            thumbnails.insert(id,im);thumbAttempts.remove(id);thumbRetryAt.remove(id);
            // Bound decoded image memory on this 512 MiB device.
            while(thumbnails.size()>60){
              // Retain the six images currently on screen; evict the farthest.
              int farthest=-1, distance=-1;
              for(auto it=thumbnails.cbegin();it!=thumbnails.cend();++it){
                int index=-1;
                for(int i=0;i<posts.size();++i)if(posts[i].id==it.key()){index=i;break;}
                const int d=index<0?999999:qAbs(index-selected);
                if(d>distance){farthest=it.key();distance=d;}
              }
              if(farthest<0)break;
              thumbnails.remove(farthest);
            }
          } else failedThumbnails.insert(id);
        } else if((code==429||code==503||code==502||code==504||
                   r->error()==QNetworkReply::TimeoutError) && thumbAttempts.value(id)<3){
          const int count=thumbAttempts.value(id)+1;thumbAttempts.insert(id,count);
          int seconds=qMin(30,2*(1<<count));
          bool headerOk=false;
          const int retry=r->rawHeader("Retry-After").toInt(&headerOk);
          if(headerOk)seconds=qBound(1,retry,60);
          thumbRetryAt.insert(id,QDateTime::currentMSecsSinceEpoch()+seconds*1000LL);
        } else {
          failedThumbnails.insert(id);
        }
        r->deleteLater();
        if(page==GALLERY){requestThumbs();update();}
      });
      if(!thumbTimer.isActive())thumbTimer.start(interval);
      return;
    }
    if(soonest&&!thumbTimer.isActive())thumbTimer.start(qBound(1,int(soonest),60000));
  }
  Post *current(){return selected>=0&&selected<posts.size()?&posts[selected]:nullptr;}
  QString cacheRoot() const {
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)+"/reread-booru/images";
  }
  QString cachePath(const QUrl &url,int id) const {
    auto hash=QCryptographicHash::hash(url.toEncoded(),QCryptographicHash::Sha256).toHex();
    // One image per directory: a document viewer must not browse unrelated cached posts.
    return cacheRoot()+"/"+QString::fromLatin1(hash)+"/post-"+QString::number(id);
  }
  void trimCache(const QString &keep) {
    QList<QFileInfo> files; qint64 bytes=0;
    QDirIterator it(cacheRoot(),QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);
    while(it.hasNext()){it.next();auto file=it.fileInfo();files<<file;bytes+=file.size();}
    std::sort(files.begin(),files.end(),[](const QFileInfo &a,const QFileInfo &b){return a.lastModified()<b.lastModified();});
    for(const auto &file:files) {
      if(bytes<=64LL*1024*1024)break;
      if(file.filePath()!=keep && QFile::remove(file.filePath())) {
        bytes-=file.size();QDir().rmdir(file.absolutePath());
      }
    }
  }
  void openCached(const QString &file) {
    auto context=g_app_launch_context_new();
    g_app_launch_context_unsetenv(context,"READER_APP_ID");
    g_app_launch_context_unsetenv(context,"READER_APP_DATA_DIR");
    GError *error=nullptr;auto uri=QUrl::fromLocalFile(file).toEncoded();
    bool opened=g_app_info_launch_default_for_uri(uri.constData(),context,&error);
    g_object_unref(context);
    status=opened?QString():error?QString::fromUtf8(error->message):"Unable to open image";
    if(error)g_error_free(error);
    update();
  }
  bool cacheImage(const QString &file,const QByteArray &bytes) {
    if(bytes.size()>9*1024*1024 || !QMimeDatabase().mimeTypeForData(bytes).name().startsWith("image/")) {
      status="No supported image sample for this post";update();return false;
    }
    QDir().mkpath(QFileInfo(file).absolutePath());QSaveFile output(file);
    if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit()) {
      status="Cannot write image cache";update();return false;
    }
    trimCache(file);return true;
  }
  void loadFull() {
    stop(imageJob);
    Post *p=current();if(!p)return;
    QUrl url=imageUrl(!p->sample.isEmpty()?p->sample:p->original);
    if(!url.isValid()){status="No viewable image for this post";update();return;}
    auto file=cachePath(url,p->id);
    if(QFileInfo(file).isFile()) {
      QFile cached(file);if(cached.open(QIODevice::ReadOnly))cached.setFileTime(QDateTime::currentDateTime(),QFileDevice::FileModificationTime);
      openCached(file);return;
    }
    status="Loading image...";update();
    auto r=net.get(request(url,false));imageJob=r;
    connect(r,&QNetworkReply::downloadProgress,this,[r](qint64 n,qint64){if(n>9*1024*1024)r->abort();});
    connect(r,&QNetworkReply::finished,this,[this,r,file]{
      if(imageJob!=r){r->deleteLater();return;}imageJob=nullptr;
      auto bytes=r->readAll();bool ok=r->error()==QNetworkReply::NoError;
      auto message=r->errorString();r->deleteLater();
      if(!ok){status=message;update();return;}
      if(cacheImage(file,bytes))openCached(file);
    });
  }
  void favorite(){
    Post *p=current();if(!p||favoritePending)return;
    if(host().username.isEmpty()||host().key.isEmpty()){status="Set account in web Apps / Booru settings";update();return;}
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
    page=DOWNLOADING;received=total=0;status="Downloading original...";chrome();
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
      status=ok?"Saved to Files / Pictures / Booru":"Download failed";page=POST;chrome();
    });
  }
  void back(){
    if(page==SETTINGS){page=HOME;}
    else if(page==SEARCH){page=GALLERY;}
    else if(page==INFO){stop(tagApi);page=tagsFromSearch?SEARCH:POST;}
    else if(page==DOWNLOADING){stop(transfer);if(saving)saving->cancelWriting();saving.reset();page=POST;}
    else if(page==POST||page==CONFIRM){stop(imageJob);page=GALLERY;requestThumbs();}
    else if(page==GALLERY){stop(api);loadingMore=false;apiRetryPending=false;page=HOME;homeIndex=qBound(0,homeIndex,sections().size()-1);}
    else {close();return;}
    status.clear();chrome();
  }
  void navigate(int dx,int dy){
    if(page==SETTINGS){settingIndex=qBound(0,settingIndex+dx+dy,1);}
    else if(page==SEARCH){searchIndex=qBound(0,searchIndex+dx+dy,7);}
    else if(page==INFO){tagIndex=qBound(0,tagIndex+dx+dy,qMax(0,currentTags().size()-1));}
    else if(page==HOME){
      int max=sections().size()-1;homeIndex=qBound(0,homeIndex+dx+dy,max);

    }else if(page==GALLERY){
      selected=qBound(0,selected+dx+dy*3,qMax(0,posts.size()-1));
      if(selected<first)first=(selected/3)*3;
      if(selected>=first+6)first=qMax(0,(selected/3-1)*3);
      requestThumbs();
      maybeLoadMore();
    }else if(page==POST){
      if(dx){const int next=qBound(0,selected+dx,posts.size()-1);
        if(next!=selected){selected=next;stop(imageJob);status.clear();if(autoOpenImages)loadFull();maybeLoadMore();}}

    }
    update();
  }
protected:
  void draw(QPainter &p,const QRect &r) override {
    // Screen paints ReRead's palette and standard controls. No app-created header.
    const QRect area=r.adjusted(4,3,-4,-3);
    if(page==SETTINGS) {
      list(p,area,{"Server", "Open images automatically"},{host().name,autoOpenImages?"On":"Off · use View on a post"},settingIndex);
    } else if(page==SEARCH) {
      const int active=int(tagFilters.size());
      list(p,area,{"Popular tags","Tags from selected post","Sort","Rating","Media type","Minimum score","Clear filters","Apply search"},
        {QString::number(active)+" active tag filters","Pick tags on the current post",sorts[sortIndex],
         ratings[ratingIndex],mediaTypes[mediaIndex],scoreLevels[scoreIndex],
         "Restore search defaults",builtSearch().left(90)},searchIndex);
    } else if(page==HOME) {
      const QStringList names=sections();
      QStringList details;
      for(int i=0;i<names.size();++i)
        details << (i==0 ? host().name+" · Latest posts" :
                    i==1 ? host().name+" · Account favorites" : "Saved search");
      list(p,area,names,details,homeIndex);
    } else if(page==GALLERY) {
      const int gap=12;
      const int cw=(area.width()-2*gap)/3;
      const int ch=(area.height()-gap)/2;
      for(int n=0;n<6;++n) {
        const int i=first+n;
        if(i>=posts.size())break;
        const QRect cell(area.x()+(n%3)*(cw+gap),area.y()+(n/3)*(ch+gap),cw,ch);
        const bool active=i==selected;
        if(active) p.fillRect(cell,palette().highlight());
        const QRect picture=cell.adjusted(5,5,-5,-34);
        p.fillRect(picture,palette().color(QPalette::Base));
        imageIn(p,picture,thumbnails.value(posts[i].id),true);
        if(!active) {
          p.setPen(QPen(palette().mid(),1));
          p.drawLine(cell.left()+5,cell.bottom()-31,cell.right()-5,cell.bottom()-31);
        } else {
          p.fillRect(QRect(cell.left(),cell.top(),6,cell.height()),palette().link());
        }
        label(p,QRect(cell.x()+12,cell.bottom()-30,cell.width()-20,26),
              "#"+QString::number(posts[i].id)+" · "+posts[i].rating+
              (posts[i].favorite?" ★":""),19,active,
              palette().color(active?QPalette::HighlightedText:QPalette::Text));
      }
      if(posts.isEmpty()&&status.isEmpty())text(p,area,"No posts found.",25);
    } else if(page==POST||page==INFO||page==CONFIRM||page==DOWNLOADING) {
      Post *post=current();
      if(page==INFO) {
        const QStringList names=currentTags();
        const auto layout=listInfo(p,area,names.size(),tagIndex,62,0.58);
        for(int i=layout.first;i<qMin(names.size(),layout.first+layout.visible);++i){
          const QRect cell=listInfoRow(p,layout,i,tagIndex);
          const bool chosen=i==tagIndex;
          label(p,cell.adjusted(12,0,-10,0),names[i],22,chosen,
                palette().color(chosen?QPalette::HighlightedText:QPalette::Text));
        }
        if(!names.isEmpty()){
          const QString tag=names.value(tagIndex);
          const int rule=tagFilters.value(tag);
          label(p,QRect(layout.info.x()+8,layout.info.y()+12,layout.info.width()-16,45),
                tag,27,true);
          text(p,QRect(layout.info.x()+8,layout.info.y()+72,layout.info.width()-16,180),
               "Filter: "+QString(rule==1?"Include":rule==-1?"Exclude":"Off")+
               (tagCatalogMode?"\nUsed in "+QString::number(popularTagCounts.value(tag))+" posts":"")+
               "\n\nSelect tags to include or exclude from the search.",22);
          label(p,QRect(layout.info.x()+8,layout.info.bottom()-46,layout.info.width()-16,35),
                QString::number(tagFilters.size())+" filters active",19);
        }else if(status.isEmpty())text(p,layout.info,"No tags found",23);
      } else {
        if(post) {
          QRect picture(area.x()+8,area.y()+8,area.width()/2-20,area.height()-24);
          imageIn(p,picture,thumbnails.value(post->id));
          QRect description(area.x()+area.width()/2,area.y()+20,area.width()/2-12,area.height()-40);
          text(p,description,"Post #"+QString::number(post->id)+" · "+post->rating+
            "\n\nOpen image for full-size viewing.\n\n"+post->tags.left(240),23);
        }
        if(page==DOWNLOADING) {
          const QRect box(area.x()+95,area.center().y()-46,area.width()-190,92);
          p.fillRect(box,palette().base());
          p.setPen(QPen(palette().mid(),2));p.drawRect(box);
          text(p,box.adjusted(16,8,-16,-8),
               "Downloading "+QString::number(received/1024)+" KiB"+
               (total>0?" / "+QString::number(total/1024)+" KiB":""),23);
        } else if(post&&post->favorite) {
          label(p,QRect(r.right()-65,r.top()+8,48,40),"★",32,true,
                palette().color(QPalette::Link));
        }
      }
    }
    if(!status.isEmpty()&&page!=DOWNLOADING) {
      const QRect note(area.left()+6,area.bottom()-46,area.width()-12,39);
      p.fillRect(note,palette().toolTipBase());
      label(p,note.adjusted(11,0,-11,0),status,18,false,
            palette().color(QPalette::ToolTipText));
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
    else if(page==POST&&key==READER_VIEW_TOGGLE&&state==1){loadFull();}
    else if(key==READER_OPTIONS&&state==1){
      if(page==HOME){page=SETTINGS;settingIndex=0;status.clear();chrome();}
      else if(page==GALLERY){page=SEARCH;searchIndex=0;status.clear();chrome();}
      else if(page==SEARCH||page==INFO)applySearch();
      else if(page==POST)download();
    }else if(key==READER_PLAY&&state==1&&page==POST){page=INFO;tagsFromSearch=false;tagCatalogMode=false;tagIndex=0;chrome();}
    else if(key==READER_OK&&state==1) {
      if(page==SETTINGS){
        if(settingIndex==0){hostIndex=(hostIndex+1)%hosts.size();homeIndex=0;}
        else {autoOpenImages=!autoOpenImages;writeJson(preferencesPath(),{{"auto_open_images",autoOpenImages}});}
        update();
      } else if(page==SEARCH){
        if(searchIndex==0){page=INFO;tagsFromSearch=true;tagCatalogMode=true;tagIndex=0;chrome();fetchPopularTags();}
        else if(searchIndex==1){page=INFO;tagsFromSearch=true;tagCatalogMode=false;tagIndex=0;chrome();}
        else if(searchIndex==2){sortIndex=(sortIndex+1)%sorts.size();update();}
        else if(searchIndex==3){ratingIndex=(ratingIndex+1)%ratings.size();update();}
        else if(searchIndex==4){mediaIndex=(mediaIndex+1)%mediaTypes.size();update();}
        else if(searchIndex==5){scoreIndex=(scoreIndex+1)%scoreLevels.size();update();}
        else if(searchIndex==6){initializeSearch(initialSearch);update();}
        else applySearch();
      } else if(page==INFO){
        const auto tags=currentTags();
        if(tagIndex<tags.size()){
          const QString t=tags[tagIndex];const int current=tagFilters.value(t);
          if(current==0)tagFilters.insert(t,1);
          else if(current==1)tagFilters.insert(t,-1);
          else tagFilters.remove(t);
          update();
        }
      }else if(page==HOME){
        if(homeIndex==1){
          if(host().username.isEmpty()){status="Set account in web Apps / Booru settings";update();return;}
          search="fav:"+host().username;
        } else search=homeIndex==0?QString():host().searches.value(homeIndex-2);
        initializeSearch(search);
        pageNumber=1;page=GALLERY;chrome();fetchPage();
      }else if(page==GALLERY&&!posts.isEmpty()){page=POST;chrome();if(autoOpenImages)loadFull();maybeLoadMore();}
      else if(page==POST)favorite();
    }
  }
public:
  Booru():Screen("booru"){
    thumbTimer.setSingleShot(true);
    connect(&thumbTimer,&QTimer::timeout,this,[this]{requestThumbs();});
    autoOpenImages=readJson(preferencesPath()).value("auto_open_images").toBool(false);
    if(!parseConfig()){Host x;x.name="Configuration error";x.url="https://e926.net";hosts<<x;}
    chrome();
  }
  void idleChanged(bool idle) override {
    thumbnailsPaused=idle;
    if(idle){
      thumbTimer.stop();
      for(auto r:thumbJobs){r->disconnect(this);r->abort();r->deleteLater();}
      thumbJobs.clear();pendingThumbnails.clear();
    } else requestThumbs();
  }
  ~Booru() override {
    thumbTimer.stop();stop(tagApi);stop(api);stop(imageJob);stop(transfer);
    for(auto r:thumbJobs){r->disconnect(this);r->abort();r->deleteLater();}
    if(saving)saving->cancelWriting();
  }
};
} // namespace
int main(int argc,char **argv){
  QApplication app(argc,argv);app.setFont(QFont("DejaVu Sans"));
  Booru window;window.show();return app.exec();
}

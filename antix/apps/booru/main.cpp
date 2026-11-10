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
  enum Page { HOME, GALLERY, POST, INFO, CONFIRM, DOWNLOADING, SETTINGS, SEARCH, LOOKUP };
  Page page=HOME;
  QNetworkAccessManager net;
  QList<Host> hosts;
  QHash<QString,QStringList> bookmarksByHost;
  QList<Post> posts;
  QMap<int,QImage> thumbnails;
  QSet<int> pendingThumbnails, failedThumbnails;
  QList<QNetworkReply*> thumbJobs;
  QTimer thumbTimer, lookupTimer, searchTimer;
  QHash<int,int> thumbAttempts;
  QHash<int,qint64> thumbRetryAt;
  qint64 lastThumbnailStart=0;
  bool thumbnailsPaused=false;
  QNetworkReply *api=nullptr, *tagApi=nullptr, *lookupApi=nullptr, *imageJob=nullptr, *transfer=nullptr;
  std::unique_ptr<QSaveFile> saving;
  QString status, search, downloadName;
  QStringList queryTerms;
  int lookupMode=0; // 0 typed tag search, 1 popular tags, 2 tags from a post
  QStringList popularTags;
  QHash<QString,int> popularTagCounts;
  int tagPage=1;
  bool tagHasMore=true,tagLoading=false;
  // 0 popular, 1 post, 2 current query terms
  int tagSource=0, tagCategory=0;
  QStringList categories={"All","General","Artists","Copyright","Characters","Species"};
  QList<int> categoryCodes={-1,0,1,3,4,5};
  QMap<QString,int> tagFilters;
  QStringList sorts={"Newest","Oldest","Top score","Most favorited","Hot","Random","Recently updated"};
  QStringList sortTerms={"","order:id","order:score","order:favcount","order:hot","order:random","order:updated"};
  QStringList ratings={"Any","Safe","Questionable","Explicit"};
  QStringList mediaTypes={"Any","Non-video","JPEG","PNG","GIF","WebP","MP4","WebM"};
  QStringList scoreLevels={"Any","10+","50+","100+"};
  int sortIndex=0,ratingIndex=0,mediaIndex=0,scoreIndex=0,searchIndex=0,tagIndex=0;
  bool tagsFromSearch=false, searchFromGallery=false, galleryFromExplore=false;
  QStringList postTagSnapshot;
  bool postTagsChanged=false;
  QString prefix;
  QStringList suggestions;
  bool lookupResultsFocus=false;
  int keyIndex=0,suggestionIndex=0;
  QString keyboardLetters="abcdefghijklmnopqrstuvwxyz0123456789_";
  int settingIndex=0, hostIndex=0, homeIndex=0, selected=0, first=0, pageNumber=1;
  qint64 received=0,total=0;
  bool autoOpenImages=false, more=false, loadingMore=false, favoritePending=false;
  bool hasResults=false;
  bool apiRetryPending=false;
  int apiRetries=0;
  quint64 searchRevision=0;

  QString preferencesPath() const {
    const QString legacy=qEnvironmentVariable("READER_APP_DATA_DIR");
    const QString dir=legacy.isEmpty()?
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation):legacy;
    return dir+"/preferences.json";
  }
  QString filesDir() const {return qEnvironmentVariable("REREAD_FILES_DIR","/var/lib/reader/files");}
  const Host &host() const {return hosts[hostIndex];}
  QStringList bookmarks() const {return bookmarksByHost.value(host().url);}
  QStringList sections() const {
    QStringList items={"Explore / build search","Recent posts","My favorites"};
    for(const auto &q:bookmarks())items<<"★ "+q.left(80);
    return items;
  }
  void savePreferences(){
    auto data=readJson(preferencesPath());
    data["auto_open_images"]=autoOpenImages;
    QJsonObject map;
    for(auto it=bookmarksByHost.cbegin();it!=bookmarksByHost.cend();++it)
      map[it.key()]=QJsonArray::fromStringList(it.value());
    data["bookmarks"]=map;writeJson(preferencesPath(),data);
  }
  void toggleBookmark(){
    const QString query=builtSearch();
    if(query.isEmpty()){status="Choose a search before bookmarking";update();return;}
    QStringList saved=bookmarks();
    if(saved.contains(query)){saved.removeAll(query);status="Bookmark removed";}
    else {
      saved.prepend(query);
      while(saved.size()>12)saved.removeLast();
      status="Bookmarked for this server";
    }
    bookmarksByHost.insert(host().url,saved);
    savePreferences();update();
  }
  QString builtSearch() const {return queryTerms.join(" ");}

  QStringList editorActions() const {
    return {"Add tag by name","Browse popular tags","Tags from selected post",
            "Add / change favorites filter","Add / change sorting","Add / change rating",
            "Add / change file type","Add / change minimum score",
            bookmarks().contains(builtSearch())?"Remove saved search":"Save this search",
            "View results"};
  }
  QStringList editorRows() const {
    QStringList names;
    for(const QString &term:queryTerms) names<<"  "+term;
    names<<editorActions();
    return names;
  }
  QString termPrefix(const QString &type) const {return type+":";}
  void setTerm(const QString &group,const QStringList &values){
    const QString prefix=termPrefix(group);
    int current=-1;
    for(const QString &term:queryTerms){
      if(term.startsWith(prefix)){current=values.indexOf(term);break;}
    }
    for(int i=queryTerms.size()-1;i>=0;--i)
      if(queryTerms[i].startsWith(prefix))queryTerms.removeAt(i);
    // 0 means no filter. Each press cycles from the current value.
    const int next=(current+1)%values.size();
    if(next>0)queryTerms<<values[next];
    searchIndex=qBound(0,searchIndex,editorRows().size()-1);
    activeSearchChanged();
  }
  void addTag(const QString &tag){
    if(tag.isEmpty()||tag.contains(' '))return;
    if(!queryTerms.contains(tag)&&!queryTerms.contains("-"+tag)){
      queryTerms<<tag;
      activeSearchChanged();
    }
  }
  void removeTerm(int index){
    if(index<0||index>=queryTerms.size())return;
    queryTerms.removeAt(index);
    searchIndex=qBound(0,searchIndex,editorRows().size()-1);
    activeSearchChanged();
  }
  void toggleExclusion(int index,bool exclude){
    if(index<0||index>=queryTerms.size())return;
    QString term=queryTerms[index];
    if(term.contains(":"))return;
    if(term.startsWith("-"))term=term.mid(1);
    queryTerms[index]=(exclude?"-":"")+term;
    activeSearchChanged();
  }
  QString selectedTermDetail() const {
    if(searchIndex<queryTerms.size()){
      const QString term=queryTerms[searchIndex];
      return "Part of the active search.\n\nOK: remove this term"+
        QString(term.contains(":")?"":"\nLeft: include\nRight: exclude");
    }
    const int action=searchIndex-queryTerms.size();
    const QStringList descriptions={
      "Choose a tag with the D-pad character picker.",
      "Browse popular tags to add to this search.",
      "Add a tag from the selected post.",
      "Add or remove fav:username. It is an ordinary editable term.",
      "Cycle through server sorting options.",
      "Choose a content rating, or none.",
      "Choose a file extension, or none.",
      "Set a minimum score, or none.",
      "Store or remove this query from Home.",
      "See posts matching the active query."
    };
    return descriptions.value(action);
  }
  QString tagState(const QString &tag) const {
    return queryTerms.contains(tag)?"Include":queryTerms.contains("-"+tag)?"Exclude":"Off";
  }
  void cycleTag(const QString &tag){
    // Tag picker adds a term. The editor is the ONLY place to modify or remove it.
    addTag(tag);
  }
  void activeSearchChanged(){
    const QString query=builtSearch();
    if(query==search){update();return;}
    if(queryTerms.size()>40){toast("Too many search terms");return;}
    search=query;
    ++searchRevision;
    searchTimer.stop();
    stop(api);
    apiRetryPending=false;loadingMore=false;
    apiRetries=0;hasResults=false;more=false;
    pageNumber=1;
    posts.clear();selected=0;first=0;
    status="Updating results...";
    searchTimer.start(500);
    update();
  }
  void fetchPopularTags(bool append=false){
    if(tagApi)return;
    if(!append){popularTags.clear();popularTagCounts.clear();tagIndex=0;tagPage=1;tagHasMore=true;}
    tagLoading=true;status=append?QString():"Loading tags...";update();
    QUrl url(host().url+"/tags.json");QUrlQuery q;
    q.addQueryItem("search[order]","count");
    if(categoryCodes[tagCategory]>=0)q.addQueryItem("search[category]",QString::number(categoryCodes[tagCategory]));
    q.addQueryItem("limit","60");q.addQueryItem("page",QString::number(tagPage));url.setQuery(q);
    auto r=net.get(request(url,true));tagApi=r;
    connect(r,&QNetworkReply::downloadProgress,this,[r](qint64 n,qint64){if(n>256*1024)r->abort();});
    connect(r,&QNetworkReply::finished,this,[this,r,append]{
      if(tagApi!=r){r->deleteLater();return;}tagApi=nullptr;tagLoading=false;
      const int code=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      const QByteArray b=r->readAll();
      const bool ok=r->error()==QNetworkReply::NoError && code>=200 && code<300 && b.size()<=256*1024;
      r->deleteLater();
      if(ok){
        const QJsonDocument doc=QJsonDocument::fromJson(b);
        const auto items=doc.array();
        tagHasMore=items.size()==60;
        for(const auto &v:items){
          const QString name=str(v.toObject().value("name"));
          if(!name.isEmpty() && !popularTagCounts.contains(name)){
            popularTags<<name;popularTagCounts.insert(name,v.toObject().value("post_count").toInt());
          }
        }
      }else if(append){tagPage=qMax(1,tagPage-1);tagHasMore=true;}
      status=!ok?"Unable to load tags (HTTP "+QString::number(code)+")":
                   popularTags.isEmpty()?"No tags available":QString();
      update();
    });
  }
  void initializeSearch(const QString &preset){
    queryTerms=preset.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
    search=builtSearch();
    searchIndex=0;
  }
  void openTagBrowser(int source){
    tagSource=source;tagIndex=0;page=INFO;status.clear();
    if(source==1){
      postTagSnapshot.clear();
      if(const Post *p=current()){
        postTagSnapshot=p->tags.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
        postTagSnapshot.removeDuplicates();
        postTagSnapshot.sort(Qt::CaseInsensitive);
      }
      postTagsChanged=false;
    }
    chrome();
    if(source==0)fetchPopularTags();
  }
  QString lookupKey(int index) const {
    return index<keyboardLetters.size()?QString(keyboardLetters[index]):
           index==keyboardLetters.size()?"DEL":"CLR";
  }
  void fetchSuggestions(){
    stop(lookupApi);
    if(prefix.isEmpty()){suggestions.clear();update();return;}
    QUrl u(host().url+"/tags.json");QUrlQuery q;
    q.addQueryItem("search[name_matches]",prefix+"*");
    q.addQueryItem("search[order]","count");
    q.addQueryItem("limit","30");u.setQuery(q);
    auto reply=net.get(request(u,true));lookupApi=reply;
    connect(reply,&QNetworkReply::downloadProgress,this,[reply](qint64 received,qint64){
      if(received>256*1024)reply->abort();
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
      if(lookupApi!=reply){reply->deleteLater();return;}
      lookupApi=nullptr;
      const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      const auto payload=reply->readAll();
      const bool ok=reply->error()==QNetworkReply::NoError && code>=200 && code<300 &&
                    payload.size()<=256*1024;
      reply->deleteLater();
      suggestions.clear();suggestionIndex=0;
      if(ok){
        for(const auto &v:QJsonDocument::fromJson(payload).array()){
          const QString name=str(v.toObject().value("name"));
          if(!name.isEmpty()&&!suggestions.contains(name)&&suggestions.size()<30)
            suggestions<<name;
        }
        status=suggestions.isEmpty()?"No matching tags":QString();
      }else{
        status="Tag lookup failed (HTTP "+QString::number(code)+")";
        if(code==429||code==503)lookupTimer.start(2500);
      }
      update();
    });
  }
  void changePrefix(const QString &key){
    if(key=="DEL"){if(!prefix.isEmpty())prefix.chop(1);}
    else if(key=="CLR")prefix.clear();
    else if(prefix.size()<40)prefix+=key;
    stop(lookupApi);lookupTimer.stop();suggestions.clear();
    suggestionIndex=0;status.clear();
    if(!prefix.isEmpty())lookupTimer.start(650);
    update();
  }
  void openTagFinder(){
    page=LOOKUP;prefix.clear();suggestions.clear();
    keyIndex=suggestionIndex=0;lookupResultsFocus=false;status.clear();chrome();
  }
  void applySearch(){
    // "Show results" only navigates. All tag/filter edits already update the
    // active query and schedule its results; there is no staged search.
    lookupTimer.stop();stop(lookupApi);stop(tagApi);
    searchFromGallery=false;galleryFromExplore=true;
    page=GALLERY;chrome();
    if(!api&&!searchTimer.isActive()&&!hasResults)fetchPage();
    else requestThumbs();
  }
  void chrome() {
    showHeader=false;

    if(page==SETTINGS) controls={{READER_UP,"Select"},{READER_OK,"Change"},{READER_BACK,"Home"}};
    else if(page==SEARCH) controls={{READER_UP,"Navigate"},{READER_OK,"Change"},{READER_OPTIONS,"View results"},{READER_BACK,searchFromGallery?"Results":"Home"}};
    else if(page==LOOKUP) controls={{READER_UP,"Select"},{READER_OK,"Choose"},{READER_VIEW_TOGGLE,"Switch panel"},{READER_ZOOM_OUT,"Delete letter"},{READER_OPTIONS,"View results"},{READER_BACK,"Explore"}};
    else if(page==HOME) controls={{READER_OK,"Open"},{READER_OPTIONS,"Settings"},{READER_BACK,"Exit"}};
    else if(page==GALLERY) controls={{READER_UP,"Browse"},{READER_OK,"View"},{READER_OPTIONS,"Refine search"},{READER_BACK,galleryFromExplore?"Explore":"Home"}};
    else if(page==POST) controls={{READER_VIEW_TOGGLE,"Open image"},{READER_OK,"Favorite"},{READER_PLAY,"Details"},{READER_OPTIONS,"Save"},{READER_BACK,"Gallery"}};
    else if(page==INFO) controls={{READER_UP,"Browse tags"},{READER_OK,"Toggle tag"},{READER_OPTIONS,"View results"},{READER_BACK,"Explore"}};
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
    req.setRawHeader("User-Agent","ReRead-Booru/0.7.1 (+https://github.com/legop3/ReRead; "+host().username.toUtf8()+")");
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
    const quint64 revision=searchRevision;
    if(!append){
      for(auto r:thumbJobs){r->disconnect(this);r->abort();r->deleteLater();}
      thumbTimer.stop();thumbJobs.clear();pendingThumbnails.clear();failedThumbnails.clear();
      thumbAttempts.clear();thumbRetryAt.clear();
      posts.clear();selected=0;first=0;
      pageNumber=1;more=false;hasResults=false;
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
    connect(r,&QNetworkReply::finished,this,[this,r,append,revision]{
      if(api!=r||revision!=searchRevision){r->deleteLater();return;}api=nullptr;loadingMore=false;
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
          QTimer::singleShot(delay,this,[this,append,expectedSearch,expectedPage,revision]{
            if(!apiRetryPending||revision!=searchRevision||page==HOME||page==SETTINGS)return;
            apiRetryPending=false;
            if(search==expectedSearch&&
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
      more=items.size()>=30;hasResults=true;
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
  QList<int> thumbnailTargets() const {
    QList<int> wanted;
    if(page==POST){
      if(selected>=0&&selected<posts.size())wanted<<selected;
      for(int distance=1;distance<=3;++distance){
        if(selected+distance<posts.size())wanted<<selected+distance;
        if(selected-distance>=0)wanted<<selected-distance;
      }
    }else if(page==GALLERY){
      for(int i=first;i<qMin(first+6,posts.size());++i)wanted<<i;
    }
    return wanted;
  }
  void requestThumbs(){
    if((page!=GALLERY && page!=POST) || thumbnailsPaused || api ||
       tagApi || transfer || posts.isEmpty())return;
    // Gallery requests its six visible tiles. Post mode requests the current
    // thumbnail FIRST and then nearby cards, regardless of gallery scroll offset.
    const QList<int> wanted=thumbnailTargets();
    if(thumbJobs.size()>=2)return;
    const qint64 now=QDateTime::currentMSecsSinceEpoch();
    const int interval=550;
    if(lastThumbnailStart && now-lastThumbnailStart<interval){
      const int delay=int(lastThumbnailStart+interval-now);
      if(!thumbTimer.isActive()||thumbTimer.remainingTime()>delay)thumbTimer.start(delay);
      return;
    }
    qint64 soonest=0;
    for(const int i:wanted){
      const Post &post=posts[i];
      const int id=post.id;
      if(thumbnails.contains(id)||pendingThumbnails.contains(id)||
         failedThumbnails.contains(id))continue;
      QUrl u=imageUrl(post.preview.isEmpty()?post.sample:post.preview);
      if(!u.isValid()){failedThumbnails.insert(id);continue;}
      if(thumbRetryAt.value(id)>now){
        const qint64 wait=thumbRetryAt.value(id)-now;
        if(!soonest||wait<soonest)soonest=wait;
        continue;
      }
      QNetworkRequest req=request(u,false);
      req.setTransferTimeout(15000);
      auto reply=net.get(req);
      lastThumbnailStart=now;thumbJobs<<reply;pendingThumbnails.insert(id);
      connect(reply,&QNetworkReply::downloadProgress,this,[reply](qint64 n,qint64){
        if(n>512*1024)reply->abort();
      });
      connect(reply,&QNetworkReply::finished,this,[this,reply,id]{
        thumbJobs.removeAll(reply);pendingThumbnails.remove(id);
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray payload=reply->readAll();
        const bool ok=reply->error()==QNetworkReply::NoError&&
                      code>=200&&code<300&&payload.size()<=512*1024;
        if(ok){
          QBuffer buffer(&payload);buffer.open(QIODevice::ReadOnly);
          QImageReader reader(&buffer);reader.setAutoTransform(true);
          const QSize native=reader.size();
          if(native.isValid())reader.setScaledSize(native.scaled(320,260,Qt::KeepAspectRatio));
          const QImage im=reader.read();
          if(!im.isNull()){
            thumbnails.insert(id,im);thumbAttempts.remove(id);thumbRetryAt.remove(id);
            while(thumbnails.size()>60){
              int farthest=-1,distance=-1;
              for(auto it=thumbnails.cbegin();it!=thumbnails.cend();++it){
                int index=-1;
                for(int j=0;j<posts.size();++j)if(posts[j].id==it.key()){index=j;break;}
                const int d=index<0?999999:qAbs(index-selected);
                if(d>distance){farthest=it.key();distance=d;}
              }
              if(farthest<0)break;
              thumbnails.remove(farthest);
            }
          }else failedThumbnails.insert(id);
        }else if(code==429||code==503||code==502||code==504||
                 reply->error()==QNetworkReply::TimeoutError||
                 reply->error()==QNetworkReply::TemporaryNetworkFailureError||
                 reply->error()==QNetworkReply::RemoteHostClosedError){
          const int count=qMin(6,thumbAttempts.value(id)+1);
          thumbAttempts.insert(id,count);
          int seconds=qMin(60,2*(1<<qMin(4,count)));
          bool valid=false;const int retry=reply->rawHeader("Retry-After").toInt(&valid);
          if(valid)seconds=qBound(1,retry,120);
          thumbRetryAt.insert(id,QDateTime::currentMSecsSinceEpoch()+seconds*1000LL);
          // Rate limits are never permanent failures: retry after cooldown.
        }else failedThumbnails.insert(id);
        reply->deleteLater();
        if(page==GALLERY||page==POST){requestThumbs();update();}
      });
      if(!thumbTimer.isActive()||thumbTimer.remainingTime()>interval)thumbTimer.start(interval);
      return;
    }
    if(soonest){
      const int delay=qBound(1,int(soonest),120000);
      if(!thumbTimer.isActive()||thumbTimer.remainingTime()>delay)thumbTimer.start(delay);
    }
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
      if(!ok){status=message;requestThumbs();update();return;}
      if(cacheImage(file,bytes))openCached(file);
      requestThumbs();
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
    else if(page==SEARCH){page=searchFromGallery?GALLERY:HOME;}
    else if(page==LOOKUP){lookupTimer.stop();stop(lookupApi);page=SEARCH;}
    else if(page==INFO){stop(tagApi);page=tagsFromSearch?SEARCH:postTagsChanged?GALLERY:POST;}
    else if(page==DOWNLOADING){stop(transfer);if(saving)saving->cancelWriting();saving.reset();page=POST;}
    else if(page==POST||page==CONFIRM){
      stop(imageJob);
      if(selected<first)first=(selected/3)*3;
      if(selected>=first+6)first=qMax(0,(selected/3-1)*3);
      page=GALLERY;requestThumbs();
    }
    else if(page==GALLERY){
      stop(api);loadingMore=false;apiRetryPending=false;
      if(galleryFromExplore){page=SEARCH;searchFromGallery=false;}
      else {page=HOME;homeIndex=qBound(0,homeIndex,sections().size()-1);}
    }
    else {close();return;}
    if(page!=GALLERY || !hasResults)status.clear();
    chrome();
    if(page==GALLERY){requestThumbs();maybeLoadMore();}
  }
  void navigate(int dx,int dy){
    if(page==SETTINGS){settingIndex=qBound(0,settingIndex+dx+dy,1);}
    else if(page==SEARCH){searchIndex=qBound(0,searchIndex+dx+dy,10);}
    else if(page==LOOKUP){
      if(lookupResultsFocus)suggestionIndex=qBound(0,suggestionIndex+dx+dy,qMax(0,suggestions.size()-1));
      else keyIndex=qBound(0,keyIndex+dx+dy*6,keyboardLetters.size()+1);
    }
    else if(page==INFO){
      tagIndex=qBound(0,tagIndex+dx+dy,qMax(0,currentTags().size()-1));
      if(tagSource==0 && !tagLoading && !tagApi && tagHasMore &&
         tagIndex>=popularTags.size()-7 && !popularTags.isEmpty()){
        ++tagPage;fetchPopularTags(true);
      }
    }
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
        if(next!=selected){selected=next;stop(imageJob);status.clear();requestThumbs();if(autoOpenImages)loadFull();maybeLoadMore();}}

    }
    update();
  }
protected:
  void draw(QPainter &p,const QRect &r) override {
    // Screen paints ReRead's palette and standard controls. No app-created header.
    const QRect area=r.adjusted(4,3,-4,-3);
    if(page==SETTINGS) {
      list(p,area,{"Server", "Open images automatically"},{host().name,autoOpenImages?"On":"Off · use View on a post"},settingIndex);
    } else if(page==LOOKUP) {
      // Compact D-pad character picker; no system keyboard or text input required.
      const QRect left(area.x()+4,area.y()+4,area.width()*45/100,area.height()-8);
      const QRect right(left.right()+20,area.y()+4,area.right()-left.right()-24,area.height()-8);
      label(p,QRect(left.x(),left.y(),left.width(),38),"Find a tag",28,true,palette().color(QPalette::Link));
      label(p,QRect(left.x(),left.y()+43,left.width(),43),
            prefix.isEmpty()?"Prefix: (empty)":"Prefix: "+prefix,23,true);
      const int gap=5,cols=6;
      const int width=(left.width()-(cols-1)*gap)/cols;
      const int height=51;
      for(int i=0;i<keyboardLetters.size()+2;++i){
        const int x=i%cols,y=i/cols;
        const QRect cell(left.x()+x*(width+gap),left.y()+99+y*(height+gap),width,height);
        const bool chosen=!lookupResultsFocus&&keyIndex==i;
        if(chosen)p.fillRect(cell,palette().highlight());
        else {p.setPen(QPen(palette().mid(),1));p.drawRect(cell.adjusted(1,1,-1,-1));}
        label(p,cell.adjusted(3,0,-3,0),lookupKey(i),21,chosen,
              palette().color(chosen?QPalette::HighlightedText:QPalette::Text));
      }
      label(p,QRect(right.x(),right.y(),right.width(),40),
            "Matching tags",26,true,palette().color(QPalette::Link));
      const int visible=8;
      const int firstMatch=(suggestionIndex/visible)*visible;
      for(int j=0;j<visible&&firstMatch+j<suggestions.size();++j){
        const int i=firstMatch+j;
        const QRect line(right.x(),right.y()+52+j*49,right.width(),44);
        const bool chosen=lookupResultsFocus&&i==suggestionIndex;
        if(chosen)p.fillRect(line,palette().highlight());
        else {p.setPen(QPen(palette().mid(),1));p.drawLine(line.left(),line.bottom(),line.right(),line.bottom());}
        const QString name=suggestions[i];
        label(p,line.adjusted(10,0,-8,0),
              (tagState(name)=="Include"?"+ ":tagState(name)=="Exclude"?"− ":"")+name,
              21,chosen,
              palette().color(chosen?QPalette::HighlightedText:QPalette::Text));
      }
      if(suggestions.isEmpty())
        text(p,QRect(right.x()+5,right.y()+76,right.width()-10,150),
             prefix.isEmpty()?"Select letters to find matching tags.":"Finding matches...",21);
      label(p,QRect(right.x(),right.bottom()-43,right.width(),37),
            !hasResults?"UPDATING POSTS":"ACTIVE SEARCH",18,
            true,palette().color(QPalette::Link));
    } else if(page==SEARCH) {
      const QStringList names={"Find tag by name","Browse popular tags","Tag category","Tags on selected post","Edit active tags",
        "Sort order","Rating","File type","Minimum score",
        bookmarks().contains(builtSearch())?"Remove saved search":"Save this search","VIEW RESULTS"};
      const QStringList descriptions={"Build a tag prefix using the D-pad","Explore tags without typing",
        categories[tagCategory],"Find related tags","Remove or change tags",sorts[sortIndex],ratings[ratingIndex],
        mediaTypes[mediaIndex],scoreLevels[scoreIndex],"Available from Home","Open matching posts"};
      const auto layout=listInfo(p,area,names.size(),searchIndex,53,0.54);
      for(int i=layout.first;i<qMin(layout.first+layout.visible,names.size());++i){
        const QRect line=listInfoRow(p,layout,i,searchIndex);
        const bool chosen=i==searchIndex;
        label(p,line.adjusted(12,0,-9,0),names[i],22,chosen,
              palette().color(chosen?QPalette::HighlightedText:QPalette::Text));
      }
      const QRect right=layout.info.adjusted(10,10,-9,-8);
      label(p,QRect(right.left(),right.top(),right.width(),37),"Explore",29,true,palette().color(QPalette::Link));
      label(p,QRect(right.left(),right.top()+44,right.width(),44),descriptions[searchIndex],20);
      p.setPen(QPen(palette().mid(),1));p.drawLine(right.left(),right.top()+94,right.right(),right.top()+94);
      QStringList active;
      for(const auto &token:baseSearch.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts)){
        const QString bare=token.startsWith("-")?token.mid(1):token;
        if(token.contains(":")||tagFilters.contains(bare))continue;
        active<<(token.startsWith("-")?"− ":"+ ")+bare;
      }
      for(auto it=tagFilters.cbegin();it!=tagFilters.cend();++it)
        if(it.value())active<<(it.value()<0?"− ":"+ ")+it.key();
      label(p,QRect(right.left(),right.top()+107,right.width(),33),"Tags in this search",21,true);
      text(p,QRect(right.left(),right.top()+144,right.width(),qMax(40,right.height()-245)),
           active.isEmpty()?"No tag restrictions":active.join("   "),21);
      const QString query=builtSearch();
      label(p,QRect(right.left(),right.bottom()-88,right.width(),30),
            !hasResults?"UPDATING POSTS":"ACTIVE SEARCH",18,true,
            palette().color(QPalette::Link));
      label(p,QRect(right.left(),right.bottom()-50,right.width(),38),
            query.isEmpty()?"All posts":query,17);
    } else if(page==HOME) {
      const QStringList names=sections();
      const auto layout=listInfo(p,area,names.size(),homeIndex,66,0.57);
      for(int i=layout.first;i<qMin(layout.first+layout.visible,names.size());++i){
        const QRect line=listInfoRow(p,layout,i,homeIndex);
        const bool chosen=i==homeIndex;
        label(p,line.adjusted(12,0,-10,0),names[i],24,chosen,
              palette().color(chosen?QPalette::HighlightedText:QPalette::Text));
      }
      const QRect side=layout.info.adjusted(10,12,-8,-10);
      label(p,QRect(side.left(),side.top(),side.width(),45),"Booru",30,true,palette().color(QPalette::Link));
      label(p,QRect(side.left(),side.top()+54,side.width(),38),host().name,23,true);
      text(p,QRect(side.left(),side.top()+109,side.width(),190),
           homeIndex==0?"Build searches with tags, ratings and sorting. No keyboard required.":
           homeIndex==1?"Browse the latest posts.":
           homeIndex==2?"Browse your account favorites.":"Open a saved search.",23);
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
          const QString rule=tagState(tag);
          label(p,QRect(layout.info.x()+8,layout.info.y()+12,layout.info.width()-16,45),
                tag,27,true);
          text(p,QRect(layout.info.x()+8,layout.info.y()+72,layout.info.width()-16,180),
               "Filter: "+rule+
               (tagSource==0?"\nUsed in "+QString::number(popularTagCounts.value(tag))+" posts":"")+
               (!hasResults?"\n\nUpdating matching posts...":"\n\nActive search updated"),22);
          label(p,QRect(layout.info.x()+8,layout.info.bottom()-46,layout.info.width()-16,35),
                tagSource==0?"Page "+QString::number(tagPage)+" · "+QString::number(names.size())+" tags":
                  QString::number(tagFilters.size())+" overrides",19);
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
    else if(state!=1)return;
    else if(page==LOOKUP&&key==READER_VIEW_TOGGLE){
      lookupResultsFocus=!lookupResultsFocus;update();
    }else if(page==LOOKUP&&key==READER_ZOOM_OUT)changePrefix("DEL");
    else if(page==POST&&key==READER_VIEW_TOGGLE)loadFull();
    else if(key==READER_OPTIONS){
      if(page==HOME){page=SETTINGS;settingIndex=0;status.clear();chrome();}
      else if(page==GALLERY){
        searchFromGallery=true;page=SEARCH;searchIndex=0;status.clear();chrome();
      }else if(page==SEARCH||page==INFO||page==LOOKUP)applySearch();
      else if(page==POST)download();
    }else if(key==READER_PLAY&&page==POST){
      tagsFromSearch=false;openTagBrowser(1);
    }else if(key==READER_OK){
      if(page==SETTINGS){
        if(settingIndex==0){
          hostIndex=(hostIndex+1)%hosts.size();homeIndex=0;
          // Post IDs are not globally unique across configured servers.
          thumbTimer.stop();
          for(auto reply:thumbJobs){reply->disconnect(this);reply->abort();reply->deleteLater();}
          thumbJobs.clear();pendingThumbnails.clear();failedThumbnails.clear();
          thumbAttempts.clear();thumbRetryAt.clear();thumbnails.clear();
          posts.clear();selected=first=0;
          initializeSearch("");
        }
        else {autoOpenImages=!autoOpenImages;savePreferences();}
        update();
      }else if(page==SEARCH){
        if(searchIndex==0)openTagFinder();
        else if(searchIndex==1){tagsFromSearch=true;openTagBrowser(0);}
        else if(searchIndex==2){tagCategory=(tagCategory+1)%categories.size();update();}
        else if(searchIndex==3){
          if(posts.isEmpty())toast("Open a post first to browse its tags");
          else {tagsFromSearch=true;openTagBrowser(1);}
        }else if(searchIndex==4){tagsFromSearch=true;openTagBrowser(2);}
        else if(searchIndex==5){sortIndex=(sortIndex+1)%sorts.size();activeSearchChanged();}
        else if(searchIndex==6){ratingIndex=(ratingIndex+1)%ratings.size();activeSearchChanged();}
        else if(searchIndex==7){mediaIndex=(mediaIndex+1)%mediaTypes.size();activeSearchChanged();}
        else if(searchIndex==8){scoreIndex=(scoreIndex+1)%scoreLevels.size();activeSearchChanged();}
        else if(searchIndex==9)toggleBookmark();
        else applySearch();
      }else if(page==LOOKUP){
        if(lookupResultsFocus){
          if(suggestionIndex<suggestions.size()){
            cycleTag(suggestions[suggestionIndex]);
            toast(suggestions[suggestionIndex]+": "+tagState(suggestions[suggestionIndex]));
          }
        }else changePrefix(lookupKey(keyIndex));
      }else if(page==INFO){
        const auto names=currentTags();
        if(tagIndex<names.size())cycleTag(names[tagIndex]);
      }else if(page==HOME){
        if(homeIndex==0){
          initializeSearch("");searchFromGallery=false;searchIndex=0;
          posts.clear();selected=first=0;hasResults=false;
          page=SEARCH;status.clear();chrome();
          ++searchRevision;searchTimer.start(0);return;
        }
        if(homeIndex==1)initializeSearch("");
        else if(homeIndex==2){
          if(host().username.isEmpty()){toast("Add account in web Apps / Booru settings");return;}
          initializeSearch("fav:"+host().username);
        }else initializeSearch(bookmarks().value(homeIndex-3));
        galleryFromExplore=false;
        pageNumber=1;page=GALLERY;chrome();fetchPage();
      }else if(page==GALLERY&&!posts.isEmpty()){
        page=POST;chrome();requestThumbs();if(autoOpenImages)loadFull();maybeLoadMore();
      }else if(page==POST)favorite();
    }
  }

public:
  Booru():Screen("booru"){
    setWindowTitle("Booru");
    thumbTimer.setSingleShot(true);
    connect(&thumbTimer,&QTimer::timeout,this,[this]{requestThumbs();});
    searchTimer.setSingleShot(true);
    connect(&searchTimer,&QTimer::timeout,this,[this]{
      if(!api && page!=HOME && page!=SETTINGS)fetchPage();
    });
    lookupTimer.setSingleShot(true);
    connect(&lookupTimer,&QTimer::timeout,this,[this]{if(page==LOOKUP)fetchSuggestions();});
    const auto prefs=readJson(preferencesPath());
    autoOpenImages=prefs.value("auto_open_images").toBool(false);
    const auto saved=prefs.value("bookmarks").toObject();
    for(auto it=saved.begin();it!=saved.end();++it){
      QStringList searches;
      for(const auto &v:it.value().toArray())
        if(v.isString()&&!v.toString().isEmpty()&&searches.size()<12)
          searches<<v.toString().left(500);
      if(!searches.isEmpty())bookmarksByHost.insert(it.key(),searches);
    }
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
    thumbTimer.stop();lookupTimer.stop();searchTimer.stop();stop(lookupApi);stop(tagApi);stop(api);stop(imageJob);stop(transfer);
    for(auto r:thumbJobs){r->disconnect(this);r->abort();r->deleteLater();}
    if(saving)saving->cancelWriting();
  }
};
} // namespace
int main(int argc,char **argv){
  QApplication app(argc,argv);
  app.setApplicationName("reread-booru");
  app.setDesktopFileName("reread-booru");
  app.setFont(QFont("DejaVu Sans"));
  Booru window;window.show();return app.exec();
}

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
  QMap<int,QStringList> categorizedTags;
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
  enum Page { HOME, GALLERY, POST, DOWNLOADING, SEARCH, LOOKUP };
  Page page=HOME;
  Page downloadReturnPage=POST;
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
  QNetworkReply *api=nullptr, *tagApi=nullptr, *lookupApi=nullptr, *imageJob=nullptr, *transfer=nullptr, *favoriteJob=nullptr;
  std::unique_ptr<QSaveFile> saving;
  QString status, search, downloadName;
  QStringList queryTerms;
  int lookupMode=0; // 0 typed tag search, 1 popular tags, 2 tags from a post
  QStringList popularTags;
  QHash<QString,int> popularTagCounts;
  QHash<QString,int> popularTagCategories;
  QList<int> suggestionCategories;
  int tagPage=1;
  bool tagHasMore=true,tagLoading=false;

  int searchIndex=0;
  QStringList postTagSnapshot;
  QMap<int,QStringList> postCategorySnapshot;
  QString prefix;
  QStringList suggestions;
  bool lookupResultsFocus=false;
  int keyIndex=0,suggestionIndex=0;
  QString keyboardLetters="abcdefghijklmnopqrstuvwxyz0123456789_";
  int hostIndex=0, homeIndex=0, selected=0, first=0, pageNumber=1;
  qint64 received=0,total=0;
  bool more=false, loadingMore=false, favoritePending=false;
  bool hasResults=false;
  bool apiRetryPending=false;
  int apiRetries=0;
  quint64 searchRevision=0;

  // Category IDs and post-sidebar ordering match e621ng TagCategory.
  static QList<int> siteCategoryOrder(){
    return {6,1,2,3,4,5,0,7,8};
  }
  static QString categoryName(int category){
    static const QStringList names={"General","Artists","Contributors","Copyrights",
      "Characters","Species","Invalid","Meta","Lore"};
    return category>=0&&category<names.size()?names[category]:"Other";
  }
  static QColor categoryColor(int category){
    // e621 default Hexagon theme category colors, with darker variants used
    // as text when the shared ReRead surface is light.
    static const QStringList colors={"#b4c7d9","#f2ac08","#c0c0c0","#dd00dd",
       "#00aa00","#ed5d1f","#ff3d3d","#ffffff","#228822"};
    if(category<0||category>=colors.size())return QColor();
    return QColor(colors[category]);
  }
  QColor categoryTextColor(int category) const {
    const QColor source=categoryColor(category);
    if(!source.isValid())return palette().color(QPalette::Text);
    const QColor background=palette().color(QPalette::Base);
    if(background.lightness()<120)return source;
    // Keep neutral meta/contributor categories neutral on ReRead's light UI.
    if(source.hslSaturation()<25)return QColor("#555555");
    // Maintain e621's category hue while providing legible light-UI text.
    return QColor::fromHsl(source.hslHue(),qMax(95,source.hslSaturation()),
                           qMin(100,source.lightness()));
  }
  static QMap<int,QStringList> parseCategories(const QJsonObject &tags){
    QMap<int,QStringList> result;
    const QStringList keys={"general","artist","contributor","copyright",
      "character","species","invalid","meta","lore"};
    for(int i=0;i<keys.size();++i){
      QStringList names;
      for(const auto &v:tags.value(keys[i]).toArray())
        if(v.isString()&&!v.toString().isEmpty())names<<v.toString();
      if(!names.isEmpty()){
        names.removeDuplicates();
        names.sort(Qt::CaseInsensitive);
        result.insert(i,names);
      }
    }
    return result;
  }
  void setSuggestionsFromCategories(const QMap<int,QStringList> &categories){
    suggestions.clear();suggestionCategories.clear();
    for(int category:siteCategoryOrder()){
      for(const QString &name:categories.value(category)){
        suggestions<<name;suggestionCategories<<category;
      }
    }
    suggestionIndex=0;
  }
  int suggestionCategory(int index) const {
    return index>=0&&index<suggestionCategories.size()?
      suggestionCategories[index]:-1;
  }
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
    QStringList items={"Search editor","Recent posts","My favorites"};
    for(const auto &q:bookmarks())items<<"★ "+q.left(80);
    if(hosts.size()>1)items<<"Switch server: "+host().name;
    return items;
  }
  void savePreferences(){
    auto data=readJson(preferencesPath());
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
    const int actionIndex=searchIndex-queryTerms.size();
    const QString prefix=termPrefix(group);
    int current=0;
    for(const QString &term:queryTerms){
      if(term.startsWith(prefix)){current=values.indexOf(term);break;}
    }
    for(int i=queryTerms.size()-1;i>=0;--i)
      if(queryTerms[i].startsWith(prefix))queryTerms.removeAt(i);
    // 0 means no filter. Each press cycles from the current value.
    const int next=(current+1)%values.size();
    if(next>0)queryTerms<<values[next];
    searchIndex=qBound(0,queryTerms.size()+actionIndex,editorRows().size()-1);
    activeSearchChanged();
  }
  void addTag(const QString &tag){
    if(tag.isEmpty()||tag.contains(' '))return;
    if(!queryTerms.contains(tag)&&!queryTerms.contains("-"+tag)){
      queryTerms<<tag;
      searchIndex=queryTerms.size()-1;
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
  void changeTerm(int index,int direction){
    if(index<0||index>=queryTerms.size())return;
    const QString term=queryTerms[index];
    if(!term.contains(":")){
      toggleExclusion(index,direction>0);return;
    }
    const QStringList groups={"rating","order","type","score"};
    const QList<QStringList> choices={
      {"rating:s","rating:q","rating:e"},
      {"order:id","order:score","order:favcount","order:hot","order:random","order:updated"},
      {"type:jpg","type:png","type:gif","type:webp","type:mp4","type:webm"},
      {"score:>=10","score:>=50","score:>=100"}
    };
    for(int k=0;k<groups.size();++k){
      if(!term.startsWith(groups[k]+":"))continue;
      const int at=choices[k].indexOf(term);
      const int next=(at+direction+choices[k].size())%choices[k].size();
      queryTerms[index]=choices[k][next];
      activeSearchChanged();
      return;
    }
  }
  QString selectedTermDetail() const {
    if(searchIndex<queryTerms.size()){
      const QString term=queryTerms[searchIndex];
      return "Part of the active search.\n\nOK: remove this term"+
        QString(term.startsWith("fav:")?"":term.contains(":")?"\nLeft / Right: change value":"\nLeft: include\nRight: exclude");
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
  void activeSearchChanged(){
    const QString query=builtSearch();
    if(query==search){update();return;}
    if(queryTerms.size()>40){
      queryTerms=search.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
      searchIndex=qBound(0,searchIndex,editorRows().size()-1);
      toast("Maximum 40 search terms");update();return;
    }
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
    chrome();
  }
  void fetchPopularTags(bool append=false){
    if(tagApi)return;
    if(!append){popularTags.clear();popularTagCounts.clear();popularTagCategories.clear();suggestionIndex=0;tagPage=1;tagHasMore=true;}
    tagLoading=true;status=append?QString():"Loading tags...";update();
    QUrl url(host().url+"/tags.json");QUrlQuery q;
    q.addQueryItem("search[order]","count");
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
            popularTagCategories.insert(name,v.toObject().value("category").toInt(-1));
          }
        }
      }else if(append){tagPage=qMax(1,tagPage-1);tagHasMore=true;}
      status=!ok?"Unable to load tags (HTTP "+QString::number(code)+")":
                   popularTags.isEmpty()?"No tags available":QString();
      if(page==LOOKUP && lookupMode==1){
        suggestions=popularTags;suggestionCategories.clear();
        for(const QString &name:suggestions)suggestionCategories<<popularTagCategories.value(name,-1);
      }
      update();
    });
  }
  void initializeSearch(const QString &preset){
    queryTerms=preset.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
    search=builtSearch();
    searchIndex=0;
  }
  void openTagBrowser(int source){
    lookupMode=source==0?1:2;
    page=LOOKUP;prefix.clear();suggestions.clear();suggestionCategories.clear();suggestionIndex=0;
    lookupResultsFocus=true;status.clear();
    if(lookupMode==1)fetchPopularTags();
    else {
      // The post may already have disappeared when search results refreshed.
      // Preserve the original API tag categories captured on entering editor.
      if(!postCategorySnapshot.isEmpty())setSuggestionsFromCategories(postCategorySnapshot);
      else if(const Post *post=current())setSuggestionsFromCategories(post->categorizedTags);
      else {
        suggestions=postTagSnapshot;
        for(int i=0;i<suggestions.size();++i)suggestionCategories<<0;
      }
    }
    chrome();
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
      suggestions.clear();suggestionCategories.clear();suggestionIndex=0;
      if(ok){
        for(const auto &v:QJsonDocument::fromJson(payload).array()){
          const QString name=str(v.toObject().value("name"));
          if(!name.isEmpty()&&!suggestions.contains(name)&&suggestions.size()<30){
            suggestions<<name;
            suggestionCategories<<v.toObject().value("category").toInt(-1);
          }
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
    stop(lookupApi);lookupTimer.stop();suggestions.clear();suggestionCategories.clear();
    suggestionIndex=0;status.clear();
    if(!prefix.isEmpty())lookupTimer.start(650);
    update();
  }
  void openTagFinder(){
    lookupMode=0;
    page=LOOKUP;prefix.clear();suggestions.clear();suggestionCategories.clear();
    keyIndex=suggestionIndex=0;lookupResultsFocus=false;status.clear();chrome();
  }
  void applySearch(){
    // "Show results" only navigates. All tag/filter edits already update the
    // active query and schedule its results; there is no staged search.
    lookupTimer.stop();stop(lookupApi);stop(tagApi);
    page=GALLERY;chrome();
    if(!api&&!searchTimer.isActive()&&!hasResults)fetchPage();
    else requestThumbs();
  }
  void chrome(){
    showHeader=false;
    controls.clear();
    if(page==HOME){
      controls={{READER_UP,"Choose"},
                {READER_OK,hosts.size()>1&&homeIndex==sections().size()-1?
                   "Switch server":"Open"},
                {READER_OPTIONS,"Edit search"},{READER_BACK,"Exit"}};
    }else if(page==SEARCH){
      controls={{READER_UP,"Select"},
                {READER_OK,searchIndex<queryTerms.size()?"Remove term":"Choose"},
                {READER_OPTIONS,"Edit search"},{READER_BACK,"Results"}};
      if(searchIndex<queryTerms.size()&&!queryTerms[searchIndex].startsWith("fav:")){
        controls.prepend({READER_LEFT,"Previous / Include"});
        controls.prepend({READER_RIGHT,"Next / Exclude"});
      }
    }else if(page==LOOKUP){
      controls={{READER_UP,lookupMode==0&&!lookupResultsFocus?"Choose character":"Choose tag"},
                {READER_OK,lookupMode==0&&!lookupResultsFocus?"Type character":"Add tag"},
                {READER_OPTIONS,"Edit search"},{READER_BACK,"Search editor"}};
      if(lookupMode==0){
        controls.append({READER_VIEW_TOGGLE,"Keyboard / matches"});
        controls.append({READER_ZOOM_OUT,"Delete character"});
      }
    }else if(page==GALLERY||page==POST){
      controls={{page==POST?READER_LEFT:READER_UP,
                 page==POST?"Previous / next":"Browse grid"},
                {READER_OK,page==POST?"Open image":"Post details"},
                {READER_VIEW_TOGGLE,"View image"},
                {READER_PLAY,"Toggle favorite"},
                {READER_SHOOT,"Download original"},
                {READER_OPTIONS,"Edit search"},
                {READER_BACK,page==POST?"Gallery":"Home"}};
    }else if(page==DOWNLOADING){
      controls={{READER_OPTIONS,"Edit search"},{READER_BACK,"Cancel download"}};
    }else{
      controls={{READER_OPTIONS,"Edit search"},{READER_BACK,"Return"}};
    }
    controls.append({READER_FAVORITES,"Close app"});
    update();
  }
  void openSearchEditor(){
    if(page==GALLERY||page==POST)rememberSelectedPostTags();
    stop(imageJob);
    if(page==LOOKUP){
      lookupTimer.stop();stop(lookupApi);stop(tagApi);
    }
    if(page==DOWNLOADING){
      stop(transfer);
      if(saving)saving->cancelWriting();
      saving.reset();
    }
    if(page==SEARCH)return; // Options already means edit this search.
    page=SEARCH;
    searchIndex=qBound(0,searchIndex,editorRows().size()-1);
    status.clear();
    chrome();
  }
  void switchServer(){
    if(hosts.size()<2){toast("Only one server configured");return;}
    thumbTimer.stop();searchTimer.stop();lookupTimer.stop();
    stop(api);stop(tagApi);stop(lookupApi);stop(imageJob);stop(favoriteJob);
    favoritePending=false;
    for(auto reply:thumbJobs){
      reply->disconnect(this);reply->abort();reply->deleteLater();
    }
    thumbJobs.clear();pendingThumbnails.clear();failedThumbnails.clear();
    thumbAttempts.clear();thumbRetryAt.clear();thumbnails.clear();
    ++searchRevision;
    apiRetryPending=false;loadingMore=false;hasResults=false;more=false;
    hostIndex=(hostIndex+1)%hosts.size();
    homeIndex=sections().size()-1;
    posts.clear();selected=first=0;pageNumber=1;
    postTagSnapshot.clear();postCategorySnapshot.clear();
    initializeSearch("");
    status="Server: "+host().name;
    chrome();
  }
  void openHomeSelection(){
    if(hosts.size()>1&&homeIndex==sections().size()-1){switchServer();return;}
    // Search editor is a *resume* action. Do not reset or refetch its query.
    if(homeIndex==0){page=SEARCH;status.clear();chrome();return;}
    postTagSnapshot.clear();postCategorySnapshot.clear();
    if(homeIndex==1)initializeSearch("");
    else if(homeIndex==2){
      if(host().username.isEmpty()){
        toast("Configure your username in web Apps / Booru settings");
        return;
      }
      initializeSearch("fav:"+host().username);
    }else initializeSearch(bookmarks().value(homeIndex-3));
    ++searchRevision;
    searchTimer.stop();stop(api);
    apiRetryPending=false;loadingMore=false;
    hasResults=false;more=false;
    posts.clear();selected=first=0;pageNumber=1;
    // Recent posts is a direct browsing shortcut. Favorites and saved searches
    // open the same editor, where every tag and metatag can be changed.
    if(homeIndex==1){page=GALLERY;chrome();fetchPage();}
    else {page=SEARCH;chrome();searchTimer.start(0);}
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
    req.setRawHeader("User-Agent","ReRead-Booru/0.9.0 (+https://github.com/legop3/ReRead; "+host().username.toUtf8()+")");
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
            if(!apiRetryPending||revision!=searchRevision||page==HOME)return;
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
        const QJsonObject tag=o.value("tags").toObject();
        p.categorizedTags=parseCategories(tag);
        QStringList all;
        for(int category:siteCategoryOrder())all<<p.categorizedTags.value(category);
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
  void rememberSelectedPostTags(){
    postTagSnapshot.clear();postCategorySnapshot.clear();
    if(const Post *p=current()){
      postCategorySnapshot=p->categorizedTags;
      postTagSnapshot=p->tags.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
      postTagSnapshot.removeDuplicates();
    }
  }
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
    favoriteJob=r;
    connect(r,&QNetworkReply::finished,this,[this,r,id,add]{
      if(favoriteJob!=r){r->deleteLater();return;}
      favoriteJob=nullptr;favoritePending=false;
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
    stop(imageJob);
    downloadReturnPage=page;
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
      status=ok?"Saved to Files / Pictures / Booru":"Download failed";page=downloadReturnPage;chrome();
      requestThumbs();
    });
  }
  void back(){
    // A single hierarchy prevents Back from oscillating between two screens:
    // Home -> Search -> Results -> Post. Pickers return to Search.
    if(page==HOME){close();return;}
    if(page==LOOKUP){
      lookupTimer.stop();stop(lookupApi);stop(tagApi);
      page=SEARCH;
    }else if(page==SEARCH){
      applySearch();
      return;
    }else if(page==DOWNLOADING){
      stop(transfer);
      if(saving)saving->cancelWriting();
      saving.reset();
      page=downloadReturnPage;
    }else if(page==POST){
      stop(imageJob);
      if(selected<first)first=(selected/3)*3;
      if(selected>=first+6)first=qMax(0,(selected/3-1)*3);
      page=GALLERY;
    }else if(page==GALLERY){
      stop(imageJob);
      page=HOME;
    }
    status.clear();
    chrome();
    if(page==GALLERY){requestThumbs();maybeLoadMore();}
  }
  void navigate(int dx,int dy){
    if(page==HOME && dy){
      homeIndex=qBound(0,homeIndex+dy,sections().size()-1);
      chrome();
    }
    else if(page==SEARCH){
      if(dy)searchIndex=qBound(0,searchIndex+dy,editorRows().size()-1);
      if(dx && searchIndex<queryTerms.size())changeTerm(searchIndex,dx);
      chrome();
    }else if(page==LOOKUP){
      if(lookupMode==0 && !lookupResultsFocus)
        keyIndex=qBound(0,keyIndex+dx+dy*6,keyboardLetters.size()+1);
      else if(dy){
        suggestionIndex=qBound(0,suggestionIndex+dy,qMax(0,suggestions.size()-1));
        if(lookupMode==1&&!tagApi&&tagHasMore &&
           !suggestions.isEmpty()&&suggestionIndex>=suggestions.size()-7){
          ++tagPage;fetchPopularTags(true);
        }
      }
    }else if(page==GALLERY){
      selected=qBound(0,selected+dx+dy*3,qMax(0,posts.size()-1));
      if(selected<first)first=(selected/3)*3;
      if(selected>=first+6)first=qMax(0,(selected/3-1)*3);
      requestThumbs();maybeLoadMore();
    }else if(page==POST && dx){
      const int next=qBound(0,selected+dx,posts.size()-1);
      if(next!=selected){
        selected=next;stop(imageJob);status.clear();requestThumbs();
        
        maybeLoadMore();
      }
    }
    update();
  }
protected:
  void draw(QPainter &p,const QRect &r) override {
    // Screen paints ReRead's palette and standard controls. No app-created header.
    const QRect area=r.adjusted(4,3,-4,-3);
    if(page==LOOKUP) {
      if(lookupMode==0){
        const QRect left(area.x()+4,area.y()+4,area.width()*45/100,area.height()-8);
        const QRect right(left.right()+20,area.y()+4,area.right()-left.right()-24,area.height()-8);
        label(p,QRect(left.x(),left.y(),left.width(),38),"Add tag",28,true,palette().color(QPalette::Link));
        label(p,QRect(left.x(),left.y()+43,left.width(),43),
              prefix.isEmpty()?"Tag prefix: (empty)":prefix,23,true);
        const int gap=5,cols=6;
        const int width=(left.width()-(cols-1)*gap)/cols,height=51;
        for(int i=0;i<keyboardLetters.size()+2;++i){
          const QRect cell(left.x()+(i%cols)*(width+gap),
                           left.y()+99+(i/cols)*(height+gap),width,height);
          const bool chosen=!lookupResultsFocus&&keyIndex==i;
          if(chosen)p.fillRect(cell,palette().highlight());
          else {p.setPen(QPen(palette().mid(),1));p.drawRect(cell.adjusted(1,1,-1,-1));}
          label(p,cell.adjusted(3,0,-3,0),lookupKey(i),21,chosen,
                palette().color(chosen?QPalette::HighlightedText:QPalette::Text));
        }
        label(p,QRect(right.x(),right.y(),right.width(),40),"Matching tags",26,true,
              palette().color(QPalette::Link));
        const int firstMatch=(suggestionIndex/8)*8;
        for(int j=0;j<8 && firstMatch+j<suggestions.size();++j){
          const int i=firstMatch+j;
          const QRect line(right.x(),right.y()+52+j*49,right.width(),44);
          const bool chosen=lookupResultsFocus&&i==suggestionIndex;
          if(chosen)p.fillRect(line,palette().highlight());
          p.fillRect(QRect(line.left()+2,line.top()+11,5,line.height()-22),
                     categoryTextColor(suggestionCategory(i)));
          label(p,line.adjusted(13,0,-8,0),suggestions[i],21,chosen,
                palette().color(chosen?QPalette::HighlightedText:QPalette::Text));
        }
        label(p,QRect(right.x(),right.bottom()-45,right.width(),38),
              "OK on a match adds it to the search",18);
      }else{
        // Post tags use the site's real section order. Headers are not
        // selectable; the existing suggestion index always identifies a tag.
        QStringList rows;
        QList<int> rowTags;
        QList<int> rowCategories;
        int previous=-1;
        int selectedRow=0;
        for(int i=0;i<suggestions.size();++i){
          const int category=suggestionCategory(i);
          if(lookupMode==2&&category!=previous){
            rows<<categoryName(category);
            rowTags<<-1;
            rowCategories<<category;
            previous=category;
          }
          if(i==suggestionIndex)selectedRow=rows.size();
          rows<<suggestions[i];
          rowTags<<i;
          rowCategories<<category;
        }
        const auto layout=listInfo(p,area,rows.size(),selectedRow,55,0.57);
        for(int i=layout.first;i<qMin(rows.size(),layout.first+layout.visible);++i){
          const int category=rowCategories[i];
          const bool header=rowTags[i]<0;
          const bool chosen=i==selectedRow&&!header;
          const QRect cell=listInfoRow(p,layout,i,selectedRow);
          const QColor color=categoryTextColor(category);
          if(header){
            p.fillRect(QRect(cell.left()+5,cell.center().y()-10,5,20),categoryTextColor(category));
            label(p,cell.adjusted(18,0,-9,0),rows[i],21,true,color);
          }else{
            // Preserve e621 category color regardless of selection highlight.
            p.fillRect(QRect(cell.left()+5,cell.top()+10,5,cell.height()-20),categoryColor(category));
            label(p,cell.adjusted(18,0,-9,0),rows[i],22,chosen,
                  palette().color(chosen?QPalette::HighlightedText:QPalette::Text));
          }
        }
        const QRect side=layout.info.adjusted(12,12,-10,-10);
        const QString name=suggestions.value(suggestionIndex);
        const int category=suggestionCategory(suggestionIndex);
        label(p,QRect(side.x(),side.y(),side.width(),40),
              name.isEmpty()?"Tags":"Add tag",27,true,palette().color(QPalette::Link));
        if(!name.isEmpty()){
          label(p,QRect(side.x(),side.y()+57,side.width(),42),name,24,true);
          label(p,QRect(side.x(),side.y()+111,side.width(),34),
                categoryName(category),22,true,categoryTextColor(category));
          text(p,QRect(side.x(),side.y()+170,side.width(),135),
               queryTerms.contains(name)?"Already included in search":
               queryTerms.contains("-"+name)?"Currently excluded from search":
               "OK adds this tag to your active search.",21);
        }else text(p,QRect(side.x(),side.y()+65,side.width(),100),
                     "No tags available",22);
      }
    } else if(page==SEARCH) {
      label(p,QRect(area.x()+5,area.y()+2,area.width()-10,44),
            builtSearch().isEmpty()?"Search: (all posts)":"Search: "+builtSearch(),
            25,true,palette().color(QPalette::Link));
      p.setPen(QPen(palette().mid(),1));
      p.drawLine(area.left()+5,area.y()+49,area.right()-5,area.y()+49);
      const QStringList names=editorRows();
      const auto layout=listInfo(p,area.adjusted(0,61,0,0),names.size(),searchIndex,58,0.57);
      for(int i=layout.first;i<qMin(names.size(),layout.first+layout.visible);++i){
        const QRect row=listInfoRow(p,layout,i,searchIndex);
        const bool chosen=i==searchIndex;
        label(p,row.adjusted(12,0,-10,0),names[i],22,chosen,
              palette().color(chosen?QPalette::HighlightedText:QPalette::Text));
      }
      const QRect side=layout.info.adjusted(10,10,-8,-10);
      label(p,QRect(side.x(),side.y(),side.width(),42),"Search editor",28,true,
            palette().color(QPalette::Link));
      label(p,QRect(side.x(),side.y()+50,side.width(),33),
            QString::number(queryTerms.size())+" terms",20);
      text(p,QRect(side.x(),side.y()+91,side.width(),104),
           selectedTermDetail(),21);
      p.setPen(QPen(palette().mid(),1));
      p.drawLine(side.left(),side.y()+205,side.right(),side.y()+205);
      label(p,QRect(side.x(),side.y()+214,side.width(),32),"Active query",21,true);
      text(p,QRect(side.x(),side.y()+252,side.width(),qMax(45,side.height()-267)),
           builtSearch().isEmpty()?"(all posts)":builtSearch(),21);
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
           homeIndex==0?"Continue editing the current search.":
           homeIndex==1?"Start a new search with recent posts.":
           homeIndex==2?"Start a search for your account favorites.":
           hosts.size()>1&&homeIndex==sections().size()-1?"Switch between configured servers.":
           "Open and edit a saved search.",23);
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
    } else if(page==POST||page==DOWNLOADING) {
      Post *post=current();
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
    // Options has exactly one meaning everywhere: open the search editor.
    if(key==READER_OPTIONS&&state==1){openSearchEditor();return;}
    if(page==DOWNLOADING)return;
    if(key==READER_UP){navigate(0,-1);return;}
    if(key==READER_DOWN){navigate(0,1);return;}
    // Editing a term is a one-shot action, not a repeating scroll operation.
    if(page==SEARCH&&state==2&&(key==READER_LEFT||key==READER_RIGHT))
      return;
    if(key==READER_LEFT){navigate(-1,0);return;}
    if(key==READER_RIGHT){navigate(1,0);return;}
    if(state!=1)return;
    // The same primary actions work in grid and preview; no screen-specific
    // reassignment of OK, Shoot or Play.
    if((page==GALLERY||page==POST)&&key==READER_SHOOT){download();return;}
    if((page==GALLERY||page==POST)&&key==READER_PLAY){favorite();return;}
    if(key==READER_VIEW_TOGGLE&&(page==POST||page==GALLERY)){loadFull();return;}
    if(key==READER_VIEW_TOGGLE&&page==LOOKUP&&lookupMode==0){
      lookupResultsFocus=!lookupResultsFocus;chrome();return;
    }
    if(key==READER_ZOOM_OUT&&page==LOOKUP&&lookupMode==0){
      changePrefix("DEL");return;
    }
    if(key!=READER_OK)return;
    if(page==SEARCH){
      if(searchIndex<queryTerms.size()){removeTerm(searchIndex);return;}
      const int action=searchIndex-queryTerms.size();
      if(action==0)openTagFinder();
      else if(action==1)openTagBrowser(0);
      else if(action==2){
        if(postTagSnapshot.isEmpty())toast("Open a post first to choose its tags");
        else openTagBrowser(1);
      }else if(action==3){
        if(host().username.isEmpty())toast("Configure a username first");
        else {
          const int actionIndex=searchIndex-queryTerms.size();
          const QString value="fav:"+host().username;
          const bool already=queryTerms.contains(value);
          for(int i=queryTerms.size()-1;i>=0;--i)
            if(queryTerms[i].startsWith("fav:"))queryTerms.removeAt(i);
          if(!already)queryTerms<<value;
          searchIndex=qBound(0,queryTerms.size()+actionIndex,editorRows().size()-1);
          activeSearchChanged();
        }
      }else if(action==4)setTerm("order",{"","order:score","order:favcount","order:hot","order:id","order:random","order:updated"});
      else if(action==5)setTerm("rating",{"","rating:s","rating:q","rating:e"});
      else if(action==6)setTerm("type",{"","type:jpg","type:png","type:gif","type:webp","type:mp4","type:webm"});
      else if(action==7)setTerm("score",{"","score:>=10","score:>=50","score:>=100"});
      else if(action==8)toggleBookmark();
      else applySearch();
    }else if(page==LOOKUP){
      if(lookupMode!=0||lookupResultsFocus){
        if(suggestionIndex<suggestions.size()){
          addTag(suggestions[suggestionIndex]);
          toast("Added "+suggestions[suggestionIndex]);
        }
      }else changePrefix(lookupKey(keyIndex));
    }else if(page==HOME){
      openHomeSelection();
    }else if(page==GALLERY&&!posts.isEmpty()){
      stop(imageJob);
      page=POST;chrome();requestThumbs();maybeLoadMore();
    }else if(page==POST){
      loadFull();
    }
  }

public:
  Booru():Screen("booru"){
    setWindowTitle("Booru");
    thumbTimer.setSingleShot(true);
    connect(&thumbTimer,&QTimer::timeout,this,[this]{requestThumbs();});
    searchTimer.setSingleShot(true);
    connect(&searchTimer,&QTimer::timeout,this,[this]{
      if(!api && page!=HOME)fetchPage();
    });
    lookupTimer.setSingleShot(true);
    connect(&lookupTimer,&QTimer::timeout,this,[this]{if(page==LOOKUP)fetchSuggestions();});
    const auto prefs=readJson(preferencesPath());
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
    thumbTimer.stop();lookupTimer.stop();searchTimer.stop();stop(lookupApi);stop(tagApi);stop(api);stop(imageJob);stop(transfer);stop(favoriteJob);
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

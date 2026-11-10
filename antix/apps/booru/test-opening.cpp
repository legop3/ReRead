#define main booru_main
#include "main.cpp"
#undef main
#include <stdexcept>
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
namespace {
class BooruTest {
public:
 static int demo(QApplication &app,const QString &image) {
  Booru window;Post post;post.id=42;post.sample="https://example.invalid/demo.svg";post.tags="Local integration fixture. No network or account requests.";
  window.posts.append(post);window.selected=0;window.page=Booru::POST;
  QFile source(image);if(!source.open(QIODevice::ReadOnly))return 1;
  if(!window.cacheImage(window.cachePath(QUrl(post.sample),post.id),source.readAll()))return 1;
  QImageReader thumb(image);thumb.setScaledSize(QSize(320,200));window.thumbnails.insert(42,thumb.read());
  window.chrome();window.show();QTimer::singleShot(300,&window,[&]{window.loadFull();});return app.exec();
 }
 static void run(const QString &record) {
  QString cachedPath;
  {
    Booru window;
    window.hosts[0].username="fixture";
    Post post;
    post.id=42;
    post.sample="https://example.invalid/image.svg";
    post.tags="cat dog";
    window.initializeSearch("fav:fixture rating:s cat -dog order:score");
    require(window.queryTerms.size()==5 &&
            window.builtSearch()=="fav:fixture rating:s cat -dog order:score",
            "The query has one ordered list containing tags and metatags");

    window.page=Booru::SEARCH;window.chrome();
    window.searchIndex=0;
    window.button(READER_OK,1);
    require(!window.search.contains("fav:fixture")&&window.queryTerms.size()==4,
            "Favorites is an ordinary term removable with OK");
    window.searchTimer.stop();
    window.searchIndex=1; // cat after removing favorites
    window.button(READER_RIGHT,1);
    require(window.queryTerms[1]=="-cat" && window.search.contains("-cat"),
            "Right excludes a selected tag in the single search");
    window.searchTimer.stop();
    window.button(READER_LEFT,1);
    require(window.queryTerms[1]=="cat","Left includes the same tag");
    window.searchTimer.stop();

    window.searchIndex=0;window.button(READER_RIGHT,1);
    require(window.queryTerms[0]=="rating:q","Rating term can be edited in place");
    window.searchTimer.stop();
    window.button(READER_OK,1);
    require(!window.search.contains("rating:"),"A rating term is directly removable");
    window.searchTimer.stop();

    window.initializeSearch("");
    window.page=Booru::SEARCH;window.searchIndex=0;
    window.button(READER_OK,1); // Add tag by name
    require(window.page==Booru::LOOKUP&&window.lookupMode==0,"Search editor opens tag picker");
    window.changePrefix("c");window.changePrefix("a");
    require(window.prefix=="ca","D-pad picker composes tag prefix");
    window.changePrefix("DEL");
    require(window.prefix=="c","D-pad picker deletes");
    window.lookupTimer.stop();
    window.suggestions={"cat"};window.lookupResultsFocus=true;
    window.button(READER_OK,1);
    require(window.queryTerms==QStringList{"cat"}&&window.search=="cat",
            "Selecting tag immediately edits ONE active search");
    window.searchTimer.stop();
    window.button(READER_BACK,1);
    require(window.page==Booru::SEARCH&&window.editorRows().first().contains("cat"),
            "Tag picker returns to the same query editor");
    window.searchIndex=0;window.button(READER_OK,1);
    require(window.queryTerms.isEmpty()&&window.search.isEmpty(),
            "Selected tag is removed from the same search");
    window.searchTimer.stop();

    window.initializeSearch("dog");
    window.toggleBookmark();
    require(window.bookmarks().contains("dog"),"Save the single search");
    require(readJson(window.preferencesPath()).value("bookmarks").toObject()
            .value("https://e926.net").toArray()
            .contains(QJsonValue(QStringLiteral("dog"))),"Bookmark persisted");
    window.toggleBookmark();
    require(!window.bookmarks().contains("dog"),"Remove saved search");

    window.posts.clear();window.posts.append(post);window.selected=0;
    window.page=Booru::POST;
    window.button(READER_PLAY,1);
    require(window.page==Booru::SEARCH&&window.postTagSnapshot.contains("cat"),
            "Post tags are accessible from the SAME editor");
    window.searchIndex=window.queryTerms.size()+2;
    window.button(READER_OK,1);
    require(window.page==Booru::LOOKUP && window.lookupMode==2 &&
            window.suggestions.contains("cat"),"Post tag chooser only adds terms");
    window.suggestionIndex=0;window.button(READER_OK,1);
    require(window.search.contains("cat") || window.search.contains("dog"),
            "Post tag selection changes the active query");
    window.searchTimer.stop();window.button(READER_BACK,1);
    require(window.page==Booru::SEARCH,"Post tag chooser returns to editor");

    window.posts.clear();
    for(int i=0;i<18;++i){Post item;item.id=100+i;window.posts.append(item);}
    window.page=Booru::POST;window.selected=13;window.first=0;
    const auto near=window.thumbnailTargets();
    require(!near.isEmpty()&&near.first()==13&&near.contains(14)&&!near.contains(0),
            "Post preview thumbnail queue follows current selection");
    window.page=Booru::GALLERY;window.first=9;
    const auto gallery=window.thumbnailTargets();
    require(gallery.first()==9&&gallery.last()==14,
            "Grid thumbnails follow the visible six-card window");

    window.posts.clear();window.posts.append(post);window.selected=0;window.first=0;
    cachedPath=window.cachePath(QUrl(post.sample),post.id);
    require(window.cacheImage(cachedPath,
       "<svg xmlns=\\"http://www.w3.org/2000/svg\\" width=\\"20\\" height=\\"10\\"><rect width=\\"20\\" height=\\"10\\" fill=\\"red\\"/></svg>"),
       "Store sample image");
    QFile old(window.cacheRoot()+"/old-image");
    require(old.open(QIODevice::WriteOnly),"Create disposable cache pressure");
    require(old.resize(65LL*1024*1024),"Create sparse cache pressure");
    old.setFileTime(QDateTime::fromMSecsSinceEpoch(1000),
                    QFileDevice::FileModificationTime);old.close();
    window.trimCache(cachedPath);
    require(!QFile::exists(old.fileName())&&QFile::exists(cachedPath),
            "LRU removes older image and preserves current file");

    QImage placeholder(20,10,QImage::Format_ARGB32);
    placeholder.fill(Qt::white);
    window.thumbnails.insert(post.id,placeholder);
    window.page=Booru::GALLERY;window.button(READER_OK,1);
    require(window.page==Booru::POST&&!window.imageJob,
            "Gallery opens post card rather than launching viewer");
    window.button(READER_VIEW_TOGGLE,2);
    require(!window.imageJob&&!QFile::exists(record),"Repeated View does not launch");
    window.button(READER_VIEW_TOGGLE,1);
    QElapsedTimer timer;timer.start();
    while(QFileInfo(record).size()==0 && timer.elapsed()<5000){
      QApplication::processEvents();QThread::msleep(10);
    }
    QFile output(record);
    require(output.open(QIODevice::ReadOnly),"Default MIME viewer launched");
    const auto value=output.readAll();
    const auto url=QUrl::fromEncoded(value);
    require((url.isLocalFile()?url.toLocalFile():QString::fromUtf8(value))==cachedPath,
            "Viewer opened the cached image");
    require(window.imageJob==nullptr,"Cache hit does not download again");
    require(!window.cacheImage(cachedPath,"not an image"),
            "Reject invalid image instead of overwriting cached sample");
  }
  require(QFile::exists(cachedPath),"App exit retains cache for external viewer");
 }

};
} // namespace
int main(int argc,char **argv) {
 if(argc>1 && QByteArray(argv[1])=="--record-open"){
  QCoreApplication app(argc,argv);QFile file(qEnvironmentVariable("TEST_OPEN_RECORD"));
  return file.open(QIODevice::WriteOnly)&&file.write(app.arguments().last().toUtf8())>0?0:1;
 }
 if(argc>2 && QByteArray(argv[1])=="--device-demo"){
  QApplication app(argc,argv);return BooruTest::demo(app,app.arguments()[2]);
 }
 QTemporaryDir root;
 qputenv("REREAD_FILES_DIR",(root.path()+"/files").toUtf8());
 qputenv("XDG_DATA_HOME",(root.path()+"/data").toUtf8());qputenv("XDG_CONFIG_HOME",(root.path()+"/config").toUtf8());
 qputenv("XDG_CACHE_HOME",(root.path()+"/cache").toUtf8());qputenv("TEST_OPEN_RECORD",(root.path()+"/opened").toUtf8());
 QApplication app(argc,argv);
 QDir().mkpath(root.path()+"/data/applications");QDir().mkpath(root.path()+"/config");
 QFile desktop(root.path()+"/data/applications/test-image.desktop");require(desktop.open(QIODevice::WriteOnly),"Create fixture handler");
 desktop.write(("[Desktop Entry]\nType=Application\nName=Fixture\nExec="+app.applicationFilePath()+" --record-open %U\nMimeType=image/svg+xml;\n").toUtf8());desktop.close();
 QFile defaults(root.path()+"/config/mimeapps.list");require(defaults.open(QIODevice::WriteOnly),"Create defaults");defaults.write("[Default Applications]\nimage/svg+xml=test-image.desktop;\n");defaults.close();
 require(QProcess::execute("update-desktop-database",{root.path()+"/data/applications"})==0,"Update MIME cache");
 BooruTest::run(root.path()+"/opened");
}

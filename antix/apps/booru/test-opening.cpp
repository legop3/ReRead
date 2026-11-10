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
  QString file;
  {
   Booru window;Post post;post.id=42;post.sample="https://example.invalid/image.svg";post.tags="cat dog";window.posts.append(post);window.selected=0;
   window.initializeSearch("cat rating:s order:score");
   require(window.sortIndex==2 && window.ratingIndex==1,"Saved search metatags are editable");
   require(window.builtSearch().contains("cat") && window.builtSearch().contains("order:score"),"Preserve saved search");
   window.sortIndex=3;window.ratingIndex=2;window.tagFilters.insert("dog",1);
   require(window.builtSearch().contains("order:favcount") &&
           window.builtSearch().contains("rating:q") &&
           window.builtSearch().contains("dog"),"Compose combined tag and sorting filters");
   window.tagFilters.insert("cat",-1);
   require(window.builtSearch().contains("-cat") && !window.builtSearch().contains(" cat "),
           "Negative tags replace positive base tags");
   window.initializeSearch("dog");
   window.toggleBookmark();
   require(window.bookmarks().contains("dog"),"Bookmark the composed search");
   require(readJson(window.preferencesPath()).value("bookmarks").toObject()
             .value("https://e926.net").toArray().contains(QJsonValue(QStringLiteral("dog"))),"Persist search bookmarks");
   window.toggleBookmark();
   require(!window.bookmarks().contains("dog"),"Remove a bookmark without text input");
   window.initializeSearch("");
   window.page=Booru::HOME;window.homeIndex=0;
   window.button(READER_OK,1);
   require(window.page==Booru::SEARCH && window.searchIndex==0,
           "Explore starts a search workspace from Home");
   window.searchIndex=0;window.button(READER_OK,1);
   require(window.page==Booru::LOOKUP && window.prefix.isEmpty(),
           "Explore has a keyboardless tag finder");
   window.changePrefix("c");window.changePrefix("a");window.changePrefix("t");
   require(window.prefix=="cat","D-pad picker composes tag prefixes");
   window.changePrefix("DEL");
   require(window.prefix=="ca","D-pad picker deletes a letter");
   window.lookupTimer.stop();window.button(READER_BACK,1);
   require(window.page==Booru::SEARCH,"Back from tag finder returns to Explore");
   window.posts.append(post);window.selected=0; // Simulate a post selected after browsing.
   window.searchIndex=6;window.button(READER_OK,1);
   require(window.ratingIndex==1 && window.builtSearch().contains("rating:s"),
           "Rating changes the composed search immediately");
   window.searchIndex=3;window.button(READER_OK,1);
   require(window.page==Booru::INFO && window.tagSource==1 &&
           window.currentTags().contains("cat"),"Related post tags open from Explore");
   window.button(READER_OK,1);
   require(window.builtSearch().contains("cat"),"Tag change updates pending search");
   window.button(READER_BACK,1);
   require(window.page==Booru::SEARCH && window.builtSearch().contains("cat"),
           "Returning to Explore keeps tag selections");
   window.searchIndex=4;window.button(READER_OK,1);
   require(window.page==Booru::INFO && window.tagSource==2 &&
           window.currentTags().contains("cat"),"Active tags can be edited");
   window.button(READER_OK,1);
   require(window.tagState("cat")=="Exclude","Active tag can be excluded");
   window.button(READER_OK,1);
   require(window.tagState("cat")=="Off","Active tag can be removed");
   window.button(READER_BACK,1);
   window.page=Booru::POST;window.initializeSearch("");
   window.button(READER_PLAY,1);
   require(window.page==Booru::INFO && window.currentTags().contains("cat"),"Post offers tag discovery");
   window.button(READER_OK,1);
   require(window.tagFilters.value("cat")==1,"OK includes the selected tag");
   window.button(READER_OK,1);
   require(window.tagFilters.value("cat")==-1,"OK excludes the selected tag");
   window.button(READER_OK,1);
   require(!window.tagFilters.contains("cat"),"OK clears the tag");
   window.button(READER_BACK,1);
   require(window.page==Booru::POST,"Tag picker returns to post");

   file=window.cachePath(QUrl(post.sample),post.id);
   require(window.cacheImage(file,"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\"><rect width=\"20\" height=\"10\" fill=\"red\"/></svg>"),"Store image sample");
   QFile old(window.cacheRoot()+"/old-image");require(old.open(QIODevice::WriteOnly),"Create old cache item");
   require(old.resize(65LL*1024*1024),"Create sparse cache pressure");
   old.setFileTime(QDateTime::fromMSecsSinceEpoch(1000),QFileDevice::FileModificationTime);old.close();
   window.trimCache(file);require(!QFile::exists(old.fileName()) && QFile::exists(file),"Evict oldest item while preserving current image");
   window.page=Booru::GALLERY;
   window.button(READER_OK,1);
   require(window.page==Booru::POST && !window.imageJob && !QFile::exists(record),"Opening a post stays on its card by default");
   window.button(READER_VIEW_TOGGLE,2);
   require(!window.imageJob && !QFile::exists(record),"Repeated View must not open a viewer");
   window.button(READER_VIEW_TOGGLE,1); // Explicit request: cached sample, real GIO dispatch.
   QElapsedTimer timer;timer.start();
   while(QFileInfo(record).size()==0 && timer.elapsed()<5000){QApplication::processEvents();QThread::msleep(10);}
   QFile output(record);require(output.open(QIODevice::ReadOnly),"Default MIME handler launched");auto value=output.readAll();auto url=QUrl::fromEncoded(value);
   require((url.isLocalFile()?url.toLocalFile():QString::fromUtf8(value))==file,"Default handler received cached image");
   require(window.imageJob==nullptr,"Cache hit does not start a download");
   require(!window.cacheImage(file,"not an image"),"Reject non-image response without replacing cached file");
  }
  require(QFile::exists(file),"Booru exit keeps files available to external viewer");
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

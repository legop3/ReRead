#define main archive_main
#include "main.cpp"
#undef main
#include <stdexcept>
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
struct ArchiveTest {
  static QString received(const QString &record) {
    QElapsedTimer timer;timer.start();
    while(QFileInfo(record).size()==0 && timer.elapsed()<5000){QApplication::processEvents();QThread::msleep(10);}
    QFile file(record);require(file.open(QIODevice::ReadOnly),"Default MIME handler launched");
    auto bytes=file.readAll();file.close();QFile::remove(record);
    QUrl uri=QUrl::fromEncoded(bytes);return uri.isLocalFile()?uri.toLocalFile():QString::fromUtf8(bytes);
  }
  static void run(const QString &record) {
    QString cached;
    {
      Archive app;app.itemId="fixture ' # ü %";app.page=Archive::FILES;app.selected=4;
      for(const QString &name:QStringList{"image ' # ü %.png","text ' # ü %.txt"}) {
        File selected{name,"Fixture",100,0};auto url=fileUrl(app.itemId,name);cached=app.cachedFile(url);
        QDir().mkpath(QFileInfo(cached).absolutePath());QFile file(cached);file.open(QIODevice::WriteOnly);file.write("cached data");file.close();
        app.openFile(selected);
        require(received(record)==cached,"Local cache dispatched without shell filename damage");
        require(app.api==nullptr && app.page==Archive::FILES && app.selected==4,"Cache hit preserves catalog and avoids network");
      }
      File audio{"audio ' # ü %.mp3","MP3",100,0};app.openFile(audio);
      require(received(record)==QString::fromUtf8(fileUrl(app.itemId,audio.name).toEncoded()),"Audio URI uses its MIME handler, not the browser");
      require(app.api==nullptr,"Streaming audio does not download in Archive");
      app.openFile(File{"unsupported.bin","Data",100,0});require(!app.status.isEmpty() && app.api==nullptr,"Unsupported type stays downloadable");
      QFile pressure(QFileInfo(cached).dir().absolutePath()+"/old");pressure.open(QIODevice::WriteOnly);pressure.resize(65LL*1024*1024);
      pressure.setFileTime(QDateTime::fromMSecsSinceEpoch(1000),QFileDevice::FileModificationTime);pressure.close();
      app.trimCache(cached);require(!QFile::exists(pressure.fileName()) && QFile::exists(cached),"Eviction keeps current file");
    }
    require(QFile::exists(cached),"Caller exit does not remove the viewer's file");
  }
};
int main(int argc,char **argv) {
  if(argc>1 && QByteArray(argv[1])=="--record-open") {
    QCoreApplication app(argc,argv);QFile file(qEnvironmentVariable("TEST_OPEN_RECORD"));
    return file.open(QIODevice::WriteOnly)&&file.write(app.arguments().last().toUtf8())>0?0:1;
  }
  QTemporaryDir root;
  qputenv("READER_APP_DATA_DIR",(root.path()+"/state").toUtf8());
  qputenv("XDG_CACHE_HOME",(root.path()+"/cache").toUtf8());
  qputenv("XDG_DATA_HOME",(root.path()+"/data").toUtf8());qputenv("XDG_CONFIG_HOME",(root.path()+"/config").toUtf8());
  qputenv("TEST_OPEN_RECORD",(root.path()+"/opened").toUtf8());
  QApplication app(argc,argv);QDir().mkpath(root.path()+"/data/applications");QDir().mkpath(root.path()+"/config");
  QFile desktop(root.path()+"/data/applications/test-viewer.desktop");desktop.open(QIODevice::WriteOnly);
  desktop.write(("[Desktop Entry]\nType=Application\nName=Fixture\nExec="+app.applicationFilePath()+" --record-open %U\nMimeType=image/png;text/plain;audio/mpeg;\n").toUtf8());desktop.close();
  QFile defaults(root.path()+"/config/mimeapps.list");defaults.open(QIODevice::WriteOnly);
  defaults.write("[Default Applications]\nimage/png=test-viewer.desktop;\ntext/plain=test-viewer.desktop;\naudio/mpeg=test-viewer.desktop;\n");defaults.close();
  require(QProcess::execute("update-desktop-database",{root.path()+"/data/applications"})==0,"Update test MIME cache");
  ArchiveTest::run(root.path()+"/opened");return 0;
}

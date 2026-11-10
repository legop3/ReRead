// Offscreen checks use a disposable shared-files root, never real Reader files.
#define main reread_files_main
#include "main.cpp"
#undef main
#include <stdexcept>
static void require(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
static void press(Files &window,reader_button id) {
  window.dispatch(id,1);window.dispatch(id,0);QCoreApplication::processEvents();
}
static void settle() {
  QElapsedTimer clock;clock.start();
  while(clock.elapsed()<100) {QCoreApplication::processEvents();QThread::msleep(5);}
}
// Exercise the real entry point too: constructing Files directly misses
// entry setup; GUI apps must leave raw evdev ownership to system services.
static void checkEntryInput() {
  if(!qEnvironmentVariableIsSet("REREAD_FILES_ENTRY_TEST"))return;
  QTimer::singleShot(0,[]{
    bool raw=false;
    for(const auto &fd:QDir("/proc/self/fd").entryInfoList(QDir::Files|QDir::System))
      if(fd.symLinkTarget().startsWith("/dev/input/"))raw=true;
    QCoreApplication::exit(raw?1:0);
  });
}
Q_COREAPP_STARTUP_FUNCTION(checkEntryInput)
int main(int argc,char **argv) {
  if(argc>1 && QByteArray(argv[1])=="--entry-test")return reread_files_main(argc,argv);
  if(argc>1 && QByteArray(argv[1])=="--record-open") {
    QCoreApplication app(argc,argv);QFile record(qEnvironmentVariable("REREAD_OPEN_RECORD"));
    return record.open(QIODevice::WriteOnly) && record.write(app.arguments().last().toUtf8())>0?0:1;
  }
  QTemporaryDir fixture, settings;
  qputenv("XDG_DATA_HOME",(settings.path()+"/data").toUtf8());
  qputenv("XDG_CONFIG_HOME",(settings.path()+"/config").toUtf8());
  qputenv("XDG_CURRENT_DESKTOP","ReRead");
  qputenv("REREAD_OPEN_RECORD",(settings.path()+"/opened-uri").toUtf8());
  QApplication app(argc,argv);
  QDir().mkpath(settings.path()+"/data/applications");QDir().mkpath(settings.path()+"/config");
  QFile desktop(settings.path()+"/data/applications/test-images.desktop");
  require(desktop.open(QIODevice::WriteOnly),"Create MIME handler");
  desktop.write(("[Desktop Entry]\nType=Application\nName=Fixture image handler\nExec="+
    QCoreApplication::applicationFilePath()+" --record-open %U\nMimeType=image/png;text/plain;\nNoDisplay=true\n").toUtf8());desktop.close();
  QFile defaults(settings.path()+"/config/mimeapps.list");
  require(defaults.open(QIODevice::WriteOnly),"Create MIME defaults");
  defaults.write("[Default Applications]\nimage/png=test-images.desktop;\ntext/plain=test-images.desktop;\n");defaults.close();
  require(QProcess::execute("update-desktop-database",{settings.path()+"/data/applications"})==0,"Update MIME cache");
  qputenv("REREAD_FILES_DIR",fixture.path().toUtf8());
  QProcess entry;
  auto environment=QProcessEnvironment::systemEnvironment();
  environment.insert("REREAD_FILES_ENTRY_TEST","1");
  entry.setProcessEnvironment(environment);
  entry.start(QCoreApplication::applicationFilePath(),{"--entry-test"});
  require(entry.waitForFinished(5000) && entry.exitStatus()==QProcess::NormalExit && entry.exitCode()==0,
          "Real entry point uses focused toolkit input without raw evdev readers");
  QDir().mkpath(fixture.path()+"/A/Camera");
  QImage image(16,16,QImage::Format_RGB32);image.fill(Qt::green);
  require(image.save(fixture.path()+"/A/Camera/photo ' # ü %.png"),"Create fixture");
  QFile text(fixture.path()+"/z.txt");require(text.open(QIODevice::WriteOnly),"Create text");text.write("Hello Files\n");text.close();
  Files window;window.show();QCoreApplication::processEvents();
  // Shared collection layout: pagination and row bounds with a nonzero origin.
  QImage canvas(960,544,QImage::Format_RGB32);QPainter painter(&canvas);
  auto layout=window.listInfo(painter,QRect(20,30,900,300),200,199,50);
  require(layout.first==198 && layout.visible==6,"Shared layout pages selection into view");
  require(layout.entries.contains(layout.row(199)),"Shared row stays inside list pane");
  require(!layout.entries.intersects(layout.info),"List and info panes do not overlap");
  require(layout.row(197).isEmpty(),"Off-page rows have no rectangle");
  require(window.listInfo(painter,QRect(20,30,900,10),0,0,50).first==0,"Empty short list");
  painter.end();
  press(window,READER_OK);require(window.subtitle.startsWith("A ·"),"Enter folder");
  press(window,READER_OK);require(window.subtitle.startsWith("A/Camera ·"),"Enter nested folder");
  press(window,READER_OK);
  QElapsedTimer opening;opening.start();
  while(QFileInfo(settings.path()+"/opened-uri").size()==0 && opening.elapsed()<5000)settle();
  QFile record(settings.path()+"/opened-uri");require(record.open(QIODevice::ReadOnly),"GIO launched default handler");
  auto received=record.readAll();record.close();auto url=QUrl::fromEncoded(received);
  require((url.isLocalFile()?url.toLocalFile():QString::fromUtf8(received))==fixture.path()+"/A/Camera/photo ' # ü %.png","Handler receives exact filename, as a path or file URI");
  require(window.subtitle.startsWith("A/Camera ·") && window.showHeader,"Files preserves folder when launching another app");
  press(window,READER_OPTIONS);press(window,READER_DOWN);press(window,READER_OK);
  require(window.title=="Delete permanently?","Delete confirmation");
  press(window,READER_OK);require(QFile::exists(fixture.path()+"/A/Camera/photo ' # ü %.png"),"Cancel is default");
  press(window,READER_OPTIONS);press(window,READER_DOWN);press(window,READER_OK);
  press(window,READER_DOWN);press(window,READER_OK);settle();
  require(!QFile::exists(fixture.path()+"/A/Camera/photo ' # ü %.png"),"Confirmed file deletion");
  press(window,READER_BACK);press(window,READER_BACK);
  require(window.subtitle.startsWith("Shared files"),"Back to shared root");
  // Delete the complete nested folder through the same button flow.
  press(window,READER_OPTIONS);press(window,READER_DOWN);press(window,READER_OK);
  press(window,READER_DOWN);press(window,READER_OK);settle();
  require(!QFileInfo::exists(fixture.path()+"/A"),"Confirmed folder deletion");
  QFile::remove(settings.path()+"/opened-uri");press(window,READER_OK);
  opening.restart();while(QFileInfo(settings.path()+"/opened-uri").size()==0 && opening.elapsed()<5000)settle();
  require(record.open(QIODevice::ReadOnly),"Text uses default MIME handler");
  received=record.readAll();record.close();url=QUrl::fromEncoded(received);
  require((url.isLocalFile()?url.toLocalFile():QString::fromUtf8(received))==fixture.path()+"/z.txt","Text handler receives exact file");
  require(window.title=="Files","Text opening keeps Files in its browser");
  press(window,READER_OPTIONS);press(window,READER_OK);
  require(window.title=="Details","Details view");
  press(window,READER_BACK);
  QFile external(fixture.path()+"/new.txt");external.open(QIODevice::WriteOnly);external.write("new");external.close();settle();
  require(window.subtitle.contains("2 items"),"External changes refresh listing");
  return 0;
}

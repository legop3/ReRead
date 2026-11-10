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
// LinuxFB setup, and two active input readers turn one OK into multiple opens.
static void checkEntryInput() {
  if(!qEnvironmentVariableIsSet("REREAD_FILES_ENTRY_TEST"))return;
  QTimer::singleShot(0,[]{
    QCoreApplication::exit(qEnvironmentVariable("QT_QPA_FB_DISABLE_INPUT")=="1"?0:1);
  });
}
Q_COREAPP_STARTUP_FUNCTION(checkEntryInput)
int main(int argc,char **argv) {
  if(argc>1 && QByteArray(argv[1])=="--entry-test")return reread_files_main(argc,argv);
  QApplication app(argc,argv);QTemporaryDir fixture;
  qputenv("REREAD_FILES_DIR",fixture.path().toUtf8());
  QProcess entry;
  auto environment=QProcessEnvironment::systemEnvironment();
  environment.remove("QT_QPA_FB_DISABLE_INPUT");
  environment.insert("REREAD_FILES_ENTRY_TEST","1");
  entry.setProcessEnvironment(environment);
  entry.start(QCoreApplication::applicationFilePath(),{"--entry-test"});
  require(entry.waitForFinished(5000) && entry.exitStatus()==QProcess::NormalExit && entry.exitCode()==0,
          "Real entry point disables Qt's duplicate LinuxFB input handler and exits cleanly");
  QDir().mkpath(fixture.path()+"/A/Camera");
  QImage image(16,16,QImage::Format_RGB32);image.fill(Qt::green);
  require(image.save(fixture.path()+"/A/Camera/photo.png"),"Create fixture");
  QFile text(fixture.path()+"/z.txt");require(text.open(QIODevice::WriteOnly),"Create text");text.write("Hello Files\n");text.close();
  Files window;window.show();QCoreApplication::processEvents();
  press(window,READER_OK);require(window.subtitle.startsWith("A ·"),"Enter folder");
  press(window,READER_OK);require(window.subtitle.startsWith("A/Camera ·"),"Enter nested folder");
  press(window,READER_OK);require(!window.showHeader,"Image preview");
  press(window,READER_ZOOM_IN);press(window,READER_RIGHT);press(window,READER_ZOOM_OUT);
  press(window,READER_BACK);require(window.showHeader,"Back from preview");
  press(window,READER_OPTIONS);press(window,READER_DOWN);press(window,READER_OK);
  require(window.title=="Delete permanently?","Delete confirmation");
  press(window,READER_OK);require(QFile::exists(fixture.path()+"/A/Camera/photo.png"),"Cancel is default");
  press(window,READER_OPTIONS);press(window,READER_DOWN);press(window,READER_OK);
  press(window,READER_DOWN);press(window,READER_OK);settle();
  require(!QFile::exists(fixture.path()+"/A/Camera/photo.png"),"Confirmed file deletion");
  press(window,READER_BACK);press(window,READER_BACK);
  require(window.subtitle.startsWith("Shared files"),"Back to shared root");
  // Delete the complete nested folder through the same button flow.
  press(window,READER_OPTIONS);press(window,READER_DOWN);press(window,READER_OK);
  press(window,READER_DOWN);press(window,READER_OK);settle();
  require(!QFileInfo::exists(fixture.path()+"/A"),"Confirmed folder deletion");
  press(window,READER_OK);require(window.showHeader,"Text preview");
  press(window,READER_BACK);press(window,READER_OPTIONS);press(window,READER_OK);
  require(window.title=="Details","Details view");
  press(window,READER_BACK);
  QFile external(fixture.path()+"/new.txt");external.open(QIODevice::WriteOnly);external.write("new");external.close();settle();
  require(window.subtitle.contains("2 items"),"External changes refresh listing");
  return 0;
}

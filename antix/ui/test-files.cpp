// Run offscreen in the builder with the launcher's normal Qt/GIO libraries.
#define main reread_launcher_main
#include "launcher.cpp"
#undef main
#include <stdexcept>
static void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
static void press(Launcher &window, reader_button button) {
  window.dispatch(button,1); window.dispatch(button,0);
  QCoreApplication::processEvents();
}
int main(int argc, char **argv) {
  QTemporaryDir files;
  require(files.isValid(), "No test directory");
  qputenv("REREAD_FILES_DIR",files.path().toUtf8());
  // Discover no host desktop apps; Home contains Files and Settings only.
  qputenv("XDG_DATA_DIRS",(files.path()+"/data").toUtf8());
  qputenv("XDG_DATA_HOME",(files.path()+"/data").toUtf8());
  QDir().mkpath(files.path()+"/Pictures/Camera");
  QImage picture(8,8,QImage::Format_RGB32); picture.fill(Qt::blue);
  require(picture.save(files.path()+"/Pictures/Camera/test.png"),"Could not create fixture");
  QApplication app(argc,argv);
  Launcher window; window.show(); QCoreApplication::processEvents();
  press(window,READER_OK);
  require(window.title=="Files" && window.subtitle.isEmpty(),"Open shared root");
  press(window,READER_OK);
  require(window.subtitle=="Pictures","Open Pictures folder");
  press(window,READER_OK);
  require(window.subtitle=="Pictures/Camera","Open nested Camera folder");
  press(window,READER_OK);
  require(window.title=="Photo","Open nested photograph");
  press(window,READER_BACK);
  require(window.title=="Files" && window.subtitle=="Pictures/Camera","Photo returns to containing folder");
  press(window,READER_BACK);
  require(window.subtitle=="Pictures","Back goes up one folder");
  press(window,READER_BACK);
  require(window.title=="Files" && window.subtitle.isEmpty(),"Back reaches shared root");
  press(window,READER_BACK);
  require(window.title=="ReRead","Back from root returns Home");
  return 0;
}

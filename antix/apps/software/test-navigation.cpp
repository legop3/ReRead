// Build with the app's libraries and run against a disposable reader-software CLI.
#define main software_application_main
#include "main.cpp"
#undef main
#include <stdexcept>
static void settled(Software &window) {
  QElapsedTimer elapsed, quiet; elapsed.start(); quiet.start();
  while (elapsed.elapsed()<15000) {
    QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    if (window.findChild<QProcess*>()) quiet.restart();
    else if (quiet.elapsed()>150) return;
    QThread::msleep(10);
  }
  throw std::runtime_error("Software frontend did not finish its request");
}
static void press(Software &window,reader_button button,int count=1) {
  for(int i=0;i<count;i++) { window.dispatch(button,1); window.dispatch(button,0); }
  settled(window);
}
int main(int argc,char **argv) {
  QApplication app(argc,argv);
  Software window; window.resize(960,544); window.show(); settled(window);
  window.grab().save("/work/build/repository/software-home.png");
  press(window,READER_OK); // Check repository.
  press(window,READER_BACK);
  press(window,READER_DOWN,2); press(window,READER_OK); // Apps.
  window.grab().save("/work/build/repository/software-apps.png");
  press(window,READER_DOWN); press(window,READER_OK); press(window,READER_OK); // Install Notes.
  press(window,READER_BACK);
  press(window,READER_DOWN,2); press(window,READER_OK);
  press(window,READER_DOWN); press(window,READER_OK); // Notes details.
  press(window,READER_DOWN); press(window,READER_OK); // Removal confirmation, Cancel selected.
  press(window,READER_UP); press(window,READER_OK); // Confirm removal.
  press(window,READER_BACK);
  press(window,READER_DOWN); press(window,READER_OK); press(window,READER_OK); // Platform update.
  return 0;
}

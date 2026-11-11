#define main reread_images_main
#include "main.cpp"
#undef main
#include <stdexcept>
static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static void press(Images &window,reader_button id){window.dispatch(id,1);window.dispatch(id,0);QApplication::processEvents();}
int main(int argc,char **argv){
  QApplication app(argc,argv);QTemporaryDir directory;
  QString file=directory.path()+"/image ' # ü %.png";
  QImage source(2400,1200,QImage::Format_RGB32);QPainter paint(&source);
  paint.fillRect(0,0,1200,600,Qt::red);paint.fillRect(1200,0,1200,600,Qt::green);
  paint.fillRect(0,600,1200,600,Qt::blue);paint.fillRect(1200,600,1200,600,Qt::yellow);paint.end();
  require(source.save(file),"Save fixture");QFile bytes(file);require(bytes.open(QIODevice::ReadOnly),"Read fixture");auto original=bytes.readAll();bytes.close();
  Images window({QString::fromUtf8(QUrl::fromLocalFile(file).toEncoded())});window.show();app.processEvents();
  auto center=window.content().center();
  require(window.grab().toImage().pixelColor(center-QPoint(40,40))==QColor(Qt::red),"Decode encoded file URI and fit image");
  press(window,READER_ZOOM_IN);for(int i=0;i<100;++i)press(window,READER_LEFT);
  require(window.grab().toImage().pixelColor(center).alpha()==255,"Panning remains renderable at bounds");
  press(window,READER_OK);press(window,READER_VIEW_TOGGLE);
  require(window.grab().toImage().pixelColor(center-QPoint(40,40))==QColor(Qt::blue),"Rotate view clockwise");
  require(bytes.open(QIODevice::ReadOnly) && bytes.readAll()==original,"Rotation leaves original file unchanged");bytes.close();
  press(window,READER_OPTIONS);require(window.showHeader,"Information screen");press(window,READER_BACK);require(!window.showHeader,"Return to image");
  press(window,READER_BACK);require(!window.isVisible(),"Back closes viewer");

  // Exercise the decoder and Qt timer together, including normal app lifecycle.
  QByteArray gif=QByteArray::fromBase64("R0lGODlhUAAoAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQADAAAACwAAAAAUAAoAAAIXQABCBxIsKDBgwgTKlzIsKHDhxAjSpxIsaLFixgzatzIsaPHjyBDihxJsqTJkyhTqlzJsqXLlzBjypxJs6bNmzhz6tzJs6fPn0CDCh1KtKjRo0iTKl3KtKnTpygDAgAh+QQBDAABACwAAAAAUAAoAIEAAP8AAAAAAAAAAAAIXQABCBxIsKDBgwgTKlzIsKHDhxAjSpxIsaLFixgzatzIsaPHjyBDihxJsqTJkyhTqlzJsqXLlzBjypxJs6bNmzhz6tzJs6fPn0CDCh1KtKjRo0iTKl3KtKnTpygDAgA7");
  QString animation=directory.path()+"/animation.gif";
  QFile fixture(animation);require(fixture.open(QIODevice::WriteOnly),"Create GIF");fixture.write(gif);fixture.close();
  auto wait=[](int ms){QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){QApplication::processEvents();QThread::msleep(5);}};
  Images animated({animation});animated.show();wait(30);
  auto pixel=[&]{return animated.grab().toImage().pixelColor(animated.content().center());};
  auto changes=[&](int ms){auto first=pixel();QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){wait(10);if(pixel()!=first)return true;}return false;};
  require(changes(600),"GIF advances frames");
  press(animated,READER_PLAY);require(!changes(350),"Play pauses animation");
  animated.hide();wait(30);animated.show();wait(30);
  require(!changes(350),"User pause survives background and foreground");
  press(animated,READER_PLAY);require(changes(600),"Play resumes animation");
  press(animated,READER_VIEW_TOGGLE);wait(300);
  auto capture=animated.grab().toImage();auto midpoint=animated.content().center();
  require(capture.pixelColor(midpoint+QPoint(0,30))!=capture.pixelColor(midpoint+QPoint(30,0)),"Rotation persists across animation frames");
  animated.hide();wait(30);animated.show();wait(30);
  require(changes(600),"Animation resumes after foreground returns");
  press(animated,READER_OPTIONS);wait(300);press(animated,READER_BACK);
  require(changes(600),"Animation resumes after information closes");
  press(animated,READER_RIGHT);wait(300);
  require(!changes(350),"Browsing to a static image stops animation");
  press(animated,READER_BACK);
  QFile finiteFile(directory.path()+"/cached-animation");
  require(finiteFile.open(QIODevice::WriteOnly),"Create extensionless finite GIF");
  finiteFile.write(gif.replace(QByteArray::fromHex("0301000000"),QByteArray::fromHex("0301010000")));finiteFile.close();
  Images finite({finiteFile.fileName()});finite.show();wait(1000);
  require(finite.controls.first().action=="Replay","Finite GIF respects its loop count without needing a filename extension");
  press(finite,READER_PLAY);require(finite.controls.first().action=="Pause","Finished GIF can replay");
  press(finite,READER_BACK);
  QFile corrupt(directory.path()+"/broken.png");require(corrupt.open(QIODevice::WriteOnly),"Create corrupt image");corrupt.write("not an image");corrupt.close();
  Images broken({corrupt.fileName()});broken.show();app.processEvents();press(broken,READER_BACK);require(!broken.isVisible(),"Corrupt file remains closable");
  return 0;
}

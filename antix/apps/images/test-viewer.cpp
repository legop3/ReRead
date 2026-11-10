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
  QFile corrupt(directory.path()+"/broken.png");require(corrupt.open(QIODevice::WriteOnly),"Create corrupt image");corrupt.write("not an image");corrupt.close();
  Images broken({corrupt.fileName()});broken.show();app.processEvents();press(broken,READER_BACK);require(!broken.isVisible(),"Corrupt file remains closable");
  return 0;
}

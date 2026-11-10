#include "reader-ui.hpp"
#include <QCommandLineParser>
using namespace ReRead;

class Images : public Screen {
  QStringList files;
  int index=0, turns=0;
  QImage original, image;
  double zoom=1;
  QPointF pan;
  QString error;
  bool info=false;
  double fit() const {
    if(image.isNull())return 1;
    return qMin(1.0,qMin(double(content().width())/image.width(),double(content().height())/image.height()));
  }
  QSizeF displayed() const {return QSizeF(image.size())*(fit()*zoom);}
  void clampPan() {
    auto size=displayed();
    double x=qMax(0.0,(size.width()-content().width())/2),y=qMax(0.0,(size.height()-content().height())/2);
    pan.setX(qBound(-x,pan.x(),x));pan.setY(qBound(-y,pan.y(),y));
  }
  void chrome() {
    showHeader=info;controlsOverlay=false;
    title="Image information";subtitle=QFileInfo(files.value(index)).fileName();
    controls=info?QList<Control>{{READER_BACK,"Image"}}:
      QList<Control>{{READER_ZOOM_IN,"Zoom"},{READER_OK,"Fit / 1:1"},{READER_VIEW_TOGGLE,"Rotate"},{READER_BACK,"Close"}};
    update();
  }
  void load() {
    original={};image={};error.clear();zoom=1;pan={};turns=0;
    setWindowTitle("Images — "+QFileInfo(files.value(index)).fileName());
    if(files.isEmpty())error="Open an image from Files or another application.";
    else {
      QImageReader reader(files[index]);reader.setAutoTransform(true);
      auto size=reader.size();
      // Bound work on this 512 MiB machine; JPEG scaling is performed by Qt's decoder.
      if(qint64(size.width())*size.height()>64000000)
        error="This image is too large for the Reader.";
      else {
        if(size.width()>4096 || size.height()>4096)reader.setScaledSize(size.scaled(4096,4096,Qt::KeepAspectRatio));
        original=reader.read();image=original;
        if(image.isNull())error="Cannot open image: "+reader.errorString();
      }
    }
    chrome();
  }
  void rotate() {
    turns=(turns+1)%4;
    image=original.transformed(QTransform().rotate(turns*90));
    zoom=1;pan={};update();
  }
  void button(reader_button id,int value) override {
    if(!value)return;
    if(value==2 && id!=READER_UP && id!=READER_DOWN && id!=READER_LEFT && id!=READER_RIGHT)return;
    if(id==READER_FAVORITES) {close();return;}
    if(id==READER_BACK) {if(info){info=false;chrome();}else close();return;}
    if(id==READER_OPTIONS) {info=!info;chrome();return;}
    if(id==READER_GUIDE) {
      toast("Zoom buttons magnify · Arrows pan; Left/Right browse at fit · OK fits / actual size · View rotates · Options shows info");return;
    }
    if(info || image.isNull())return;
    if(id==READER_ZOOM_IN || id==READER_ZOOM_OUT) {
      zoom=qBound(1.0,zoom*(id==READER_ZOOM_IN?1.414213562:0.707106781),qMax(16.0,1.0/fit()));
    } else if(id==READER_OK) {zoom=qAbs(zoom-1)<0.001?1.0/fit():1.0;pan={};}
    else if(id==READER_VIEW_TOGGLE) {rotate();return;}
    else if(id==READER_LEFT || id==READER_RIGHT || id==READER_UP || id==READER_DOWN) {
      if(qAbs(zoom-1)<0.001 && (id==READER_LEFT || id==READER_RIGHT)) {
        int next=index+(id==READER_RIGHT?1:-1);
        if(next>=0 && next<files.size()){index=next;load();}return;
      }
      pan+=QPointF(id==READER_LEFT?80:id==READER_RIGHT?-80:0,id==READER_UP?60:id==READER_DOWN?-60:0);
    }
    clampPan();update();
  }
  void draw(QPainter &p,const QRect &r) override {
    if(!error.isEmpty()){text(p,r,error,26);return;}
    if(info) {
      text(p,r,files.value(index)+"\n\n"+QString::number(original.width())+" × "+QString::number(original.height())+
        " decoded pixels\n"+QString::number(qRound(zoom*100))+"% of fit · "+QString::number(turns*90)+
        "° rotation\nImage "+QString::number(index+1)+" of "+QString::number(files.size())+
        "\n\nViewing does not change the original file.",23);return;
    }
    clampPan();auto size=displayed();
    QRectF target(QPointF(r.center())+pan-QPointF(size.width()/2,size.height()/2),size);
    p.save();p.setClipRect(r);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(target,image);p.restore();
  }
public:
  explicit Images(const QStringList &arguments):Screen("Images") {
    for(const auto &argument:arguments) {
      QUrl url(argument);
      if(!QFileInfo::exists(argument) && !url.scheme().isEmpty() && !url.isLocalFile()) {
        error="Only local image files are supported. Download the image first.";chrome();return;
      }
      files.append(url.isLocalFile()?url.toLocalFile():QFileInfo(argument).absoluteFilePath());
    }
    if(files.size()==1) {
      // Preserve the familiar previous/next behaviour for a file opened from a folder.
      QFileInfo chosen(files.first());QStringList siblings;
      auto formats=QImageReader::supportedImageFormats();
      for(const auto &entry:chosen.dir().entryInfoList(QDir::Files,QDir::Name))
        if(formats.contains(entry.suffix().toLower().toLatin1()))siblings.append(entry.absoluteFilePath());
      int position=siblings.indexOf(chosen.absoluteFilePath());
      if(position>=0){files=siblings;index=position;}
    }
    load();
  }
};
int main(int argc,char **argv) {
  QApplication app(argc,argv);app.setApplicationName("reread-images");
  app.setDesktopFileName("reread-images");app.setFont(QFont("DejaVu Sans"));
  QCommandLineParser parser;parser.setApplicationDescription("ReRead image viewer");parser.addHelpOption();
  parser.addPositionalArgument("files","Local image paths or file: URIs","[files…]");parser.process(app);
  Images window(parser.positionalArguments());window.show();return app.exec();
}

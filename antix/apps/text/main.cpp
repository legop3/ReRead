#include "reader-ui.hpp"
#include <QTextCodec>
using namespace ReRead;

class Text : public Screen {
  friend struct TextTest;
  QTextDocument document;
  QString filename, error, warning;
  qreal offset=0;
  int fontSize=26;
  bool information=false;
  QRect body() const {return content().adjusted(0,48,-12,-30);}
  qreal maximum() const {return qMax(0.0,document.size().height()-body().height());}
  void chrome() {
    controls=(!error.isEmpty() || information)?QList<Control>{{READER_BACK,information?"Document":"Close"}}:
      QList<Control>{{READER_UP,"Scroll"},{READER_RIGHT,"Page"},{READER_ZOOM_IN,"Text size ±"},{READER_OK,"Start"},{READER_OPTIONS,"Info"},{READER_BACK,"Close"}};
    update();
  }
  void layout() {
    QFont font("DejaVu Sans");font.setPixelSize(fontSize);
    document.setDefaultFont(font);document.setTextWidth(body().width());
    offset=qBound(0.0,offset,maximum());update();
  }
  void resizeEvent(QResizeEvent *event) override {Screen::resizeEvent(event);layout();}
  void button(reader_button id,int value) override {
    if(!value)return;
    if(value==2 && id!=READER_UP && id!=READER_DOWN && id!=READER_LEFT && id!=READER_RIGHT)return;
    if(id==READER_FAVORITES){close();return;}
    if(id==READER_BACK){if(information){information=false;chrome();}else close();return;}
    if(id==READER_OPTIONS){information=!information;chrome();return;}
    if(information || !error.isEmpty())return;
    if(id==READER_ZOOM_IN || id==READER_ZOOM_OUT) {
      const qreal fraction=maximum()>0?offset/maximum():0;
      fontSize=qBound(18,fontSize+(id==READER_ZOOM_IN?2:-2),42);layout();offset=fraction*maximum();
    } else if(id==READER_UP || id==READER_DOWN)
      offset+=(id==READER_DOWN?1:-1)*QFontMetrics(document.defaultFont()).lineSpacing();
    else if(id==READER_LEFT || id==READER_RIGHT)offset+=(id==READER_RIGHT?1:-1)*body().height()*0.9;
    else if(id==READER_OK)offset=0;
    offset=qBound(0.0,offset,maximum());update();
  }
  void draw(QPainter &p,const QRect &r) override {
    label(p,QRect(r.x(),r.y(),r.width(),36),QFileInfo(filename).fileName(),25,true,palette().color(QPalette::Link));
    if(!error.isEmpty()){text(p,body(),error,26);return;}
    if(information){text(p,body(),filename+"\n\nRead-only plain text · "+QString::number(fontSize)+" px\n"+
      "Arrows scroll; Left / Right turn pages. Zoom changes text size. OK returns to the start.\n\n"+
      (warning.isEmpty()?"The original file is never changed.":warning),24);return;}
    const QRect viewport=body();
    p.save();p.setClipRect(viewport);p.translate(viewport.x(),viewport.y()-offset);
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette=palette();context.clip=QRectF(0,offset,viewport.width(),viewport.height());
    document.documentLayout()->draw(&p,context);p.restore();
    const int progress=maximum()>0?qRound(offset*100/maximum()):100;
    label(p,QRect(r.x(),r.bottom()-26,r.width(),26),
      (warning.isEmpty()?QString():warning+" · ")+QString::number(progress)+"% · Hold ? for controls",18);
    if(maximum()>0) {
      QRect track(r.right()-5,viewport.y(),4,viewport.height());p.fillRect(track,Qt::darkGray);
      const int height=qMax(16,qRound(viewport.height()*viewport.height()/document.size().height()));
      p.fillRect(QRect(track.x(),track.y()+qRound(offset/maximum()*(track.height()-height)),4,height),palette().link());
    }
  }
public:
  explicit Text(const QString &argument):Screen("Text") {
    showHeader=false;
    document.setUndoRedoEnabled(false);document.setDocumentMargin(0);
    QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);document.setDefaultTextOption(option);
    QUrl url(argument);
    if(argument.isEmpty())error="Open a text file from Files or another application.";
    else if(!QFileInfo::exists(argument) && !url.scheme().isEmpty() && !url.isLocalFile())
      error="Only local files are supported. Download the document first.";
    else {
      filename=url.isLocalFile()?url.toLocalFile():QFileInfo(argument).absoluteFilePath();
      QFile file(filename);
      if(!QFileInfo(filename).isFile())error="This is not a regular file.";
      else if(!file.open(QIODevice::ReadOnly))error="Cannot open text: "+file.errorString();
      else {
        constexpr int limit=2*1024*1024;
        auto bytes=file.read(limit);
        if(file.error()!=QFileDevice::NoError)error="Cannot read text: "+file.errorString();
        else {
          if(!file.atEnd())warning="First 2 MiB only";
          auto codec=QTextCodec::codecForUtfText(bytes,QTextCodec::codecForName("UTF-8"));
          QTextCodec::ConverterState state;
          auto value=codec->toUnicode(bytes.constData(),bytes.size(),&state);
          if(value.contains(QChar(0)))error="This file contains binary data, not readable text.";
          else {document.setPlainText(value);if(state.invalidChars)warning+=(warning.isEmpty()?"":" · ")+QString("Invalid characters replaced");}
        }
      }
    }
    setWindowTitle("Text — "+QFileInfo(filename).fileName());chrome();layout();
  }
};
int main(int argc,char **argv) {
  QApplication app(argc,argv);app.setApplicationName("reread-text");app.setDesktopFileName("reread-text");
  app.setFont(QFont("DejaVu Sans"));QCommandLineParser parser;
  parser.setApplicationDescription("Read-only ReRead text viewer");parser.addHelpOption();
  parser.addPositionalArgument("file","Local file path or file: URI","[file]");parser.process(app);
  if(parser.positionalArguments().size()>1)parser.showHelp(1);
  Text window(parser.positionalArguments().value(0));window.show();return app.exec();
}

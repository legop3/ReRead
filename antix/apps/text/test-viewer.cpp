#define main reread_text_main
#include "main.cpp"
#undef main
#include <stdexcept>
static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
struct TextTest {
  static void press(Text &w,reader_button id){w.dispatch(id,1);w.dispatch(id,0);QApplication::processEvents();}
  static int run(int argc,char **argv){
    QApplication app(argc,argv);QTemporaryDir directory;
    QString filename=directory.path()+"/document ' # ü %.txt";
    QByteArray original="<b>Plain text, not HTML</b>\n";
    for(int i=0;i<300;++i)original+=QString("Line %1 — Unicode café, and a long line that should wrap on the Reader's screen.\n").arg(i).toUtf8();
    QFile file(filename);require(file.open(QIODevice::WriteOnly),"Create fixture");file.write(original);file.close();
    Text viewer(QString::fromUtf8(QUrl::fromLocalFile(filename).toEncoded()));viewer.show();app.processEvents();
    require(viewer.error.isEmpty() && viewer.document.toPlainText().startsWith("<b>Plain"),"URI decoding and plain-text markup");
    require(viewer.maximum()>0 && !viewer.helpVisible(),"Wrapped document, help hidden");
    auto area=viewer.content();viewer.dispatch(READER_GUIDE,1);app.processEvents();
    require(viewer.helpVisible() && viewer.content()==area,"Help overlays without reserving space");
    viewer.dispatch(READER_GUIDE,0);require(!viewer.helpVisible(),"Release hides help");
    press(viewer,READER_RIGHT);require(viewer.offset>0,"Page advances");
    auto old=viewer.offset/viewer.maximum();press(viewer,READER_ZOOM_IN);
    require(viewer.fontSize==28 && qAbs(viewer.offset/viewer.maximum()-old)<0.001,"Text zoom preserves progress");
    for(int i=0;i<100;++i)press(viewer,READER_RIGHT);
    require(viewer.offset==viewer.maximum(),"Scroll clamps at document end");
    press(viewer,READER_OK);require(viewer.offset==0,"Return to beginning");
    press(viewer,READER_OPTIONS);require(viewer.information,"Information");
    press(viewer,READER_BACK);require(!viewer.information && viewer.isVisible(),"Back from information");
    require(file.open(QIODevice::ReadOnly) && file.readAll()==original,"Viewing never changes source");file.close();
    press(viewer,READER_BACK);require(!viewer.isVisible(),"Back closes document");
    QFile utf16(directory.path()+"/bom.txt");utf16.open(QIODevice::WriteOnly);utf16.write(QByteArray::fromHex("fffe430061006600e9000a00"));utf16.close();
    Text bom(utf16.fileName());require(bom.document.toPlainText().startsWith(QString::fromUtf8("Café")),"UTF-16 BOM");
    QFile large(directory.path()+"/large.txt");large.open(QIODevice::WriteOnly);QByteArray chunk(79,'x');chunk+='\n';
    for(int i=0;i<27000;++i)large.write(chunk);
    large.close();
    Text bounded(large.fileName());require(bounded.warning.contains("2 MiB") && bounded.document.characterCount()<=2*1024*1024+1,"Large preview is bounded and labelled");
    QFile binary(directory.path()+"/binary");binary.open(QIODevice::WriteOnly);binary.write("data\0binary",11);binary.close();
    Text rejected(binary.fileName());require(!rejected.error.isEmpty(),"Reject binary data");
    Text missing(directory.path()+"/missing");require(!missing.error.isEmpty(),"Missing file error");
    Text remote("https://example.org/a.txt");require(!remote.error.isEmpty(),"Remote URI is not mistaken for a path");
    return 0;
  }
};
int main(int argc,char **argv){return TextTest::run(argc,argv);}

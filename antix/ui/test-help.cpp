#include "reader-ui.hpp"
#include <stdexcept>
using namespace ReRead;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
class TestScreen : public Screen {
public:
  int guideDelivered=0;
  TestScreen():Screen("Test") {
    showHeader=false;
    controls={{READER_OK,"Activate"},{READER_UP,"Navigate"},{READER_BACK,"Close"}};
  }
  void draw(QPainter &p,const QRect &r) override {p.fillRect(r,Qt::blue);}
  void button(reader_button id,int) override {if(id==READER_GUIDE)++guideDelivered;}
};
static void events(){QApplication::processEvents();}
int main(int argc,char **argv) {
  QApplication app(argc,argv);TestScreen screen;screen.show();events();
  auto device=screen.findChild<Device *>();require(device,"Shared device adapter");device->setActive(true);
  const QRect content=screen.content();
  require(content==screen.safeArea(),"No footer space is reserved");
  auto overlay=screen.findChild<QWidget *>("reread-help");require(overlay && !overlay->isVisible(),"Help hidden by default");
  const auto before=screen.grab().toImage();screen.dispatch(READER_GUIDE,1);events();
  require(screen.helpVisible() && overlay->isVisible(),"Press shows shared overlay");
  require(screen.content()==content && screen.guideDelivered==0,"Guide belongs to shared UI, with stable layout");
  require(screen.grab().toImage()!=before,"Overlay changes visible pixels");
  screen.dispatch(READER_GUIDE,2);require(screen.helpVisible(),"Repeat is not a toggle");
  screen.dispatch(READER_GUIDE,0);events();require(!overlay->isVisible(),"Release hides overlay");
  require(screen.grab().toImage()==before,"Release restores content without layout changes");
  // Guide works even if a child widget normally consumes keyboard events.
  QLineEdit child(&screen);child.setGeometry(content);child.show();child.setFocus();events();device->setActive(true);
  QKeyEvent down(QEvent::KeyPress,Qt::Key_F1,Qt::NoModifier);QApplication::sendEvent(&child,&down);events();
  require(overlay->isVisible() && screen.children().indexOf(overlay)>screen.children().indexOf(&child),"Help is above child widgets");
  QKeyEvent up(QEvent::KeyRelease,Qt::Key_F1,Qt::NoModifier);QApplication::sendEvent(&child,&up);events();require(!overlay->isVisible(),"Child key release hides help");
  child.hide();screen.dispatch(READER_GUIDE,1);QEvent deactivate(QEvent::WindowDeactivate);QApplication::sendEvent(&screen,&deactivate);events();
  require(!screen.helpVisible(),"Focus loss clears held help");
  screen.dispatch(READER_GUIDE,2);require(!screen.helpVisible(),"A repeat cannot reopen help after focus loss");
  device->setActive(true);screen.dispatch(READER_GUIDE,1);
  QMetaObject::invokeMethod(device,"sleepChanged",Q_ARG(bool,true));require(!screen.helpVisible(),"Sleep clears help");
  QMetaObject::invokeMethod(device,"sleepChanged",Q_ARG(bool,false));
  screen.orientation=180;screen.controls.clear();
  for(int i=0;i<12;++i)screen.controls.append({READER_OK,"Context action "+QString::number(i)});
  screen.dispatch(READER_GUIDE,1);screen.update();events();
  require(screen.safeArea().contains(overlay->geometry()) && overlay->height()>50,"Long control lists wrap within safe bounds");
  require(overlay->y()==screen.safeArea().y(),"Inverted overlay follows screen orientation");
  screen.hide();require(!screen.helpVisible(),"Hiding the app clears help");
  return 0;
}

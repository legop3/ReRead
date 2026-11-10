// Starter for the recommended ReRead UI path; still an independent Qt/X11 app.
#include "reader-ui.hpp"
using namespace ReRead;

class Example : public Screen {
  int selected=0;
  const QStringList names={"First item","Second item","Third item"};
  const QStringList descriptions={"A collection uses the shared list/info layout.",
    "Hold the question-mark button for the controls overlay.",
    "Back or Favorites closes this ordinary X11 window."};
  void hideHelp() {showControls=false;update();}
protected:
  bool event(QEvent *event) override {
    if(event->type()==QEvent::WindowDeactivate || event->type()==QEvent::Hide)hideHelp();
    return Screen::event(event);
  }
  void idleChanged(bool inactive) override {if(inactive)hideHelp();}
  void button(reader_button id,int value) override {
    // Handle release before the ordinary navigation filter.
    if(id==READER_GUIDE){showControls=value!=0;update();return;}
    if(!value)return;
    if(id==READER_UP || id==READER_DOWN) {
      selected=qBound(0,selected+(id==READER_DOWN?1:-1),names.size()-1);update();return;
    }
    if(value!=1)return; // Repeats may navigate, but must not activate repeatedly.
    if(id==READER_BACK || id==READER_FAVORITES)close();
    else if(id==READER_OK)toast("Selected: "+names[selected]);
  }
  void draw(QPainter &p,const QRect &area) override {
    label(p,QRect(area.x(),area.y(),area.width(),40),"Example collection",28,true,palette().color(QPalette::Link));
    auto layout=listInfo(p,area.adjusted(0,52,0,0),names.size(),selected,60);
    for(int i=layout.first;i<qMin(layout.first+layout.visible,names.size());++i) {
      auto row=listInfoRow(p,layout,i,selected);
      auto color=palette().color(i==selected?QPalette::HighlightedText:QPalette::Text);
      label(p,row.adjusted(12,0,-12,0),names[i],24,i==selected,color);
    }
    text(p,layout.info,descriptions[selected],24);
  }
public:
  Example():Screen("Example") {
    setWindowTitle("ReRead example");
    showHeader=false;showControls=false;controlsOverlay=true;
    controls={{READER_UP,"Select"},{READER_OK,"Activate"},{READER_BACK,"Home"}};
  }
};
int main(int argc,char **argv) {
  QApplication app(argc,argv);app.setApplicationName("reread-example");
  app.setDesktopFileName("reread-example");app.setFont(QFont("DejaVu Sans"));
  Example window;window.show();return app.exec();
}

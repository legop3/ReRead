#include <gio/gio.h>
#include "reader-ui.hpp"
#include <QtNetwork>
#include <QtWidgets>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <memory>

using namespace ReRead;

namespace {
struct Address {
  QString host, selector, title;
  quint16 port=70;
  char type='1';
};
bool supported(char t) {
  return t=='0'||t=='1'||t=='4'||t=='5'||t=='6'||t=='9'||
         t=='g'||t=='I'||t=='s'||t=='d'||t=='P'||t==';';
}
QString kindName(char t) {
  switch(t) {
    case '1': return "DIRECTORY";
    case '0': return "TEXT";
    case '7': return "SEARCH · UNAVAILABLE";
    case 'g': case 'I': return "IMAGE";
    case 's': return "AUDIO";
    case 'd': case 'P': return "DOCUMENT";
    case ';': return "VIDEO";
    case '3': return "SERVER MESSAGE";
    case 'i': return "INFO";
    case 'h': return "WEB LINK · UNAVAILABLE";
    case '8': case 'T': return "INTERACTIVE · UNAVAILABLE";
    default: return "DOWNLOAD";
  }
}
struct Item { Address address; bool selectable=true; };
struct Page {
  Address address;
  QList<Item> items;
  QString document;
  bool text=false;
  int selected=-1, scroll=0;
};
QString key(const Address &a) {
  return QString::number(uchar(a.type))+"|"+a.host+"|"+QString::number(a.port)+"|"+a.selector;
}
QString display(const Address &a) {
  return "gopher://"+a.host+(a.port==70?"":":"+QString::number(a.port))+
         "/"+QChar(a.type)+QString::fromUtf8(QUrl::toPercentEncoding(a.selector,"/"));
}
QString safeName(QString s) {
  s=QFileInfo(s).fileName();
  s.replace(QRegularExpression("[^A-Za-z0-9._ -]"),"_");
  while(s.startsWith('.'))s.remove(0,1);
  if(s.isEmpty())s="gopher-download";
  return s.left(100);
}
QString dataPath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
QString filesPath() {
  return qEnvironmentVariable("REREAD_FILES_DIR","/var/lib/reader/files")+"/Downloads/Gopher";
}
QString decode(const QByteArray &b) {
  return QString::fromUtf8(b);
}
QList<Item> parseMenu(QByteArray bytes) {
  QList<Item> out;
  for(QByteArray line:bytes.split('\n')) {
    if(line.endsWith('\r'))line.chop(1);
    if(line=="." )break;
    if(line.isEmpty())continue;
    char type=line.at(0);
    const QList<QByteArray> parts=line.mid(1).split('\t');
    if(parts.isEmpty())continue;
    Item item;
    item.address.type=type;
    item.address.title=decode(parts.value(0));
    item.selectable=supported(type);
    if(parts.size()>=4) {
      item.address.selector=decode(parts.at(1));
      item.address.host=decode(parts.at(2));
      bool ok=false;int port=parts.at(3).toInt(&ok);
      if(!ok||port<1||port>65535)item.selectable=false;
      else item.address.port=quint16(port);
      if(item.address.host.isEmpty())item.selectable=false;
    } else item.selectable=false;
    if(out.size()>=2000)break;
    out << item;
  }
  return out;
}
QByteArray unstuff(QByteArray bytes) {
  QByteArray result;
  for(QByteArray line:bytes.split('\n')) {
    if(line.endsWith('\r'))line.chop(1);
    if(line==".")break;
    if(line.startsWith(".."))line.remove(0,1);
    result+=line+"\n";
  }
  return result;
}
}

class Gopher : public Screen {
  enum Mode { Home, Browsing, Bookmarks, History, Actions, ConfirmDownload };
  Mode mode=Home, previous=Browsing;
  QList<Page> trail;
  int index=-1, choice=0, homeChoice=0, actionChoice=0;
  QList<Address> marks, recent;
  QTcpSocket socket;
  QTimer timeout;
  QByteArray incoming;
  std::unique_ptr<QSaveFile> transfer;
  QString transferPath, status;
  bool downloading=false, busy=false;
  Address pending;
  int fontSize=25;
  QTextDocument doc;
  qreal textOffset=0;
  static constexpr qint64 menuLimit=2*1024*1024;
  static constexpr qint64 fileLimit=128LL*1024*1024;
  static constexpr qint64 minimumFree=64LL*1024*1024;
  static constexpr int headerHeight=48;

  Page *page() {return index>=0 && index<trail.size()?&trail[index]:nullptr;}
  const Page *page() const {return index>=0 && index<trail.size()?&trail[index]:nullptr;}
  void setStatus(const QString &s) {status=s;update();}
  void controlsForMode() {
    if(mode==Home || mode==Bookmarks || mode==History)
      controls={{READER_UP,"Select"},{READER_OK,"Open"},{READER_OPTIONS,"Actions"},{READER_BACK,"Back"}};
    else if(mode==Actions || mode==ConfirmDownload)
      controls={{READER_UP,"Select"},{READER_OK,"Choose"},{READER_BACK,"Cancel"}};
    else if(page()&&page()->text)
      controls={{READER_UP,"Scroll"},{READER_RIGHT,"Page"},{READER_ZOOM_IN,"Text size"},{READER_OPTIONS,"Actions"},{READER_BACK,"Back"}};
    else
      controls={{READER_UP,"Select"},{READER_OK,"Open"},{READER_OPTIONS,"Actions"},{READER_BACK,"Back"}};
    update();
  }
  void setMode(Mode m) {
    if(m!=mode && (m==Bookmarks||m==History))choice=0;
    mode=m;controlsForMode();
  }
  QStringList actions() const {
    QStringList a;
    if(previous==Browsing && page()) {
      a<<"Reload";
      a<<(bookmarked(page()->address)?"Remove page bookmark":"Bookmark this page");
      if(!page()->text && page()->selected>=0 && page()->selected<page()->items.size()) {
        const auto &selected=page()->items[page()->selected];
        if(selected.selectable)
          a<<(bookmarked(selected.address)?"Remove link bookmark":"Bookmark selected link");
      }
      a<<"Show address";
      if(index+1<trail.size())a<<"Forward";
    } else if(previous==Bookmarks && !marks.isEmpty())a<<"Remove bookmark";
    a<<"Bookmarks"<<"History"<<"Home";
    return a;
  }
  bool bookmarked(const Address &a) const {
    for(const auto &m:marks)if(key(m)==key(a))return true;
    return false;
  }
  void saveRecent() {
    QDir().mkpath(dataPath());
    QJsonArray arr;
    for(const auto &a:recent) {
      QJsonObject o;
      o["host"]=a.host;o["port"]=int(a.port);o["selector"]=a.selector;
      o["title"]=a.title;o["type"]=QString(QChar(a.type));
      arr.append(o);
    }
    QSaveFile f(dataPath()+"/history.json");
    if(f.open(QIODevice::WriteOnly)){
      f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));f.commit();
    }
  }
  void loadRecent() {
    QFile f(dataPath()+"/history.json");
    if(!f.open(QIODevice::ReadOnly))return;
    for(const auto &v:QJsonDocument::fromJson(f.readAll()).array()) {
      const auto o=v.toObject();Address a;
      a.host=o.value("host").toString();a.selector=o.value("selector").toString();
      a.title=o.value("title").toString();
      const QString t=o.value("type").toString("1");
      a.type=t.isEmpty()?'1':t.at(0).toLatin1();
      int port=o.value("port").toInt(70);
      if(a.host.isEmpty()||port<1||port>65535)continue;
      a.port=quint16(port);recent<<a;
      if(recent.size()>=70)break;
    }
  }
  void remember(const Address &a) {
    for(int i=recent.size()-1;i>=0;--i)
      if(key(recent[i])==key(a))recent.removeAt(i);
    recent.prepend(a);
    while(recent.size()>70)recent.removeLast();
    saveRecent();
  }
  void saveMarks() {
    QDir().mkpath(dataPath());
    QJsonArray arr;
    for(const Address &a:marks) {
      QJsonObject o;
      o["host"]=a.host;o["port"]=int(a.port);o["selector"]=a.selector;
      o["title"]=a.title;o["type"]=QString(QChar(a.type));
      arr.append(o);
    }
    QSaveFile f(dataPath()+"/bookmarks.json");
    if(f.open(QIODevice::WriteOnly)) {
      f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));f.commit();
    }
  }
  void loadMarks() {
    QFile f(dataPath()+"/bookmarks.json");
    if(!f.open(QIODevice::ReadOnly))return;
    for(auto v:QJsonDocument::fromJson(f.readAll()).array()) {
      auto o=v.toObject();Address a;
      a.host=o.value("host").toString();a.selector=o.value("selector").toString();
      a.title=o.value("title").toString();
      const QString t=o.value("type").toString("1");
      a.type=t.isEmpty()?'1':t.at(0).toLatin1();
      int port=o.value("port").toInt(70);
      if(port<1||port>65535||a.host.isEmpty())continue;
      a.port=quint16(port);marks<<a;
      if(marks.size()>=200)break;
    }
  }
  QList<Address> startSites() const {
    return {{"gopher.floodgap.com","","Floodgap Gopher",70,'1'}};
  }
  void disconnectRequest() {
    timeout.stop();
    busy=false;downloading=false;socket.abort();
    if(transfer){transfer->cancelWriting();transfer.reset();}
    incoming.clear();transferPath.clear();
  }
  void request(const Address &a,bool save=false) {
    if(a.host.isEmpty()||a.host.contains('/')||a.host.contains('\n')||a.host.contains('\r')||
       a.selector.contains('\n')||a.selector.contains('\r')) {
      setStatus("Invalid Gopher address");return;
    }
    disconnectRequest();
    pending=a;downloading=save;busy=true;
    setStatus(save?"Downloading...":"Connecting to "+a.host+"...");
    if(save) {
      QStorageInfo storage(filesPath());
      QDir().mkpath(filesPath());storage.refresh();
      if(!storage.isValid()||storage.bytesAvailable()<minimumFree) {
        disconnectRequest();setStatus("Not enough free storage");return;
      }
      QString name=safeName(a.title);
      if(QFileInfo(name).suffix().isEmpty()) {
        if(a.type=='g')name+=".gif";
        else if(a.type=='I')name+=".jpg";
        else if(a.type=='0')name+=".txt";
        else name+=".bin";
      }
      QString path=filesPath()+"/"+name;
      for(int n=1;QFileInfo::exists(path)&&n<10000;++n)
        path=filesPath()+"/"+QFileInfo(name).completeBaseName()+"-"+QString::number(n)+"."+QFileInfo(name).suffix();
      if(QFileInfo::exists(path)){disconnectRequest();setStatus("Cannot choose a unique filename");return;}
      transferPath=path;transfer.reset(new QSaveFile(path));
      if(!transfer->open(QIODevice::WriteOnly)) {
        disconnectRequest();setStatus("Cannot create download");return;
      }
    }
    socket.connectToHost(a.host,a.port);
    timeout.start(15000);
  }
  void open(const Address &a) {
    if(a.type=='7'){setStatus("Search requires text entry");return;}
    if(a.type=='3'||a.type=='i')return;
    if(a.type=='h'){setStatus("Web links need an HTML browser");return;}
    if(!supported(a.type)){setStatus("This item needs an unsupported service");return;}
    if(a.type=='0'||a.type=='1')request(a);
    else {pending=a;previous=mode;setMode(ConfirmDownload);}
  }
  void append(Page p) {
    while(trail.size()>index+1)trail.removeLast();
    remember(p.address);
    if(!p.text) {
      p.selected=-1;
      for(int i=0;i<p.items.size();++i)if(p.items[i].selectable){p.selected=i;break;}
    }
    trail<<p;index=trail.size()-1;
    while(trail.size()>70){trail.removeFirst();--index;}
    setMode(Browsing);status.clear();textOffset=0;layoutText();
  }
  void layoutText() {
    if(!page()||!page()->text)return;
    QFont f("DejaVu Sans");f.setPixelSize(fontSize);
    doc.setDefaultFont(f);doc.setDocumentMargin(0);
    QTextOption opts;opts.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    doc.setDefaultTextOption(opts);
    doc.setPlainText(page()->document);
    doc.setTextWidth(qMax(100,content().width()-24));
    textOffset=qBound(0.,textOffset,qMax(0.,doc.size().height()-bodyRect().height()));
  }
  QRect bodyRect() const {return content().adjusted(0,headerHeight,-12,-32);}
  void back() {
    if(busy){disconnectRequest();setStatus("Canceled");return;}
    if(mode==Actions||mode==ConfirmDownload){setMode(previous);return;}
    if(mode==Bookmarks||mode==History){setMode(Home);return;}
    if(mode==Browsing&&index>0){--index;setMode(Browsing);textOffset=0;layoutText();return;}
    setMode(Home);
  }
  void launchFile(const QString &path) {
    auto ctx=g_app_launch_context_new();
    g_app_launch_context_unsetenv(ctx,"READER_APP_ID");
    g_app_launch_context_unsetenv(ctx,"READER_APP_DATA_DIR");
    QByteArray uri=QUrl::fromLocalFile(path).toEncoded();
    GError *error=nullptr;
    bool ok=g_app_info_launch_default_for_uri(uri.constData(),ctx,&error);
    g_object_unref(ctx);
    if(!ok)setStatus(error?QString::fromUtf8(error->message):"No compatible file viewer installed");
    if(error)g_error_free(error);
  }
  void onFinished() {
    if(!busy)return;
    const auto error=socket.error();
    const QString reason=socket.errorString();
    timeout.stop();busy=false;
    if(error!=QAbstractSocket::UnknownSocketError && error!=QAbstractSocket::RemoteHostClosedError) {
      if(transfer){transfer->cancelWriting();transfer.reset();}
      downloading=false;setStatus("Connection failed: "+reason);return;
    }
    if(downloading) {
      bool ok=transfer && transfer->commit();
      transfer.reset();downloading=false;
      if(ok){setStatus("Saved to Downloads/Gopher");launchFile(transferPath);}
      else setStatus("Could not save download");
      return;
    }
    Page p;p.address=pending;p.text=pending.type=='0';
    if(p.text)p.document=decode(unstuff(incoming));
    else p.items=parseMenu(incoming);
    append(p);
    if(!p.text&&p.items.isEmpty())setStatus("Empty Gopher directory");
  }
  void onReady() {
    if(!busy)return;
    QByteArray chunk=socket.readAll();
    if(downloading) {
      if(!transfer || transfer->size()+chunk.size()>fileLimit ||
         transfer->write(chunk)!=chunk.size()) {
        disconnectRequest();setStatus("Download failed or exceeded 128 MiB");return;
      }
      return;
    }
    if(incoming.size()+chunk.size()>menuLimit) {
      disconnectRequest();setStatus("Gopher response exceeds 2 MiB");return;
    }
    incoming+=chunk;
  }
  void openSelection() {
    if(mode==Home) {
      if(homeChoice==0)open(startSites().first());
      else if(homeChoice==1)setMode(Bookmarks);
      else setMode(History);
      return;
    }
    if(mode==Bookmarks) {if(choice<marks.size())open(marks[choice]);return;}
    if(mode==History) {
      if(choice>=0&&choice<recent.size())open(recent[choice]);
      return;
    }
    if(mode==ConfirmDownload){setMode(previous);request(pending,true);return;}
    if(mode==Actions) {
      const QString action=actions().value(actionChoice);
      Mode source=previous;setMode(source);
      if(action=="Reload"&&page())request(page()->address);
      else if((action=="Bookmark this page"||action=="Bookmark selected link")&&page()) {
        Address a=page()->address;
        if(action=="Bookmark selected link" && page()->selected>=0 &&
           page()->selected<page()->items.size())a=page()->items[page()->selected].address;
        if(!bookmarked(a) && marks.size()<200){marks<<a;saveMarks();}
        setStatus("Bookmarked: "+a.title);
      } else if(action.startsWith("Remove ") && action.endsWith("bookmark")) {
        Address a;
        if(source==Bookmarks && choice>=0 && choice<marks.size())a=marks[choice];
        else if(action=="Remove link bookmark" && page() &&
                page()->selected>=0 && page()->selected<page()->items.size())
          a=page()->items[page()->selected].address;
        else if(page())a=page()->address;
        for(int i=marks.size()-1;i>=0;--i)if(key(marks[i])==key(a))marks.removeAt(i);
        saveMarks();
        if(source==Bookmarks)choice=qBound(0,choice,qMax(0,marks.size()-1));
        setStatus("Bookmark removed");
      } else if(action=="Show address"&&page())setStatus(display(page()->address));
      else if(action=="Forward"&&index+1<trail.size()) {++index;textOffset=0;layoutText();}
      else if(action=="Bookmarks")setMode(Bookmarks);
      else if(action=="History")setMode(History);
      else if(action=="Home")setMode(Home);
      return;
    }
    Page *p=page();if(!p)return;
    if(p->text){textOffset=0;update();return;}
    if(p->selected>=0 && p->selected<p->items.size() && p->items[p->selected].selectable)open(p->items[p->selected].address);
  }
  void move(int delta) {
    if(mode==Home){homeChoice=qBound(0,homeChoice+delta,2);}
    else if(mode==Actions){actionChoice=qBound(0,actionChoice+delta,actions().size()-1);}
    else if(mode==Bookmarks){choice=qBound(0,choice+delta,qMax(0,marks.size()-1));}
    else if(mode==History){choice=qBound(0,choice+delta,qMax(0,recent.size()-1));}
    else if(mode==Browsing&&page()) {
      if(page()->text) {
        textOffset=qBound(0.,textOffset+delta*QFontMetrics(doc.defaultFont()).lineSpacing(),
                          qMax(0.,doc.size().height()-bodyRect().height()));
      } else {
        int i=page()->selected;
        if(i<0)i=delta>0?-1:page()->items.size();
        do {i+=delta;} while(i>=0&&i<page()->items.size()&&!page()->items[i].selectable);
        if(i>=0&&i<page()->items.size())page()->selected=i;
      }
    }
    update();
  }
protected:
  void closeEvent(QCloseEvent *e) override {disconnectRequest();Screen::closeEvent(e);}
  void button(reader_button id,int value) override {
    if(!value)return;
    if(id==READER_UP||id==READER_DOWN) {
      if(mode!=ConfirmDownload)move(id==READER_DOWN?1:-1);
      return;
    }
    if(id==READER_LEFT||id==READER_RIGHT) {
      if(mode==Browsing&&page()&&page()->text) {
        textOffset=qBound(0.,textOffset+(id==READER_RIGHT?1:-1)*bodyRect().height()*.85,
                          qMax(0.,doc.size().height()-bodyRect().height()));update();
      }
      return;
    }
    if(value!=1)return;
    if(id==READER_BACK){back();return;}
    if(busy)return;
    if(id==READER_OPTIONS) {
      if(mode==Actions){setMode(previous);return;}
      if(mode==ConfirmDownload)return;
      previous=mode;actionChoice=0;setMode(Actions);return;
    }
    if(id==READER_ZOOM_IN||id==READER_ZOOM_OUT) {
      if(mode==Browsing&&page()&&page()->text) {
        fontSize=qBound(18,fontSize+(id==READER_ZOOM_IN?2:-2),40);layoutText();update();
      }
      return;
    }
    if(id==READER_OK)openSelection();
  }
  int menuRowHeight(const Item &item) const {
    if(item.address.type=='i')return item.address.title.trimmed().isEmpty()?18:34;
    if(item.address.type=='3')return 44;
    return 66;
  }
  void drawMenu(QPainter &p,const QRect &area,const Page &pg) {
    // Informational lines are document content, not inactive-looking buttons.
    // Only operable resources receive focus and navigation affordances.
    const int width=area.width();
    const int infoWidth=qMin(252,qMax(195,width/3));
    QRect left(area.x(),area.y(),width-infoWidth-14,area.height());
    QRect right(left.right()+14,area.y(),infoWidth,area.height());
    int y=0, selectionY=0, selectionHeight=0;
    for(int i=0;i<pg.items.size();++i) {
      if(i==pg.selected){selectionY=y;selectionHeight=menuRowHeight(pg.items[i]);}
      y+=menuRowHeight(pg.items[i]);
    }
    const int maxScroll=qMax(0,y-left.height());
    int offset=qBound(0,pg.scroll,maxScroll);
    if(pg.selected>=0) {
      if(selectionY<offset+12)offset=qMax(0,selectionY-12);
      if(selectionY+selectionHeight>offset+left.height()-12)
        offset=qMin(maxScroll,selectionY+selectionHeight-left.height()+12);
    }
    // Selection-based scrolling retains the directory's focused row.
    auto &stored=const_cast<Page&>(pg);
    stored.scroll=offset;
    p.save();p.setClipRect(left);
    int pos=left.y()-offset;
    QFont font=p.font();
    for(int i=0;i<pg.items.size();++i) {
      const Item &item=pg.items[i];
      const Address &a=item.address;
      const int h=menuRowHeight(item);
      QRect row(left.x(),pos,left.width()-6,h);
      pos+=h;
      if(row.bottom()<left.y()||row.top()>left.bottom())continue;
      if(a.type=='i') {
        if(!a.title.trimmed().isEmpty()) {
          font.setPixelSize(21);font.setBold(false);p.setFont(font);
          p.setPen(palette().color(QPalette::Text));
          p.drawText(row.adjusted(12,0,-8,0),Qt::AlignVCenter|Qt::TextSingleLine,
                     QFontMetrics(font).elidedText(a.title,Qt::ElideRight,row.width()-20));
        }
        continue;
      }
      if(a.type=='3') {
        font.setPixelSize(20);font.setBold(false);p.setFont(font);
        p.setPen(QColor("#ff9d82"));
        p.drawText(row.adjusted(12,0,-8,0),Qt::AlignVCenter|Qt::TextSingleLine,
                   QFontMetrics(font).elidedText(a.title,Qt::ElideRight,row.width()-20));
        continue;
      }
      const bool focus=item.selectable && i==pg.selected;
      if(focus)p.fillRect(row.adjusted(1,2,-2,-2),palette().color(QPalette::Highlight));
      else if(item.selectable)p.fillRect(row.adjusted(1,2,-2,-2),QColor("#151515"));
      const QColor fg=focus?palette().color(QPalette::HighlightedText):
                      item.selectable?palette().color(QPalette::Text):palette().color(QPalette::Mid);
      font.setPixelSize(23);font.setBold(focus);p.setFont(font);p.setPen(fg);
      QRect name(row.x()+14,row.y()+6,row.width()-45,32);
      p.drawText(name,Qt::AlignVCenter|Qt::TextSingleLine,
                 QFontMetrics(font).elidedText(a.title,Qt::ElideRight,name.width()));
      font.setPixelSize(17);font.setBold(false);p.setFont(font);
      p.setPen(focus?fg:(item.selectable?palette().color(QPalette::Link):palette().color(QPalette::Mid)));
      p.drawText(QRect(row.x()+14,row.y()+38,row.width()-32,23),
                 Qt::AlignVCenter|Qt::TextSingleLine,kindName(a.type));
      if(item.selectable) {
        font.setPixelSize(29);p.setFont(font);p.setPen(fg);
        p.drawText(QRect(row.right()-32,row.y()+13,27,36),Qt::AlignCenter,QString::fromUtf8("›"));
      }
    }
    p.restore();
    p.setPen(palette().color(QPalette::Mid));
    p.drawLine(right.left()-8,right.top(),right.left()-8,right.bottom());
    if(pg.selected>=0&&pg.selected<pg.items.size()) {
      const auto &a=pg.items[pg.selected].address;
      label(p,QRect(right.x()+7,right.y()+6,right.width()-14,32),kindName(a.type),20,true,
            palette().color(QPalette::Link));
      text(p,QRect(right.x()+7,right.y()+49,right.width()-14,120),
           a.title,23);
      label(p,QRect(right.x()+7,right.y()+194,right.width()-14,30),"SERVER",17,true,
            palette().color(QPalette::Mid));
      text(p,QRect(right.x()+7,right.y()+226,right.width()-14,62),
           a.host+":"+QString::number(a.port),19);
      text(p,QRect(right.x()+7,right.bottom()-90,right.width()-14,80),
           a.type=='1'?"Open this directory":a.type=='0'?"Read this document":
           "Open or download this item",19);
    } else {
      text(p,right.adjusted(10,8,-12,-8),"This page contains information, but no selectable links.",21);
    }
    if(y>left.height()){
      const int bar=qMax(20,left.height()*left.height()/y);
      const int yy=left.y()+(maxScroll?offset*(left.height()-bar)/maxScroll:0);
      p.fillRect(QRect(left.right()-4,yy,3,bar),palette().color(QPalette::Link));
    }
  }
  void draw(QPainter &p,const QRect &r) override {
    QString heading="Gopher";
    if(mode==Browsing && page())heading=page()->address.title;
    else if(mode==Bookmarks)heading="Bookmarks";
    else if(mode==History)heading="History";
    else if(mode==Actions)heading="Actions";
    else if(mode==ConfirmDownload)heading="Download file?";
    label(p,QRect(r.x(),r.y(),r.width(),38),heading,28,true,palette().color(QPalette::Link));
    QRect listArea=r.adjusted(0,headerHeight,0,-32);
    if(mode==Browsing&&page()&&page()->text) {
      p.save();p.setClipRect(bodyRect());
      p.translate(bodyRect().x(),bodyRect().y()-textOffset);
      QAbstractTextDocumentLayout::PaintContext context;
      context.palette=palette();
      context.clip=QRectF(0,textOffset,bodyRect().width(),bodyRect().height());
      doc.documentLayout()->draw(&p,context);
      p.restore();
    } else if(mode==ConfirmDownload) {
      text(p,listArea,"Save "+pending.title+" to Downloads/Gopher?\n\nOK to download and open, Back to cancel.",25);
    } else if(mode==Browsing && page() && !page()->text) {
      drawMenu(p,listArea,*page());
    } else {
      QStringList labels;
      QList<bool> enabled;
      int current=0;
      if(mode==Home) {
        labels=QStringList{"Browse Gopherspace","Bookmarks","History"};enabled=QList<bool>{true,true,true};current=homeChoice;
      } else if(mode==Bookmarks) {
        for(const auto &m:marks){labels<<m.title;enabled<<true;}current=choice;
      } else if(mode==History) {
        for(const auto &a:recent){labels<<a.title;enabled<<true;}current=choice;
      } else if(mode==Actions) {
        labels=actions();for(int i=0;i<labels.size();++i)enabled<<true;current=actionChoice;
      }
      if(labels.isEmpty())text(p,listArea,"Nothing here yet.",25);
      else {
        auto layout=listInfo(p,listArea,labels.size(),qBound(0,current,labels.size()-1),54);
        for(int i=layout.first;i<qMin(layout.first+layout.visible,labels.size());++i) {
          QRect row=listInfoRow(p,layout,i,current);
          const QColor fg=enabled.value(i)?
            palette().color(i==current?QPalette::HighlightedText:QPalette::Text):
            palette().color(QPalette::Mid);
          label(p,row.adjusted(10,0,-10,0),labels[i],23,i==current,fg);
        }
        QString info;
        if(mode==Bookmarks&&current<marks.size())info=display(marks[current]);
        else if(mode==Home)info="Explore Gopher servers without a keyboard.";
        if(!info.isEmpty())text(p,layout.info,info,20);
      }
    }
    if(busy)label(p,QRect(r.x(),r.bottom()-28,r.width(),26),downloading?"Downloading... Back cancels":"Connecting... Back cancels",18);
    else if(!status.isEmpty())label(p,QRect(r.x(),r.bottom()-28,r.width(),26),status,18);
  }
public:
  Gopher():Screen("Gopher") {
    showHeader=false;setWindowTitle("Gopher");
    timeout.setSingleShot(true);
    connect(&timeout,&QTimer::timeout,this,[this]{disconnectRequest();setStatus("Gopher server timed out");});
    connect(&socket,&QTcpSocket::connected,this,[this]{
      QByteArray selector=pending.selector.toUtf8()+"\r\n";
      socket.write(selector);socket.flush();
      timeout.start(15000);
      setStatus(downloading?"Downloading...":"Receiving...");
    });
    connect(&socket,&QTcpSocket::readyRead,this,&Gopher::onReady);
    connect(&socket,&QTcpSocket::disconnected,this,[this]{onReady();onFinished();});
    connect(&socket,QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),this,
            [this](QAbstractSocket::SocketError){onReady();onFinished();});
    loadMarks();loadRecent();controlsForMode();
  }
};
int main(int argc,char **argv) {
  QApplication app(argc,argv);app.setApplicationName("reread-gopher");
  app.setDesktopFileName("reread-gopher");app.setFont(QFont("DejaVu Sans"));
  Gopher window;window.show();return app.exec();
}

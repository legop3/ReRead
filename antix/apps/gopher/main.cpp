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
struct Item { Address address; bool selectable=true; };
struct Page {
  Address address;
  QList<Item> items;
  QString document;
  bool text=false;
  int selected=0, scroll=0;
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
    item.selectable=type!='i' && type!='3';
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
  QList<Address> marks;
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
  void setMode(Mode m) {mode=m;controlsForMode();}
  QStringList actions() const {
    if(mode!=Actions)return {};
    QStringList a;
    if(previous==Browsing && page()) {
      a<<"Reload";
      a<<(bookmarked(page()->address)?"Remove bookmark":"Add bookmark");
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
      a.title=o.value("title").toString();a.type=o.value("type").toString("1").at(0).toLatin1();
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
    socket.abort();busy=false;downloading=false;
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
    if(a.type=='7'){setStatus("Search needs ReRead text entry (coming later)");return;}
    if(a.type=='3'||a.type=='i')return;
    if(a.type=='h') {
      if(a.selector.startsWith("URL:")) {
        setStatus("External web links are not supported by this browser");
      } else setStatus("Unsupported Gopher item");
      return;
    }
    if(a.type!='0'&&a.type!='1'&&a.type!='g'&&a.type!='I'&&a.type!='9'&&a.type!='5'&&a.type!='s') {
      setStatus("Unsupported item type");return;
    }
    if(a.type=='0'||a.type=='1')request(a);
    else {pending=a;previous=Browsing;setMode(ConfirmDownload);}
  }
  void append(Page p) {
    while(trail.size()>index+1)trail.removeLast();
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
      if(ok){setMode(Browsing);setStatus("Saved to Downloads/Gopher");launchFile(transferPath);}
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
      int target=trail.size()-1-choice;
      if(target>=0 && target<trail.size()){index=target;setMode(Browsing);textOffset=0;layoutText();}
      return;
    }
    if(mode==ConfirmDownload){setMode(Browsing);request(pending,true);return;}
    if(mode==Actions) {
      const QString action=actions().value(actionChoice);
      Mode source=previous;setMode(source);
      if(action=="Reload"&&page())request(page()->address);
      else if(action=="Add bookmark"&&page()) {
        if(!bookmarked(page()->address)){marks<<page()->address;saveMarks();}
        setStatus("Bookmarked");
      } else if(action=="Remove bookmark") {
        Address a;
        if(source==Bookmarks && choice<marks.size())a=marks[choice];
        else if(page())a=page()->address;
        for(int i=marks.size()-1;i>=0;--i)if(key(marks[i])==key(a))marks.removeAt(i);
        saveMarks();setStatus("Bookmark removed");
      } else if(action=="Show address"&&page())setStatus(display(page()->address));
      else if(action=="Forward"&&index+1<trail.size()) {++index;layoutText();}
      else if(action=="Bookmarks")setMode(Bookmarks);
      else if(action=="History")setMode(History);
      else if(action=="Home")setMode(Home);
      return;
    }
    Page *p=page();if(!p)return;
    if(p->text){textOffset=0;update();return;}
    if(p->selected>=0 && p->selected<p->items.size())open(p->items[p->selected].address);
  }
  void move(int delta) {
    if(mode==Home){homeChoice=qBound(0,homeChoice+delta,2);}
    else if(mode==Actions){actionChoice=qBound(0,actionChoice+delta,actions().size()-1);}
    else if(mode==Bookmarks){choice=qBound(0,choice+delta,qMax(0,marks.size()-1));}
    else if(mode==History){choice=qBound(0,choice+delta,qMax(0,trail.size()-1));}
    else if(mode==Browsing&&page()) {
      if(page()->text) {
        textOffset=qBound(0.,textOffset+delta*QFontMetrics(doc.defaultFont()).lineSpacing(),
                          qMax(0.,doc.size().height()-bodyRect().height()));
      } else {
        int i=page()->selected;
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
    } else {
      QStringList labels;
      QList<bool> enabled;
      int current=0;
      if(mode==Home) {
        labels={"Browse Gopherspace","Bookmarks","History"};enabled={true,true,true};current=homeChoice;
      } else if(mode==Bookmarks) {
        for(const auto &m:marks){labels<<m.title;enabled<<true;}current=choice;
      } else if(mode==History) {
        for(int i=trail.size()-1;i>=0;--i){labels<<trail[i].address.title;enabled<<true;}current=choice;
      } else if(mode==Actions) {
        labels=actions();for(int i=0;i<labels.size();++i)enabled<<true;current=actionChoice;
      } else if(page()) {
        const auto *pg=page();
        for(const auto &item:pg->items){labels<<item.address.title;enabled<<item.selectable;}
        current=pg->selected;
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
        if(mode==Browsing&&page()&&current<page()->items.size()) {
          const auto &a=page()->items[current].address;
          info=(a.type=='1'?"Directory":a.type=='0'?"Text":a.type=='7'?"Search unavailable":
                a.type=='i'?"Information":a.type=='3'?"Server error":"File")+
               QString("\n\n")+display(a);
        } else if(mode==Bookmarks&&current<marks.size())info=display(marks[current]);
        else if(mode==Home)info="Explore Gopher servers without a keyboard.";
        if(!info.isEmpty())text(p,layout.info,info,20);
      }
    }
    if(busy)label(p,QRect(r.x(),r.bottom()-28,r.width(),26),"Connecting... Back cancels",18);
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
            [this](QAbstractSocket::SocketError){onFinished();});
    loadMarks();controlsForMode();
  }
};
int main(int argc,char **argv) {
  QApplication app(argc,argv);app.setApplicationName("reread-gopher");
  app.setDesktopFileName("reread-gopher");app.setFont(QFont("DejaVu Sans"));
  Gopher window;window.show();return app.exec();
}

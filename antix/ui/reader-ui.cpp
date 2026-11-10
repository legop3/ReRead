#include "reader-ui.hpp"
#include <memory>
namespace ReRead {
static reader_button keyButton(QKeyEvent *e) {
  auto id=e->nativeScanCode()>=8 ? reader_button_from_key(e->nativeScanCode()-8) : READER_NONE;
  if(id!=READER_NONE)return id;
  switch(e->key()) {
    case Qt::Key_Up:return READER_UP; case Qt::Key_Down:return READER_DOWN;
    case Qt::Key_Left:return READER_LEFT; case Qt::Key_Right:return READER_RIGHT;
    case Qt::Key_Return:case Qt::Key_Enter:return READER_OK;
    case Qt::Key_Escape:return READER_BACK;case Qt::Key_F1:return READER_GUIDE;
    case Qt::Key_Menu:return READER_OPTIONS;case Qt::Key_Plus:return READER_ZOOM_IN;
    case Qt::Key_Minus:return READER_ZOOM_OUT;default:return READER_NONE;
  }
}
class HelpOverlay final : public QWidget {
  Screen &screen;
  void paintEvent(QPaintEvent *) override {QPainter painter(this);screen.paintHelp(painter);}
public:
  explicit HelpOverlay(Screen &owner):QWidget(&owner),screen(owner) {
    setObjectName("reread-help");setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents);hide();
  }
};
Screen::Screen(const QString &name) : title(name) {
  // Per-launch grip hint; no global display state is changed.
  orientation=qEnvironmentVariableIntValue("REREAD_SCREEN_ORIENTATION")==180?180:0;
  QPalette colors;
  colors.setColor(QPalette::Window, QColor("#000000"));
  colors.setColor(QPalette::WindowText, QColor("#ffffff"));
  colors.setColor(QPalette::Base, Qt::black);
  colors.setColor(QPalette::Text, Qt::white);
  colors.setColor(QPalette::Mid, QColor("#ffffff"));
  colors.setColor(QPalette::Dark, QColor("#ffffff"));
  colors.setColor(QPalette::Highlight, Qt::white);
  colors.setColor(QPalette::Link, QColor("#00bfff"));
  colors.setColor(QPalette::HighlightedText, Qt::black);
  colors.setColor(QPalette::Button, Qt::black);
  colors.setColor(QPalette::ButtonText, Qt::white);
  colors.setColor(QPalette::ToolTipBase, Qt::white);
  colors.setColor(QPalette::ToolTipText, Qt::black);
  setPalette(colors);
  setWindowFlags(Qt::Window);
  setCursor(Qt::BlankCursor);
  resize(960, 544);
  helpOverlay=new HelpOverlay(*this);
  qApp->installEventFilter(this);
  device = new Device(this);
  device->onButton([this](reader_button id, int value) {
    if(id==READER_GUIDE){if(value!=2)setHelp(value==1);return;}
    if (orientation == 180) {
      if (id == READER_UP) id = READER_DOWN;
      else if (id == READER_DOWN) id = READER_UP;
      else if (id == READER_LEFT) id = READER_RIGHT;
      else if (id == READER_RIGHT) id = READER_LEFT;
    }
    button(id, value);
  });
  device->onIdle([this](bool dimmed) { if(dimmed)setHelp(false);idleChanged(dimmed); });
  device->onHold([this](bool locked) { if(locked)setHelp(false);update(); });
  device->onNotice([this](const QString &message) { toast(message); });
  device->onResume([this] { update(); });
  connect(qGuiApp,&QGuiApplication::applicationStateChanged,this,[this](Qt::ApplicationState state){
    device->setActive(isVisible() && state==Qt::ApplicationActive);
  });
  connect(&clock, &QTimer::timeout, this, [this] {
    if (isVisible())
      update();
  });
  clock.start(30000);
  auto batteryWatcher = new QFileSystemWatcher(this);
  const QString batteryPath = "/run/reader-battery.json";
  batteryWatcher->addPath(batteryPath);
  auto displayedBattery = std::make_shared<QByteArray>();
  connect(batteryWatcher, &QFileSystemWatcher::fileChanged, this,
          [this, batteryWatcher, batteryPath, displayedBattery] {
            batteryWatcher->addPath(batteryPath);
            auto state = readJson(batteryPath);
            QJsonObject visible;
            for (const auto &key : {"capacity_percent", "external_power",
                                    "level", "action_pending"})
              visible[key] = state[key];
            auto signature =
                QJsonDocument(visible).toJson(QJsonDocument::Compact);
            if (signature != *displayedBattery) {
              *displayedBattery = signature;
              if (isVisible())
                update();
            }
          });
}
Screen::~Screen() { qApp->removeEventFilter(this);device->setActive(false); }
void Screen::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  device->setActive(true);
}
void Screen::hideEvent(QHideEvent *event) {
  device->setActive(false);
  QWidget::hideEvent(event);
}
void Screen::dispatch(reader_button id, int value) {
  if (isVisible()) device->dispatch(id, value);
}
QRect Screen::safeArea() const { return rect().adjusted(14, 14, -14, -14); }
QRect Screen::content() const {
  return safeArea().adjusted(0, showHeader ? 56 : 0, 0, 0);
}
bool Screen::helpVisible() const {return helpHeld && !controls.isEmpty();}
void Screen::setHelp(bool held) {
  if(helpHeld==held)return;
  helpHeld=held;syncHelp();
}
bool Screen::event(QEvent *event) {
  if(event->type()==QEvent::WindowDeactivate || event->type()==QEvent::Hide)setHelp(false);
  return QWidget::event(event);
}
bool Screen::eventFilter(QObject *object,QEvent *event) {
  auto target=qobject_cast<QWidget *>(object);
  if(target && (target==this || isAncestorOf(target)) &&
     (event->type()==QEvent::KeyPress || event->type()==QEvent::KeyRelease)) {
    auto key=static_cast<QKeyEvent *>(event);
    if(keyButton(key)==READER_GUIDE) {
      if(event->type()==QEvent::KeyPress)dispatch(READER_GUIDE,key->isAutoRepeat()?2:1);
      else if(!key->isAutoRepeat())dispatch(READER_GUIDE,0);
      return true;
    }
  }
  return QWidget::eventFilter(object,event);
}
QVector<QRect> Screen::helpGroups() const {
  QFont font=this->font();font.setPixelSize(21);QFontMetrics metrics(font);
  const int available=qMax(1,safeArea().width()-16);int x=8,y=8;
  QVector<QRect> groups;
  for(const auto &control:controls) {
    int width=qMin(available,58+metrics.horizontalAdvance(control.action));
    if(x>8 && x+width>available+8){x=8;y+=50;}
    groups.append(QRect(x,y,width,42));x+=width+16;
  }
  return groups;
}
void Screen::syncHelp() {
  if(!isVisible() || !helpVisible()){helpOverlay->hide();return;}
  auto groups=helpGroups();
  const auto safe=safeArea();int height=groups.isEmpty()?0:groups.last().bottom()+9;
  QRect area(safe.left(),safe.bottom()-height+1,safe.width(),height);
  if(orientation==180)area.moveTop(height?this->height()-safe.bottom()-1:0);
  helpOverlay->setGeometry(area);
  helpOverlay->setVisible(isVisible() && helpVisible());
  if(helpVisible()){helpOverlay->raise();helpOverlay->update();}
}
void Screen::paintHelp(QPainter &p) {
  if(orientation==180){p.translate(helpOverlay->width(),helpOverlay->height());p.rotate(180);}
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(helpOverlay->rect(),Qt::black);p.setPen(QPen(Qt::white,2));
  p.drawRect(helpOverlay->rect().adjusted(1,1,-1,-1));
  const auto groups=helpGroups();
  for(int i=0;i<groups.size();++i) {
    const auto box=groups[i];
    buttonIcon(p,QRect(box.x()+4,box.y()+5,32,32),controls[i].button);
    label(p,box.adjusted(45,0,0,0),controls[i].action,21,false,Qt::white);
  }
}
// Physical control glyphs are standard SVG icon-theme assets.
void Screen::buttonIcon(QPainter &p, const QRect &r, reader_button id) {
  static QHash<int,QIcon> icons;
  if(!icons.contains(id)) {
    QString name=QString::fromLatin1(reader_button_icon_name(id));
    icons.insert(id,QIcon::fromTheme(name,QIcon("/usr/share/icons/hicolor/scalable/actions/"+name+".svg")));
  }
  icons.value(id).paint(&p,r);
}
void Screen::text(QPainter &p, const QRect &r, const QString &t, int size,
                  QColor color) {
  auto f = p.font();
  f.setPixelSize(size);
  p.setFont(f);
  if (!color.isValid())
    color = palette().color(QPalette::WindowText);
  p.setPen(color);
  p.drawText(r, Qt::AlignVCenter | Qt::TextWordWrap, t);
}
void Screen::paintEvent(QPaintEvent *) {
  QPainter p(this);
  if (orientation == 180) {
    p.translate(width(), height());
    p.rotate(180);
  }
  p.fillRect(rect(), palette().window());
  p.setRenderHint(QPainter::Antialiasing);
  QRect safe = safeArea();
  auto battery = readJson("/run/reader-battery.json");
  if (showHeader) {
    auto heading = p.font(); heading.setBold(true); p.setFont(heading);
    text(p, QRect(safe.left(), safe.top(), safe.width()/2, 42), title, 32);
    heading.setBold(false); p.setFont(heading);
    p.setPen(QPen(palette().link(), 2));
    p.drawLine(safe.left(), safe.top()+48, safe.right(), safe.top()+48);
    auto f = p.font(); f.setPixelSize(22); p.setFont(f);
    const int charge = battery.value("capacity_percent").toInt();
    const bool known = battery.contains("capacity_percent");
    const bool ac = battery.value("external_power").toBool();
    QColor statusColor = !known ? QColor("#ffffff") : charge <= 10 ? QColor("#ff3030") :
        charge <= 20 ? QColor("#ffd000") : QColor("#00df40");
    QRect badge(safe.right()-235,safe.top()+3,135,34);
    p.fillRect(badge,statusColor);
    p.setPen(Qt::black);
    p.drawText(badge,Qt::AlignCenter,(ac ? "AC " : "BAT ") +
               (known ? QString::number(charge)+"%" : "--"));
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRect(safe.right()-90,safe.top(),90,42),Qt::AlignRight|Qt::AlignVCenter,
               QTime::currentTime().toString("HH:mm"));
  }
  draw(p, content());
  syncHelp();
  QString warning;
  const QString level = battery.value("level").toString();
  if (level == "critical" || level == "low" || level == "warning") {
    warning = level == "critical" ? "Critical battery — connect power" : "Low battery — connect power";
    if (battery.value("action_pending").toBool())
      warning = "Battery exhausted — shutting down soon; connect power";
  }
  QString message = device->isLocked() ? "Hold locked — slide switch to unlock" : !warning.isEmpty() ? warning : notice;
  if (!message.isEmpty()) {
    QRect box(safe.left(),safe.top()+52,safe.width(),48);
    p.setPen(QPen(palette().windowText(), 2)); p.setBrush(palette().toolTipBase());
    p.drawRect(box);
    text(p,box.adjusted(12,0,-12,0),message,23,palette().color(QPalette::ToolTipText));
  }
}
QRect Screen::ListInfoLayout::row(int index, int gap) const {
  if(index<first || index>=first+visible)return {};
  return QRect(entries.x(),entries.y()+(index-first)*rowHeight,
               entries.width(),qMax(1,rowHeight-qMax(0,gap)));
}
Screen::ListInfoLayout Screen::listInfo(QPainter &p,const QRect &area,int items,
                                       int selected,int rowHeight,qreal listShare) {
  const int split=area.x()+qRound(area.width()*qBound(0.3,listShare,0.75));
  const int height=qMax(1,qMin(qMax(1,rowHeight),area.height()));
  const int visible=qMax(1,area.height()/height);
  const int index=qBound(0,selected,qMax(0,items-1));
  p.save();p.setPen(QPen(palette().windowText(),1));
  p.drawLine(split,area.top(),split,area.bottom());p.restore();
  return {QRect(area.x(),area.y(),qMax(1,split-area.x()-18),area.height()),
          QRect(split+18,area.y(),qMax(1,area.right()-split-18),area.height()),
          (index/visible)*visible,visible,height};
}
QRect Screen::listInfoRow(QPainter &p,const ListInfoLayout &layout,int index,
                          int selected,int gap) {
  const auto rect=layout.row(index,gap);
  if(index==selected)p.fillRect(rect,palette().highlight());
  return rect;
}
void Screen::label(QPainter &p,const QRect &r,const QString &value,int pixels,
                   bool bold,QColor color,Qt::TextElideMode elide) {
  p.save();auto font=p.font();font.setPixelSize(pixels);font.setBold(bold);p.setFont(font);
  p.setPen(color.isValid()?color:palette().color(QPalette::Text));
  p.drawText(r,Qt::AlignVCenter,QFontMetrics(font).elidedText(value,elide,r.width()));p.restore();
}
void Screen::list(QPainter &p, const QRect &r, const QStringList &labels,
                  const QStringList &details, int selected) {
  int visible = qMax(1, r.height() / 84);
  int first = qMax(0, selected - visible + 1);
  for (int i = first; i < labels.size() && i < first + visible; i++) {
    QRect row(r.x(), r.y() + (i - first) * 84, r.width(), 76);
    if (i == selected) {
      p.fillRect(row, palette().highlight());
      p.fillRect(QRect(row.left(),row.top(),6,row.height()),palette().link());
    } else {
      p.setPen(QPen(palette().mid(),1));
      p.drawLine(row.left()+18,row.bottom(),row.right()-18,row.bottom());
    }
    auto font = p.font(); font.setBold(i == selected); p.setFont(font);
    text(p, row.adjusted(18, details.value(i).isEmpty() ? 0 : -16, -32, 0),
         labels[i], 27, palette().color(i == selected ? QPalette::HighlightedText : QPalette::Text));
    font.setBold(false); p.setFont(font);
    if (!details.value(i).isEmpty())
      text(p, QRect(row.x() + 18, row.y() + 43, row.width() - 42, 25),
           details[i], 19, palette().color(i == selected ? QPalette::HighlightedText : QPalette::Text));
  }
  if (labels.isEmpty())
    text(p, r, "Nothing here yet.");
  if (labels.size() > visible)
    text(p, QRect(r.right() - 75, r.bottom() - 25, 75, 25),
         QString::number(selected + 1) + " / " + QString::number(labels.size()),
         17, palette().color(QPalette::Dark));
}
void Screen::toast(const QString &t) {
  notice = t;
  update();
  QTimer::singleShot(3000, this, [this, t] {
    if (notice == t) {
      notice.clear();
      update();
    }
  });
}
void Screen::keyPressEvent(QKeyEvent *event) {
  auto id=keyButton(event);
  if(id==READER_NONE) { QWidget::keyPressEvent(event); return; }
  dispatch(id,event->isAutoRepeat()?2:1);
  event->accept();
}
void Screen::keyReleaseEvent(QKeyEvent *event) {
  auto id=keyButton(event);
  if(id==READER_NONE) { QWidget::keyReleaseEvent(event); return; }
  if(!event->isAutoRepeat())dispatch(id,0);
  event->accept();
}
} // namespace ReRead

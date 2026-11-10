#include "reader-ui.hpp"
#include <memory>
namespace ReRead {
Screen::Screen(const QString &name) : title(name) {
  QPalette colors;
  colors.setColor(QPalette::Window, QColor("#000000"));
  colors.setColor(QPalette::WindowText, QColor("#ffffff"));
  colors.setColor(QPalette::Base, QColor("#000000"));
  colors.setColor(QPalette::Text, QColor("#ffffff"));
  colors.setColor(QPalette::Mid, QColor("#ffffff"));
  colors.setColor(QPalette::Dark, QColor("#ffffff"));
  colors.setColor(QPalette::Highlight, QColor("#00bfff"));
  colors.setColor(QPalette::Link, QColor("#00bfff"));
  colors.setColor(QPalette::HighlightedText, Qt::black);
  colors.setColor(QPalette::Button, QColor("#000000"));
  colors.setColor(QPalette::ButtonText, QColor("#ffffff"));
  setPalette(colors);
  setWindowFlags(Qt::FramelessWindowHint);
  setCursor(Qt::BlankCursor);
  resize(960, 544);
  device = new Device(this);
  device->onButton([this](reader_button id, int value) {
    if (orientation == 180) {
      if (id == READER_UP) id = READER_DOWN;
      else if (id == READER_DOWN) id = READER_UP;
      else if (id == READER_LEFT) id = READER_RIGHT;
      else if (id == READER_RIGHT) id = READER_LEFT;
    }
    button(id, value);
  });
  device->onIdle([this](bool dimmed) { idleChanged(dimmed); });
  device->onHold([this](bool) { update(); });
  device->onNotice([this](const QString &message) { toast(message); });
  device->onResume([this] { update(); });
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
Screen::~Screen() { device->setActive(false); }
void Screen::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  device->setActive(true);
}
void Screen::hideEvent(QHideEvent *event) {
  device->setActive(false);
  QWidget::hideEvent(event);
}
void Screen::foreground(bool enabled) {
  if (enabled) { showFullScreen(); update(); }
  else { device->setActive(false); hide(); }
}
void Screen::dispatch(reader_button id, int value) {
  if (isVisible()) device->dispatch(id, value);
}
QRect Screen::safeArea() const { return rect().adjusted(14, 14, -14, -14); }
QRect Screen::content() const {
  return safeArea().adjusted(0, showHeader ? 56 : 0, 0,
      showControls && !controlsOverlay && !controls.isEmpty() ? -56 : 0);
}
// Simple vector glyphs follow the symbols on the physical Reader controls.
// They are drawn in code so they stay crisp at the panel's unusual scale.
void Screen::buttonIcon(QPainter &p, const QRect &r, reader_button id) {
  p.save();
  p.translate(r.center());
  p.scale(r.width() / 32.0, r.height() / 32.0);
  p.setPen(QPen(QColor("#ffffff"), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  switch (id) {
  case READER_BACK: {
    QPolygonF arrow; arrow << QPointF(10,10) << QPointF(-9,-9)
                         << QPointF(-9,3) << QPointF(-9,-9) << QPointF(3,-9);
    p.drawPolyline(arrow); break;
  }
  case READER_OPTIONS:
    p.drawRoundedRect(QRectF(-12,-11,24,22),2,2);
    for (int y : {-6,0,6}) p.drawLine(-7,y,7,y);
    break;
  case READER_VIEW_TOGGLE:
    p.drawRect(QRectF(-13,-10,18,15));
    p.drawRect(QRectF(-5,-3,18,15)); break;
  case READER_OK:
    p.drawEllipse(QRectF(-11,-11,22,22));
    p.setBrush(QColor("#ffffff")); p.drawEllipse(QRectF(-3,-3,6,6)); break;
  case READER_SHOOT:
    p.drawRoundedRect(QRectF(-13,-8,26,18),3,3);
    p.drawEllipse(QRectF(-5,-5,10,10)); p.drawLine(-6,-12,5,-12); break;
  case READER_UP: case READER_DOWN: case READER_LEFT: case READER_RIGHT: {
    if (id == READER_DOWN) p.rotate(180);
    if (id == READER_LEFT) p.rotate(-90);
    if (id == READER_RIGHT) p.rotate(90);
    p.drawLine(0,11,0,-10); p.drawLine(0,-10,-7,-3); p.drawLine(0,-10,7,-3); break;
  }
  case READER_ZOOM_IN: case READER_ZOOM_OUT:
    p.drawLine(-10,0,10,0);
    if (id == READER_ZOOM_IN) p.drawLine(0,-10,0,10);
    break;
  case READER_GUIDE:
    { auto f=p.font(); f.setPixelSize(29); p.setFont(f);
      p.drawText(QRect(-16,-16,32,32),Qt::AlignCenter,"?"); } break;
  case READER_PLAY:
    p.drawLine(-10,0,10,0); p.drawLine(10,0,3,-7); p.drawLine(10,0,3,7); break;
  default: break;
  }
  p.restore();
}
void Screen::text(QPainter &p, const QRect &r, const QString &t, int size,
                  QColor color) {
  auto f = p.font();
  f.setPixelSize(size);
  p.setFont(f);
  // Older app binaries compiled the previous light-text default into calls.
  // Preserve their ABI while allowing them to inherit the new foreground.
  if (!color.isValid() || color == QColor("#eef3f8"))
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
  if (showControls && !controls.isEmpty()) {
    int total = 0;
    QFont f = p.font(); f.setPixelSize(21); p.setFont(f);
    QFontMetrics metrics(f);
    for (const auto &control : controls) total += 58 + metrics.horizontalAdvance(control.action) + 16;
    int x = safe.left();
    // Explicit icon/action groups replace ambiguous runs of button names.
    // Apps choose these groups, whether to show them and whether they overlay.
    double scale = qMin(1.0, double(safe.width()) / qMax(1,total));
    p.save(); p.translate(x, safe.bottom()-43); p.scale(scale,scale);
    x = 0;
    for (const auto &control : controls) {
      int w = 58 + metrics.horizontalAdvance(control.action);
      p.setPen(Qt::NoPen); p.setBrush(palette().base());
      p.drawRect(QRect(x,0,w,42));
      p.setPen(QPen(Qt::white,1)); p.setBrush(Qt::black);
      p.drawRect(QRect(x+5,2,38,38));
      buttonIcon(p,QRect(x+9,6,30,30),control.button);
      text(p,QRect(x+48,0,w-52,42),control.action,21);
      x += w+16;
    }
    p.restore();
  }
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
    p.setPen(QPen(palette().windowText(), 2)); p.setBrush(palette().base());
    p.drawRect(box);
    text(p,box.adjusted(12,0,-12,0),message,23);
  }
}
void Screen::list(QPainter &p, const QRect &r, const QStringList &labels,
                  const QStringList &details, int selected) {
  int visible = qMax(1, r.height() / 84);
  int first = qMax(0, selected - visible + 1);
  for (int i = first; i < labels.size() && i < first + visible; i++) {
    QRect row(r.x(), r.y() + (i - first) * 84, r.width(), 76);
    p.setPen(QPen(i == selected ? palette().link() : palette().mid(),
                  i == selected ? 3 : 2));
    p.setBrush(i == selected ? palette().highlight() : palette().base());
    p.drawRect(row.adjusted(1,1,-1,-1));
    if (i == selected) p.fillRect(QRect(row.left()+1,row.top()+1,5,row.height()-2),palette().link());
    auto font = p.font(); font.setBold(i == selected); p.setFont(font);
    text(p, row.adjusted(18, details.value(i).isEmpty() ? 0 : -16, -32, 0),
         labels[i], 27, palette().color(i == selected ? QPalette::HighlightedText : QPalette::WindowText));
    font.setBold(false); p.setFont(font);
    if (!details.value(i).isEmpty())
      text(p, QRect(row.x() + 18, row.y() + 43, row.width() - 42, 25),
           details[i], 19, palette().color(i == selected ? QPalette::HighlightedText : QPalette::Dark));
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
void Screen::keyPressEvent(QKeyEvent *e) {
  reader_button id = READER_NONE;
  switch (e->key()) {
  case Qt::Key_Up:
    id = READER_UP;
    break;
  case Qt::Key_Down:
    id = READER_DOWN;
    break;
  case Qt::Key_Left:
    id = READER_LEFT;
    break;
  case Qt::Key_Right:
    id = READER_RIGHT;
    break;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    id = READER_OK;
    break;
  case Qt::Key_Escape:
    id = READER_BACK;
    break;
  default:
    break;
  }
  dispatch(id, e->isAutoRepeat() ? 2 : 1);
}
} // namespace ReRead

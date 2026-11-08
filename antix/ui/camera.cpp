#include "reader-ui.hpp"
using namespace ReRead;
class Camera : public Screen {
  QImage frame;
  QTimer capturePoll;
  quint64 sequence = 0;
  bool idlePaused = false, livePending = false, busy = false, flash = false,
       document = false, options = false, review = false;
  int option = 0, zoom = 1, rotation = 180;
  QString job;
  void saveSettings() {
    writeJson(
        qEnvironmentVariable("READER_APP_DATA_DIR",
                             "/var/lib/reader/apps/reread-camera") +
            "/settings.json",
        {{"flash", flash}, {"document", document}, {"rotation", rotation}});
  }
  void idleChanged(bool paused) override {
    idlePaused = paused;
    if (!paused)
      live();
  }
  void live() {
    if (review || idlePaused || livePending)
      return;
    livePending = true;
    cameraRequest(
        "/rgb?after=" + QString::number(sequence), {},
        [this](int code, QByteArray data,
               QMap<QByteArray, QByteArray> headers) {
          livePending = false;
          if (review || idlePaused)
            return;
          if (code == 200) {
            int width = headers.value("x-width").toInt();
            int height = headers.value("x-height").toInt();
            QImage image;
            if (width > 0 && width <= 2592 && height > 0 && height <= 1920 &&
                data.size() == width * height * 3)
              image = QImage(reinterpret_cast<const unsigned char *>(
                                 data.constData()),
                             width, height, width * 3, QImage::Format_RGB888)
                          .convertToFormat(QImage::Format_RGB32);
            if (!image.isNull()) {
              sequence = headers.value("x-frame").toULongLong();
              frame = image;
              QString previousSubtitle = subtitle;
              subtitle = busy ? "Focusing and saving…"
                              : QString(document ? "Document" : "Photo") +
                                    " · " + (flash ? "Flash on" : "Flash off") +
                                    " · " + QString::number(zoom) + "×";
              if (previousSubtitle == subtitle)
                update(content());
              else
                update();
            }
          } else if (!busy) {
            subtitle = "Camera unavailable · retrying";
            update();
          }
          QTimer::singleShot(code == 200 ? 0 : 2000, this, [this] { live(); });
        },
        this);
  }
  void capture() {
    if (busy)
      return;
    busy = true;
    if (review)
      frame = QImage();
    review = false;
    live();
    subtitle = "Focusing and saving… Hold still.";
    update();
    cameraRequest(
        "/capture",
        {{"flash", flash},
         {"mode", document ? "document" : "photo"},
         {"rotation", rotation}},
        [this](int code, QByteArray data, QMap<QByteArray, QByteArray>) {
          auto response = QJsonDocument::fromJson(data).object();
          if (code == 202) {
            job = response.value("id").toString();
            capturePoll.start(400);
          } else {
            busy = false;
            toast(response.value("error").toString("Unable to take photo"));
          }
        },
        this);
  }
  void button(reader_button id, int value) override {
    if (value == 0)
      return;
    if ((id == READER_BACK || id == READER_FAVORITES) && value == 1) {
      if (options) {
        options = false;
        update();
      } else if (review) {
        review = false;
        live();
        update();
      } else if (busy)
        toast("Finishing photo before leaving…");
      else
        qApp->quit();
      return;
    }
    if (id == READER_OPTIONS && value == 1) {
      options = !options;
      update();
      return;
    }
    if (id == READER_GUIDE && value == 1) {
      toast("OK / Shoot takes photo · Options changes flash and mode");
      return;
    }
    if (options) {
      if (id == READER_UP || id == READER_DOWN) {
        option = qBound(0, option + (id == READER_DOWN ? 1 : -1), 2);
        update();
      } else if (value == 1 &&
                 (id == READER_OK || id == READER_LEFT || id == READER_RIGHT)) {
        if (option == 0)
          flash = !flash;
        if (option == 1)
          document = !document;
        if (option == 2)
          rotation = rotation ? 0 : 180;
        saveSettings();
        update();
      }
      return;
    }
    if (id == READER_ZOOM_IN || id == READER_ZOOM_OUT) {
      int next = qBound(1, zoom + (id == READER_ZOOM_IN ? 1 : -1), 3);
      if (next == zoom)
        return;
      zoom = next;
      toast("View zoom " + QString::number(zoom) +
            "× · photo keeps full frame");
      update();
      return;
    }
    if (id == READER_VIEW_TOGGLE && value == 1 && !busy) {
      if (review) {
        review = false;
        live();
      } else {
        QImage image("/var/lib/reader/camera/preview.jpg");
        if (!image.isNull()) {
          frame = image;
          review = true;
          subtitle = "Last photo";
        } else
          toast("No photos yet");
      }
      update();
      return;
    }
    if (value == 1 && (id == READER_OK || id == READER_SHOOT))
      capture();
  }
  void draw(QPainter &p, const QRect &r) override {
    if (options) {
      list(p, r, {"Flash", "Photo type", "Orientation"},
           {flash ? "On" : "Off", document ? "Document" : "Photo",
            rotation ? "Upright when bottom camera points forward"
                     : "Sensor orientation"},
           option);
      return;
    }
    if (frame.isNull()) {
      text(p, r, "Starting camera…\nAim at your subject. OK takes a photo.");
      return;
    }
    QSize size = frame.size().scaled(r.size(), Qt::KeepAspectRatio);
    QRect target(QPoint(r.center().x() - size.width() / 2,
                        r.center().y() - size.height() / 2),
                 size);
    p.save();
    p.setClipRect(r);
    p.translate(target.center());
    if (!review && rotation)
      p.rotate(rotation);
    QRect source = frame.rect();
    if (zoom > 1)
      source = QRect(QPoint((frame.width() - frame.width() / zoom) / 2,
                            (frame.height() - frame.height() / zoom) / 2),
                     QSize(frame.width() / zoom, frame.height() / zoom));
    p.drawImage(QRect(-target.width() / 2, -target.height() / 2, target.width(),
                      target.height()),
                frame, source);
    p.restore();
    if (busy) {
      p.fillRect(QRect(r.left() + 12, r.top() + 12, 380, 42),
                 QColor(16, 24, 32, 220));
      text(p, QRect(r.left() + 24, r.top() + 12, 355, 42),
           "Focusing and saving…", 24);
    }
  }

public:
  Camera() : Screen("Camera") {
    auto settings =
        readJson(qEnvironmentVariable("READER_APP_DATA_DIR",
                                      "/var/lib/reader/apps/reread-camera") +
                 "/settings.json");
    flash = settings.value("flash").toBool();
    document = settings.value("document").toBool();
    rotation = settings.value("rotation").toInt(180);
    hint = "OK Shoot   Options Settings   View Last photo   Back Home";
    subtitle = "Starting camera…";
    connect(&capturePoll, &QTimer::timeout, this, [this] {
      capturePoll.stop();
      cameraRequest(
          "/status", {},
          [this](int code, QByteArray data, QMap<QByteArray, QByteArray>) {
            auto state = QJsonDocument::fromJson(data).object();
            auto result = state.value("job").toObject();
            if (code != 200 || result.value("id").toString() != job) {
              busy = false;
              toast("Capture status unavailable. Check Files.");
              return;
            }
            if (result.value("state") == "running") {
              capturePoll.start(400);
              return;
            }
            busy = false;
            if (result.value("state") == "complete")
              toast("Saved · " + result.value("photo")
                                     .toObject()
                                     .value("filename")
                                     .toString());
            else
              toast(result.value("error").toString("Photo failed"));
            update();
          },
          this);
    });
    QTimer::singleShot(100, this, [this] { live(); });
  }
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setFont(QFont("DejaVu Sans"));
  Camera window;
  window.showFullScreen();
  return app.exec();
}

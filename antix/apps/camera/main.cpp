#include <gio/gio.h>
#include "reader-ui.hpp"
#include <QtNetwork>
#include <memory>
using namespace ReRead;
namespace {
void cameraRequest(
    const QString &endpoint, const QJsonObject &body,
    std::function<void(int, QByteArray, QMap<QByteArray, QByteArray>)> done,
    QObject *owner) {
  auto socket = new QLocalSocket(owner);
  auto data = std::make_shared<QByteArray>();
  auto timer = new QTimer(socket);
  timer->setSingleShot(true);
  auto finished = std::make_shared<bool>(false);
  auto finish = [socket, data, timer, done,
                 finished](int code, QByteArray bytes,
                           QMap<QByteArray, QByteArray> headers) {
    if (*finished)
      return;
    *finished = true;
    timer->stop();
    done(code, bytes, headers);
    socket->deleteLater();
  };
  QObject::connect(timer, &QTimer::timeout, socket,
                   [finish] { finish(504, "Camera request timed out", {}); });
  QObject::connect(
      socket, &QLocalSocket::connected, socket, [socket, endpoint, body] {
        bool get = endpoint == "/status" || endpoint.startsWith("/live?") ||
                   endpoint.startsWith("/rgb?");
        QByteArray bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        socket->write(
            (get ? "GET " : "POST ") + endpoint.toUtf8() +
            " HTTP/1.0\r\nContent-Type: application/json\r\nContent-Length: " +
            QByteArray::number(get ? 0 : bytes.size()) + "\r\n\r\n" +
            (get ? QByteArray() : bytes));
      });
  QObject::connect(socket, &QLocalSocket::readyRead, socket,
                   [socket, data, finish] {
                     *data += socket->readAll();
                     int end = data->indexOf("\r\n\r\n");
                     if (end < 0)
                       return;
                     auto lines = data->left(end).split('\n');
                     QMap<QByteArray, QByteArray> headers;
                     for (auto line : lines) {
                       int colon = line.indexOf(':');
                       if (colon > 0)
                         headers[line.left(colon).toLower().trimmed()] =
                             line.mid(colon + 1).trimmed();
                     }
                     int length = headers.value("content-length").toInt();
                     if (data->size() < end + 4 + length)
                       return;
                     finish(lines.first().split(' ').value(1).toInt(),
                            data->mid(end + 4, length), headers);
                   });
  QObject::connect(socket,
                   QOverload<QLocalSocket::LocalSocketError>::of(
                       &QLocalSocket::errorOccurred),
                   socket, [finish](QLocalSocket::LocalSocketError e) {
                     if (e != QLocalSocket::PeerClosedError)
                       finish(503, "Camera service unavailable", {});
                   });
  timer->start(30000);
  socket->connectToServer("/run/reader-camera.sock");
}
} // namespace

class Camera : public Screen {
  QImage frame;
  QTimer capturePoll;
  quint64 sequence = 0;
  bool idlePaused = false, livePending = false, busy = false, flash = false,
       document = false, options = false;
  int option = 0, zoom = 1, rotation = 180;
  QString job, cameraError;
  void chrome() {
    orientation = rotation;
    showHeader = false;
    controlsOverlay = true;
    if (options)
      controls = {{READER_OK,"Change"},{READER_BACK,"Camera"}};
    else
      controls = {{READER_OK,"Shoot"},{READER_OPTIONS,"Settings"},
                  {READER_VIEW_TOGGLE,"Last photo"},{READER_BACK,"Home"}};
  }
  void lastPhoto() {
    auto latest = readJson("/var/lib/reader/camera/latest.json");
    QString name = latest.value("filename").toString();
    if (name.isEmpty() || QDir::isAbsolutePath(name) || name.split('/').contains("..")) {
      toast("No photos yet");
      return;
    }
    QString filename=qEnvironmentVariable("REREAD_FILES_DIR","/var/lib/reader/files")+"/"+name;
    if(!QFileInfo(filename).isFile()) {toast("Last photo is no longer available. Check Files.");return;}
    auto context=g_app_launch_context_new();
    g_app_launch_context_unsetenv(context,"READER_APP_ID");g_app_launch_context_unsetenv(context,"READER_APP_DATA_DIR");
    GError *error=nullptr;auto uri=QUrl::fromLocalFile(filename).toEncoded();
    bool opened=g_app_info_launch_default_for_uri(uri.constData(),context,&error);g_object_unref(context);
    if(!opened)toast(error?QString::fromUtf8(error->message):"Unable to open last photo.");
    if(error)g_error_free(error);
  }
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
    if (options || idlePaused || livePending)
      return;
    livePending = true;
    cameraRequest(
        "/rgb?after=" + QString::number(sequence), {},
        [this](int code, QByteArray data,
               QMap<QByteArray, QByteArray> headers) {
          livePending = false;
          if (options || idlePaused)
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
              cameraError.clear();
              update();
            }
          } else if (!busy) {
            cameraError = "Camera unavailable · retrying";
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
    zoom=1;chrome();live();
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
      if (id == READER_FAVORITES) {
        if (busy) toast("Finishing photo before leaving…");
        else qApp->quit();
        return;
      }
      if (options) {
        options = false;
        live();
        chrome();
        update();
      } else if (busy)
        toast("Finishing photo before leaving…");
      else
        qApp->quit();
      return;
    }
    if (id == READER_OPTIONS && value == 1) {
      options = !options;
      if (!options) live();
      chrome();
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
        chrome();
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
      lastPhoto();
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
    const QImage &image = frame;
    if (image.isNull()) {
      text(p, r, cameraError.isEmpty() ? "Starting camera…" : cameraError);
      return;
    }
    QSize size = image.size().scaled(r.size(), Qt::KeepAspectRatio);
    QRect target(QPoint(r.center().x() - size.width() / 2,
                        r.center().y() - size.height() / 2),
                 size);
    p.save();
    p.setClipRect(r);
    p.translate(target.center());
    if (rotation)
      p.rotate(rotation);
    QRect source = image.rect();
    if (zoom > 1)
      source = QRect(QPoint((image.width() - image.width() / zoom) / 2,
                            (image.height() - image.height() / zoom) / 2),
                     QSize(image.width() / zoom, image.height() / zoom));
    p.drawImage(QRect(-target.width() / 2, -target.height() / 2, target.width(),
                      target.height()),
                image, source);
    p.restore();
    QString status = QString(document ? "Document" : "Photo") +
        " · " + (flash ? "Flash on" : "Flash off");
    p.fillRect(QRect(r.left(),r.top(),320,36),palette().base().color());
    text(p,QRect(r.left()+10,r.top(),305,36),status,21,palette().color(QPalette::Text));
    if (!cameraError.isEmpty())
      text(p,r,cameraError,23);
    if (busy) {
      p.fillRect(QRect(r.left() + 12, r.top() + 12, 380, 42),
                 palette().base().color());
      text(p, QRect(r.left() + 24, r.top() + 12, 355, 42),
           "Focusing and saving…", 24, palette().color(QPalette::Text));
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
    chrome();
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
  window.show();
  return app.exec();
}

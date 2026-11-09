// A regular Qt Widgets program. It uses optional device helpers, not Screen,
// ReRead drawing functions, a theme, a plugin interface or an application server.
#include "reader-device.hpp"
#include <QtWidgets>
int main(int argc, char **argv) {
  qputenv("QT_QPA_FB_DISABLE_INPUT", "1"); // Device owns evdev in this example.
  QApplication app(argc, argv);
  QWidget window;
  window.setWindowTitle("Independent app");
  window.setCursor(Qt::BlankCursor);
  window.setStyleSheet("QWidget { background: #f2f4f6; color: #192a34; font-size: 32px; }");
  QVBoxLayout layout(&window);
  layout.setContentsMargins(14, 14, 14, 14);
  QLabel title("An ordinary Qt window"), count("Select presses: 0"), status("Back returns to Home");
  layout.addWidget(&title);
  layout.addWidget(&count);
  layout.addWidget(&status);
  ReRead::Device device;
  int presses = 0;
  device.onButton([&](reader_button id, int value) {
    if (value != 1) return;
    if (id == READER_OK) count.setText("Select presses: " + QString::number(++presses));
    if (id == READER_BACK || id == READER_FAVORITES) app.quit();
  });
  device.onNotice([&](const QString &message) { status.setText(message); });
  device.onIdle([&](bool dimmed) { status.setText(dimmed ? "Idle" : "Back returns to Home"); });
  device.onHold([&](bool locked) { status.setText(locked ? "Hold locked" : "Back returns to Home"); });
  device.onResume([&] { window.update(); });
  window.showFullScreen();
  device.setActive(true);
  int result = app.exec();
  device.setActive(false);
  return result;
}

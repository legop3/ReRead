// A regular Qt Widgets program. It uses optional device helpers, not Screen,
// ReRead drawing functions, a theme, a plugin interface or an application server.
#include "reader-device.hpp"
#include <QtWidgets>
int main(int argc, char **argv) {
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
  // Ordinary Qt keyboard shortcuts, independent of ReRead input adapters.
  QObject::connect(new QShortcut(QKeySequence(Qt::Key_Return),&window),&QShortcut::activated,
                   [&]{count.setText("Select presses: "+QString::number(++presses));});
  QObject::connect(new QShortcut(QKeySequence(Qt::Key_Escape),&window),&QShortcut::activated,
                   &app,&QApplication::quit);
  QObject::connect(new QShortcut(QKeySequence(Qt::Key_Favorites),&window),&QShortcut::activated,
                   &app,&QApplication::quit);
  device.onNotice([&](const QString &message) { status.setText(message); });
  device.onIdle([&](bool dimmed) { status.setText(dimmed ? "Idle" : "Back returns to Home"); });
  device.onHold([&](bool locked) { status.setText(locked ? "Hold locked" : "Back returns to Home"); });
  device.onResume([&] { window.update(); });
  QObject::connect(&app,&QGuiApplication::applicationStateChanged,&device,
                   [&](Qt::ApplicationState state) {
    device.setActive(window.isVisible() && state==Qt::ApplicationActive);
  });
  window.resize(960,544);
  window.show();
  device.setActive(true);
  int result = app.exec();
  device.setActive(false);
  return result;
}

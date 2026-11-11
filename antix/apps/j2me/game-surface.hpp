#pragma once
#include <QWidget>
#include <QWindow>
#include <QTimer>
#include <QPointer>
#include <QString>
#include <QByteArray>
#include <QGuiApplication>
#include <X11/Xlib.h>

// ReRead runs on X11. Embed FreeJ2ME's native AWT X window into a native
// QWidget; Java continues drawing directly (no frame copying or streaming).
class JavaGameSurface final : public QWidget {
  Display *display=nullptr;
  Window guest=0;
  QPointer<QWidget> container;
  QPointer<QWindow> foreign;
  QTimer probe;
  QString token;
  int attempts=0;

  Window findWindow(Window root, int depth) {
    if(depth>8) return 0;
    char *title=nullptr;
    if(XFetchName(display,root,&title) && title) {
      const bool found=QString::fromLocal8Bit(title)==token;
      XFree(title);
      if(found)return root;
    }
    Window parent=0, *children=nullptr;
    unsigned int count=0;
    Window result=0;
    if(XQueryTree(display,root,&root,&parent,&children,&count)) {
      for(unsigned int i=0;i<count && !result;i++)
        result=findWindow(children[i],depth+1);
      if(children)XFree(children);
    }
    return result;
  }
  void tryAttach() {
    if(guest || !display || token.isEmpty())return;
    ++attempts;
    const Window found=findWindow(DefaultRootWindow(display),0);
    if(!found){
      if(attempts>=200){probe.stop();emit attachFailed("FreeJ2ME did not create an embedded game window.");}
      return;
    }
    probe.stop();
    guest=found;
    QWindow *window=QWindow::fromWinId(static_cast<WId>(found));
    if(!window){guest=0;emit attachFailed("X11 could not embed FreeJ2ME's game window.");return;}
    foreign=window;
    QWidget *view=QWidget::createWindowContainer(window,this,Qt::FramelessWindowHint);
    container=view;
    view->setFocusPolicy(Qt::StrongFocus);
    view->setGeometry(rect());
    view->show();
    view->raise();
    focusGame();
    emit attached();
  }
public:
  explicit JavaGameSurface(QWidget *parent=nullptr):QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::StrongFocus);
    display=XOpenDisplay(nullptr);
    probe.setInterval(75);
    connect(&probe,&QTimer::timeout,this,[this]{tryAttach();});
  }
  ~JavaGameSurface() override {
    probe.stop();
    // Qt owns the foreign wrapper, never the actual Java window.
    if(display)XCloseDisplay(display);
  }
  void begin(const QString &windowToken) {
    reset();
    token=windowToken;
    attempts=0;
    setGeometry(parentWidget()->rect());
    show();
    raise();
    if(!display)emit attachFailed("X11 display unavailable; Java window embedding requires X11.");
    else probe.start();
  }
  void focusGame() {
    if(!guest || !display)return;
    if(container){container->setFocus(Qt::OtherFocusReason);container->raise();}
    XSetInputFocus(display,guest,RevertToPointerRoot,CurrentTime);
    XFlush(display);
  }
  void resume() {
    show();raise();focusGame();
  }
  void reset() {
    probe.stop();
    token.clear();
    guest=0;
    if(container) {delete container.data();container=nullptr;}
    foreign=nullptr;
    hide();
  }
signals:
  void attached();
  void attachFailed(const QString &error);
protected:
  void resizeEvent(QResizeEvent *event) override {
    QWidget::resizeEvent(event);
    if(container)container->setGeometry(rect());
  }
};

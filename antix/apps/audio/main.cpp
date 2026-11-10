#include <gst/gst.h>
#include "reader-ui.hpp"
#include <QtDBus>
using namespace ReRead;

class Audio : public Screen {
  Q_OBJECT
  friend struct AudioTest;
  GstElement *player=nullptr;
  GstBus *bus=nullptr;
  QTimer tick;
  QDBusUnixFileDescriptor idleInhibitor;
  bool wanted=false, buffering=false, ended=false;
  QString source, track, artist, error;
  qint64 position=0,duration=0;
  static QString time(qint64 ns) {
    auto seconds=qMax(qint64(0),ns/GST_SECOND);
    return QString("%1:%2").arg(seconds/60).arg(seconds%60,2,10,QChar('0'));
  }
  void inhibit() {
    QDBusInterface manager("org.freedesktop.login1","/org/freedesktop/login1",
      "org.freedesktop.login1.Manager",QDBusConnection::systemBus());
    auto pending=new QDBusPendingCallWatcher(manager.asyncCall("Inhibit","idle","ReRead Audio","Playing audio","block"),this);
    connect(pending,&QDBusPendingCallWatcher::finished,this,[this,pending] {
      QDBusPendingReply<QDBusUnixFileDescriptor> reply=*pending;
      if(wanted && !reply.isError())idleInhibitor=reply.value();
      pending->deleteLater();
    });
  }
  void playback(bool play) {
    if(!player || !error.isEmpty())return;
    if(play && ended){gst_element_set_state(player,GST_STATE_NULL);position=0;ended=false;}
    const bool starting=play&&!wanted;wanted=play;
    if(starting)inhibit();
    if(!play)idleInhibitor=QDBusUnixFileDescriptor();
    gst_element_set_state(player,wanted&&!buffering?GST_STATE_PLAYING:GST_STATE_PAUSED);
    if(wanted)tick.start(1000);else tick.stop();update();
  }
  void readPosition() {
    gint64 value=0;
    if(gst_element_query_position(player,GST_FORMAT_TIME,&value))position=value;
    if(gst_element_query_duration(player,GST_FORMAT_TIME,&value))duration=value;
    update();
  }
  void messages() {
    while(auto message=gst_bus_pop(bus)) {
      switch(GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
          GError *failure=nullptr;gchar *debug=nullptr;gst_message_parse_error(message,&failure,&debug);
          playback(false);gst_element_set_state(player,GST_STATE_NULL);
          error=failure?QString::fromUtf8(failure->message):"Cannot play this audio.";
          g_clear_error(&failure);g_free(debug);update();break;
        }
        case GST_MESSAGE_EOS:
          readPosition();playback(false);gst_element_set_state(player,GST_STATE_READY);ended=true;update();break;
        case GST_MESSAGE_BUFFERING: {
          gint percent=0;gst_message_parse_buffering(message,&percent);buffering=percent<100;
          gst_element_set_state(player,wanted&&!buffering?GST_STATE_PLAYING:GST_STATE_PAUSED);update();break;
        }
        case GST_MESSAGE_TAG: {
          GstTagList *tags=nullptr;gst_message_parse_tag(message,&tags);gchar *value=nullptr;
          if(gst_tag_list_get_string(tags,GST_TAG_TITLE,&value)){track=QString::fromUtf8(value);g_free(value);}
          if(gst_tag_list_get_string(tags,GST_TAG_ARTIST,&value)){artist=QString::fromUtf8(value);g_free(value);}
          gst_tag_list_unref(tags);update();break;
        }
        case GST_MESSAGE_ASYNC_DONE:readPosition();break;
        default:break;
      }
      gst_message_unref(message);
    }
  }
  void seek(int seconds) {
    if(!player || !error.isEmpty())return;
    readPosition();qint64 target=qMax(qint64(0),position+qint64(seconds)*GST_SECOND);
    if(duration>0)target=qMin(target,qMax(qint64(0),duration-GST_SECOND));
    if(!gst_element_seek_simple(player,GST_FORMAT_TIME,GstSeekFlags(GST_SEEK_FLAG_FLUSH|GST_SEEK_FLAG_KEY_UNIT),target))
      toast("This source does not support seeking.");
  }
private slots:
  void sleeping(bool active){if(active)playback(false);}
protected:
  void button(reader_button id,int value) override {
    if(!value)return;
    if(value==2)return;
    if(id==READER_BACK || id==READER_FAVORITES){close();return;}
    if(id==READER_PLAY || id==READER_OK)playback(!wanted);
    else if(id==READER_LEFT)seek(-10);
    else if(id==READER_RIGHT)seek(10);
  }
  void closeEvent(QCloseEvent *event) override {playback(false);if(player)gst_element_set_state(player,GST_STATE_NULL);Screen::closeEvent(event);}
  void draw(QPainter &p,const QRect &r) override {
    controls=error.isEmpty()?QList<Control>{{READER_PLAY,"Play / Pause"},{READER_LEFT,"Seek ±10s"},{READER_BACK,"Close"}}:
      QList<Control>{{READER_BACK,"Close"}};
    label(p,QRect(r.x(),r.y()+10,r.width(),48),track,30,true,palette().color(QPalette::Link));
    label(p,QRect(r.x(),r.y()+65,r.width(),35),artist,23);
    if(!error.isEmpty()){text(p,r.adjusted(0,115,0,0),error,25);return;}
    const QString state=ended?"Finished":buffering&&wanted?"Buffering…":wanted?"Playing":"Paused";
    label(p,QRect(r.x(),r.y()+130,r.width(),45),state,29,true);
    QRect progress(r.x(),r.y()+205,r.width(),14);p.fillRect(progress,Qt::darkGray);
    if(duration>0)p.fillRect(QRect(progress.x(),progress.y(),qRound(progress.width()*qBound(0.0,double(position)/duration,1.0)),14),palette().link());
    label(p,QRect(r.x(),r.y()+232,r.width(),40),time(position)+" / "+(duration>0?time(duration):"--:--"),26);
    const reader_button buttons[]={READER_LEFT,READER_PLAY,READER_RIGHT};
    const QString captions[]={"Back 10s",wanted?"Pause":"Play","Ahead 10s"};
    for(int i=0;i<3;++i) {
      int x=r.x()+i*r.width()/3;buttonIcon(p,QRect(x+10,r.y()+300,38,38),buttons[i]);
      label(p,QRect(x+60,r.y()+300,r.width()/3-65,38),captions[i],23);
    }
  }
public:
  explicit Audio(const QString &argument,GstElement *output=nullptr):Screen("Audio") {
    showHeader=false;
    controls={{READER_PLAY,"Play / Pause"},{READER_LEFT,"Seek"},{READER_BACK,"Close"}};
    QUrl uri(argument);
    if(QFileInfo::exists(argument) || uri.scheme().isEmpty())uri=QUrl::fromLocalFile(QFileInfo(argument).absoluteFilePath());
    source=QString::fromUtf8(uri.toEncoded());track=QFileInfo(uri.path()).fileName();setWindowTitle("Audio — "+track);
    if(argument.isEmpty()){error="Open audio from Files or another application.";return;}
    if(!uri.isLocalFile() && uri.scheme()!="https" && uri.scheme()!="http"){error="Unsupported audio address.";return;}
    player=gst_element_factory_make("playbin",nullptr);
    auto sink=output?output:gst_element_factory_make("alsasink",nullptr);
    if(!player || !sink){if(sink)gst_object_unref(sink);error="GStreamer playback or ALSA plugin is unavailable.";return;}
    gst_object_ref_sink(sink);g_object_set(player,"uri",source.toUtf8().constData(),"audio-sink",sink,"flags",2,nullptr);gst_object_unref(sink);
    bus=gst_element_get_bus(player);GPollFD poll;gst_bus_get_pollfd(bus,&poll);
    auto notifier=new QSocketNotifier(poll.fd,QSocketNotifier::Read,this);
    connect(notifier,&QSocketNotifier::activated,this,[this](int){messages();});
    connect(&tick,&QTimer::timeout,this,[this]{readPosition();});
    QDBusConnection::systemBus().connect("org.freedesktop.login1","/org/freedesktop/login1",
      "org.freedesktop.login1.Manager","PrepareForSleep",this,SLOT(sleeping(bool)));
    playback(true);
  }
  ~Audio() override {
    tick.stop();if(player){gst_element_set_state(player,GST_STATE_NULL);gst_object_unref(player);}
    if(bus)gst_object_unref(bus);
  }
};
int main(int argc,char **argv) {
  gst_init(&argc,&argv);QApplication app(argc,argv);app.setApplicationName("reread-audio");app.setDesktopFileName("reread-audio");
  app.setFont(QFont("DejaVu Sans"));QCommandLineParser parser;parser.setApplicationDescription("ReRead audio player");parser.addHelpOption();
  parser.addPositionalArgument("file","Local audio file or HTTP(S) URL","[file]");parser.process(app);
  if(parser.positionalArguments().size()>1)parser.showHelp(1);
  Audio window(parser.positionalArguments().value(0));window.show();return app.exec();
}
#include "audio.moc"

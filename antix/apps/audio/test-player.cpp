#define main reread_audio_main
#include "main.cpp"
#undef main
#include <stdexcept>
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
struct AudioTest {
  static void wait(const std::function<bool()> &condition,const char *message) {
    QElapsedTimer timer;timer.start();
    while(!condition() && timer.elapsed()<8000){QApplication::processEvents();QThread::msleep(10);}
    require(condition(),message);
  }
  static int run(int argc,char **argv) {
    gst_init(&argc,&argv);QApplication app(argc,argv);
    for(const auto &name:app.arguments().mid(1)) {
      auto output=gst_element_factory_make("fakesink",nullptr);require(output,"Create test sink");g_object_set(output,"sync",TRUE,nullptr);
      Audio window(name.startsWith("http:")?name:QString::fromUtf8(QUrl::fromLocalFile(name).toEncoded()),output);window.show();
      wait([&]{return !window.error.isEmpty() || window.position>0;},"Playback advances");
      require(window.error.isEmpty(),qPrintable(window.error));require(window.duration>10*GST_SECOND,"Duration reported");
      window.dispatch(READER_PLAY,1);window.dispatch(READER_PLAY,0);require(!window.wanted,"Pause button");
      gst_element_get_state(window.player,nullptr,nullptr,GST_SECOND);window.readPosition();qint64 paused=window.position;
      QThread::msleep(80);window.readPosition();require(window.position==paused,"Paused source does not advance");
      window.seek(10);wait([&]{window.readPosition();return window.position>=9*GST_SECOND;},"Seek moves actual media position");
      window.dispatch(READER_OK,1);window.dispatch(READER_OK,0);require(window.wanted,"OK resumes");
      window.sleeping(true);require(!window.wanted && !window.idleInhibitor.isValid(),"Sleep pauses and releases idle inhibitor");
      window.dispatch(READER_BACK,1);window.dispatch(READER_BACK,0);require(!window.isVisible(),"Back closes player");
      GstState state;gst_element_get_state(window.player,&state,nullptr,GST_SECOND);require(state==GST_STATE_NULL,"Close releases audio hardware");
    }
    if(app.arguments().size()>1 && !app.arguments()[1].startsWith("http:")) {
      auto fast=gst_element_factory_make("fakesink",nullptr);
      Audio completed(app.arguments()[1],fast);completed.show();
      wait([&]{return completed.ended;},"End of media");
      require(!completed.wanted && !completed.idleInhibitor.isValid(),"Completion releases idle inhibitor");
      GstState state;gst_element_get_state(completed.player,&state,nullptr,GST_SECOND);
      require(state==GST_STATE_READY,"Completion releases playback resources");
      completed.playback(true);require(completed.wanted && !completed.ended,"Finished track can restart");completed.close();
    }
    auto output=gst_element_factory_make("fakesink",nullptr);
    Audio missing("/does-not-exist.wav",output);missing.show();
    wait([&]{return !missing.error.isEmpty();},"Missing source reports error");
    require(!missing.wanted,"Failure releases playing state");missing.close();
    return 0;
  }
};
int main(int argc,char **argv){return AudioTest::run(argc,argv);}

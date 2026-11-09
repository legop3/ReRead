#pragma once
#include <QtCore>
#include <QtNetwork>
#include <QLibrary>
#include <atomic>
#include <functional>

// Only the tiny mpg123 decoder and the existing ALSA PCM interface are needed.
// Networking is performed by Qt in a worker thread; no audio data hits the SSD.
// Library symbols are loaded dynamically, avoiding development headers in the
// ReRead builder. Debian package dependencies guarantee their presence.
class ArchiveAudio final : public QThread {
  std::atomic_bool stopping{false}, paused{false};
  QUrl url;
  QObject *receiver;
  std::function<void(const QString&)> notification;
  qint64 beginByte;
  void report(const QString &s) {
    QMetaObject::invokeMethod(receiver,[cb=notification,s] {cb(s);},Qt::QueuedConnection);
  }
public:
  ArchiveAudio(QUrl source, qint64 byteOffset, QObject *parent,
               std::function<void(const QString&)> callback)
    :QThread(parent),url(std::move(source)),receiver(parent),
     notification(std::move(callback)),beginByte(qMax(qint64(0),byteOffset)) {}
  void stop() {stopping.store(true);}
  void setPaused(bool yes) {paused.store(yes);}
protected:
  void run() override {
    QLibrary mpg("libmpg123.so.0"), alsa("libasound.so.2");
    if(!mpg.load()||!alsa.load()){report("MP3 decoder or ALSA library unavailable");return;}
    using Handle=void;
    using Pcm=void;
    auto init=reinterpret_cast<int(*)()>(mpg.resolve("mpg123_init"));
    auto finish=reinterpret_cast<void(*)()>(mpg.resolve("mpg123_exit"));
    auto create=reinterpret_cast<Handle*(*)(const char*,int*)>(mpg.resolve("mpg123_new"));
    auto destroy=reinterpret_cast<void(*)(Handle*)>(mpg.resolve("mpg123_delete"));
    auto openFeed=reinterpret_cast<int(*)(Handle*)>(mpg.resolve("mpg123_open_feed"));
    auto formatsOff=reinterpret_cast<int(*)(Handle*)>(mpg.resolve("mpg123_format_none"));
    auto format=reinterpret_cast<int(*)(Handle*,long,int,int)>(mpg.resolve("mpg123_format"));
    auto decode=reinterpret_cast<int(*)(Handle*,const unsigned char*,size_t,void*,size_t,size_t*)>(mpg.resolve("mpg123_decode"));
    auto getFormat=reinterpret_cast<int(*)(Handle*,long*,int*,int*)>(mpg.resolve("mpg123_getformat"));
    auto pcmOpen=reinterpret_cast<int(*)(Pcm**,const char*,int,int)>(alsa.resolve("snd_pcm_open"));
    auto pcmSet=reinterpret_cast<int(*)(Pcm*,int,int,unsigned int,unsigned int,int,unsigned int)>(alsa.resolve("snd_pcm_set_params"));
    auto pcmWrite=reinterpret_cast<long(*)(Pcm*,const void*,unsigned long)>(alsa.resolve("snd_pcm_writei"));
    auto pcmRecover=reinterpret_cast<int(*)(Pcm*,int,int)>(alsa.resolve("snd_pcm_recover"));
    auto pcmDrop=reinterpret_cast<int(*)(Pcm*)>(alsa.resolve("snd_pcm_drop"));
    auto pcmClose=reinterpret_cast<int(*)(Pcm*)>(alsa.resolve("snd_pcm_close"));
    if(!init||!finish||!create||!destroy||!openFeed||!formatsOff||!format||
       !decode||!getFormat||!pcmOpen||!pcmSet||!pcmWrite||!pcmRecover||
       !pcmDrop||!pcmClose){report("Missing audio library functions");return;}
    if(init()!=0){report("MP3 decoder initialization failed");return;}
    int err=0;
    Handle *decoder=create(nullptr,&err);
    if(!decoder){finish();report("Could not open MP3 decoder");return;}
    constexpr int signed16=0xD0;
    formatsOff(decoder);
    for(long rate:{8000L,11025L,12000L,16000L,22050L,24000L,32000L,44100L,48000L})
      format(decoder,rate,3,signed16);
    openFeed(decoder);
    Pcm *pcm=nullptr;
    int channels=0;
    bool failed=false, configured=false;
    auto audioOutput=[&](const char *bytes,size_t count)->bool {
      if(!pcm||channels<=0)return false;
      const unsigned long frameSize=unsigned(channels)*2;
      unsigned long frames=count/frameSize;
      const char *pos=bytes;
      while(frames && !stopping.load()) {
        long n=pcmWrite(pcm,pos,frames);
        if(n<0){int fixed=pcmRecover(pcm,int(n),1);if(fixed<0)return false;continue;}
        if(n==0)continue;
        frames-=unsigned long(n);pos+=size_t(n)*frameSize;
      }
      return true;
    };
    auto process=[&](const QByteArray &chunk)->bool {
      if(chunk.isEmpty())return true;
      unsigned char buffer[16384];
      int result=0;
      bool first=true;
      do {
        size_t got=0;
        result=decode(decoder,first?reinterpret_cast<const unsigned char*>(chunk.constData()):nullptr,
                      first?size_t(chunk.size()):0,buffer,sizeof(buffer),&got);
        first=false;
        if(result==-11) { // MPG123_NEW_FORMAT
          long rate=0;int encoding=0;
          if(getFormat(decoder,&rate,&channels,&encoding)!=0||
             encoding!=signed16||channels<1||channels>2||rate<8000||rate>48000)
            return false;
          if(pcm){pcmDrop(pcm);pcmClose(pcm);pcm=nullptr;}
          if(pcmOpen(&pcm,"default:CARD=MID",0,0)<0)return false;
          // ALSA S16_LE=2; RW_INTERLEAVED=3.
          if(pcmSet(pcm,2,3,unsigned(channels),unsigned(rate),1,250000)<0)return false;
          configured=true;
        }
        if(got && !audioOutput(reinterpret_cast<char*>(buffer),got))return false;
      }while((result==0||result==-11) && !stopping.load());
      return result!=-1;
    };
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent","ReRead-Archive/0.3");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    if(beginByte>0)request.setRawHeader("Range","bytes="+QByteArray::number(beginByte)+"-");
    QNetworkReply *reply=network.get(request);
    reply->setReadBufferSize(96*1024);
    QEventLoop loop;
    // Small bounded chunks. ALSA blocking occurs only in this worker thread.
    auto drain=[&] {
      if(paused.load()||stopping.load()||failed)return;
      while(reply->bytesAvailable()>0 && !paused.load() && !stopping.load() && !failed) {
        QByteArray chunk=reply->read(16384);
        if(!process(chunk)){failed=true;report("MP3 decoding or audio output failed");reply->abort();break;}
      }
    };
    QObject::connect(reply,&QNetworkReply::readyRead,reply,drain);
    QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
    QTimer poll;
    poll.setInterval(75);
    QObject::connect(&poll,&QTimer::timeout,&loop,[&] {
      if(stopping.load())reply->abort();
      else drain();
    });
    poll.start();
    loop.exec();
    drain();
    if(!stopping.load() && reply->error()!=QNetworkReply::NoError && !failed)
      report("Audio network request failed");
    else if(!configured && !stopping.load() && !failed)
      report("The selected MP3 has no decodable audio");
    reply->deleteLater();
    if(pcm){pcmDrop(pcm);pcmClose(pcm);}
    destroy(decoder);finish();
  }
};

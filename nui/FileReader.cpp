#include "FileReader.hpp"

#include <QtNetwork>

FileReader::FileReader(const QString& file) {
  timer.start();
  // TODO: Support reading files from the API
  startRequest(QUrl("http://data.comma.life/"+file));
}

void FileReader::startRequest(const QUrl &url) {
  reply = qnam.get(QNetworkRequest(url));
  connect(reply, &QNetworkReply::finished, this, &FileReader::httpFinished);
  connect(reply, &QIODevice::readyRead, this, &FileReader::readyRead);
  qDebug() << "requesting" << url;
}

void FileReader::httpFinished() {
  if (reply->error()) {
    qWarning() << reply->errorString();
  }

  const QVariant redirectionTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
  if (!redirectionTarget.isNull()) {
    const QUrl redirectedUrl = redirectionTarget.toUrl();
    //qDebug() << "redirected to" << redirectedUrl;
    startRequest(redirectedUrl);
  } else {
    qDebug() << "done in" << timer.elapsed() << "ms";
    done();

  }
}

void FileReader::readyRead() {
  QByteArray dat = reply->readAll();
  printf("got http ready read: %d\n", dat.size());
}

FileReader::~FileReader() {

}

LogReader::LogReader(const QString& file, Events *events_) : FileReader(file), events(events_) {
  bStream.next_in = NULL;
  bStream.avail_in = 0;
  bStream.bzalloc = NULL;
  bStream.bzfree = NULL;
  bStream.opaque = NULL;

  int ret = BZ2_bzDecompressInit(&bStream, 0, 0);
  if (ret != BZ_OK) qWarning() << "bz2 init failed";

  // start with 64MB buffer
  raw.resize(1024*1024*64);

  // auto increment?
  bStream.next_out = raw.data();
  bStream.avail_out = raw.size();

  // parsed no events yet
  event_offset = 0;
}

void LogReader::readyRead() {
  QByteArray dat = reply->readAll();

  bStream.next_in = dat.data();
  bStream.avail_in = dat.size();

  while (bStream.avail_in > 0) {
    int ret = BZ2_bzDecompress(&bStream);
    if (ret != BZ_OK && ret != BZ_STREAM_END) qWarning() << "bz2 decompress failed";

    qDebug() << "got" << dat.size() << "with" << bStream.avail_out << "size" << raw.size();

    // support growth
    // TODO: this will break underlying pointers, need to fix
    /*size_t old_size = raw.size();
    if (old_size/2 > bStream.avail_out) {
      qDebug() << "resizing";
      raw.resize(old_size*2);
      bStream.next_out = raw.data() + old_size - bStream.avail_out;
      bStream.avail_out += old_size;
    }*/
  }

  int dled = raw.size() - bStream.avail_out;
  auto amsg = kj::arrayPtr((const capnp::word*)(raw.data() + event_offset), (dled-event_offset)/sizeof(capnp::word));
  Events events_local;

  while (amsg.size() > 0) {
    try {
      capnp::FlatArrayMessageReader cmsg = capnp::FlatArrayMessageReader(amsg);

      // this needed? it is
      capnp::FlatArrayMessageReader *tmsg =
        new capnp::FlatArrayMessageReader(kj::arrayPtr(amsg.begin(), cmsg.getEnd()));

      amsg = kj::arrayPtr(cmsg.getEnd(), amsg.end());

      cereal::Event::Reader event = tmsg->getRoot<cereal::Event>();
      events_local.insert(event.getLogMonoTime(), event);

      // increment
      event_offset = (char*)cmsg.getEnd() - raw.data();
    } catch (const kj::Exception& e) {
      qDebug() << e.getDescription().cStr();
      break;
    }
  }

  // merge in events
  // TODO: add lock
  *events += events_local;

  printf("parsed %d into %d events with offset %d\n", dled, events->size(), event_offset);
}

void LogReader::done() {
  //uint64_t t0 = events->begin().key();
  //uint64_t t1 = (events->end()-1).key();
  /*printf("paint event: %lu %lu e %lu\n", t0, t1, t1-t0);*/

  /*uint64_t t = events->begin().value().getLogMonoTime();
  printf("%lld\n", t);*/

  /*for (cereal::Event::Reader e : *events) {
    auto type = e.which();
    //printf("%lld %d\n", e.getLogMonoTime()-t0, type);
    if (type == cereal::Event::CONTROLS_STATE) {
      auto controlsState = e.getControlsState();
      float vEgo = controlsState.getVEgo();
      printf("%lld : %f\n", e.getLogMonoTime()-t0, vEgo);
    }
  }*/
}

#include "music/dm_loader.h"

#include "core/log.h"
#include "core/strutil.h"
#include "music/synth.h"

namespace cc {
namespace music {

BytesPtr MusicLibrary::readMusicFile(const std::string& file) {
    if (!read_) return nullptr;
    return read_("aud/dmusic/" + file);
}

SegmentPtr MusicLibrary::loadSegment(const std::string& file) {
    BytesPtr b = readMusicFile(file);
    if (!b) {
        logFile(LogLevel::Warn, "aud/dmusic/" + file, "music segment missing");
        return nullptr;
    }
    auto seg = std::make_shared<Segment>();
    std::string err;
    if (!parseSegment(*b, *seg, err)) {
        logFile(LogLevel::Warn, "aud/dmusic/" + file, "broken music segment: " + err);
        return nullptr;
    }
    seg->fileName = file;
    if (seg->name.empty()) seg->name = stem(file);
    for (StyleRefEvent& ev : seg->styles) ev.style = resolveStyle(ev.ref);
    return seg;
}

StylePtr MusicLibrary::loadStyle(const std::string& file) {
    for (const StylePtr& s : styles_)
        if (iequals(s->fileName, file)) return s;
    for (const std::string& f : failed_)
        if (iequals(f, file)) return nullptr;
    BytesPtr b = readMusicFile(file);
    auto st = std::make_shared<Style>();
    std::string err;
    if (!b || !parseStyle(*b, *st, err)) {
        logFile(LogLevel::Warn, "aud/dmusic/" + file, b ? "broken music style: " + err : "music style missing");
        failed_.push_back(file);
        return nullptr;
    }
    st->fileName = file;
    styles_.push_back(st);
    return st;
}

StylePtr MusicLibrary::resolveStyle(const DmReference& ref) {
    if (ref.hasGuid)
        for (const StylePtr& s : styles_)
            if (s->guid == ref.guid) return s;
    if (ref.file.empty()) return nullptr;
    return loadStyle(ref.file);
}

DlsPtr MusicLibrary::loadCollection(const std::string& file) {
    for (const DlsPtr& d : dls_)
        if (iequals(d->fileName, file)) return d;
    for (const std::string& f : failed_)
        if (iequals(f, file)) return nullptr;
    BytesPtr b = readMusicFile(file);
    auto col = std::make_shared<DlsCollection>();
    std::string err;
    if (!b || !parseDls(*b, *col, err)) {
        logFile(LogLevel::Warn, "aud/dmusic/" + file, b ? "broken DLS collection: " + err : "DLS collection missing");
        failed_.push_back(file);
        return nullptr;
    }
    col->fileName = file;
    dls_.push_back(col);
    return col;
}

DlsPtr MusicLibrary::resolveCollection(const DmReference& ref) {
    if (ref.hasGuid)
        for (const DlsPtr& d : dls_)
            if (d->hasGuid && d->guid == ref.guid) return d;
    if (ref.file.empty()) return nullptr;
    return loadCollection(ref.file);
}

BandPtr MusicLibrary::loadBand(const std::string& file) {
    BytesPtr b = readMusicFile(file);
    if (!b) {
        logFile(LogLevel::Warn, "aud/dmusic/" + file, "music band missing");
        return nullptr;
    }
    auto band = std::make_shared<Band>();
    std::string err;
    if (!parseBandFile(*b, *band, err)) {
        logFile(LogLevel::Warn, "aud/dmusic/" + file, "broken music band: " + err);
        return nullptr;
    }
    return band;
}

void MusicLibrary::downloadBand(const Band& band, DlsSynth& synth) {
    for (const BandInstrument& bi : band.instruments) {
        if (bi.hasRef) {
            DlsPtr col = resolveCollection(bi.ref);
            if (!col) continue;
            if (const DlsInstrument* in = col->find(bi.patch)) synth.download(in, col.get(), false);
            continue;
        }
        if (!(bi.flags & (BandGM | BandGS | BandXG))) continue;
        ++gmRequests_;
        const DlsInstrument* in = gm_ ? gm_->find(bi.patch) : nullptr;
        if (!in && gm_) in = gm_->find(bi.patch & 0x7F);
        if (in) synth.download(in, gm_.get(), true);
        else ++gmMissing_;
    }
}

void MusicLibrary::downloadSegment(const Segment& seg, DlsSynth& synth) {
    for (const BandEvent& be : seg.bands)
        if (be.band) downloadBand(*be.band, synth);
}

}  // namespace music
}  // namespace cc

// Loading MM2's music files from the archives (spec/formats/directmusic.md, "References",
// "Downloading"): segments, styles, bands and DLS collections from aud/dmusic/, references
// resolved by GUID, then by file name, then by loading the file; styles and collections stay
// loaded for the life of the library (one music player) and are shared.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/bytes.h"
#include "music/dm_files.h"

namespace cc {
namespace music {

class DlsSynth;

// Reads a file from the game archives by path (case-insensitive); null if missing.
using FileReader = std::function<BytesPtr(const std::string& path)>;

class MusicLibrary {
public:
    explicit MusicLibrary(FileReader reader) : read_(std::move(reader)) {}

    // aud/dmusic/<file>. A missing or broken file gives null (and a logged problem).
    SegmentPtr loadSegment(const std::string& file);
    StylePtr loadStyle(const std::string& file);
    BandPtr loadBand(const std::string& file);

    StylePtr resolveStyle(const DmReference& ref);
    DlsPtr resolveCollection(const DmReference& ref);

    // The General MIDI set (the player's gm.dls, or a bundled bank); null = none.
    void setGmBank(DlsPtr gm) { gm_ = std::move(gm); }
    const DlsPtr& gmBank() const { return gm_; }

    // Downloads every instrument the band names ("Downloading").
    void downloadBand(const Band& band, DlsSynth& synth);
    // Every band of the segment's band track.
    void downloadSegment(const Segment& seg, DlsSynth& synth);

    const std::vector<StylePtr>& styles() const { return styles_; }
    const std::vector<DlsPtr>& collections() const { return dls_; }
    int gmRequests() const { return gmRequests_; }
    int gmMissing() const { return gmMissing_; }

private:
    BytesPtr readMusicFile(const std::string& file);
    DlsPtr loadCollection(const std::string& file);

    FileReader read_;
    std::vector<StylePtr> styles_;
    std::vector<DlsPtr> dls_;
    std::vector<std::string> failed_;  // file names that did not load (not retried)
    DlsPtr gm_;
    int gmRequests_ = 0, gmMissing_ = 0;
};

}  // namespace music
}  // namespace cc

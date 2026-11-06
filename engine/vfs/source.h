// Random-access byte sources that archives are read from. Archives stay where they are (a file
// on disk natively, a JavaScript ArrayBuffer in the browser); only the ranges asked for are
// copied.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cc {

class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual uint64_t size() const = 0;
    // Copies len bytes at offset into dst. False if the range is outside the source or the read
    // failed.
    virtual bool read(uint64_t offset, void* dst, size_t len) = 0;

    // Lazily read sources (the browser's audio archives): only the ranges fetched so far can be
    // read. has() tells whether a range is there, fetch() asks for it (it arrives later), forget()
    // lets its bytes go. Every other source has everything at once.
    virtual bool lazy() const { return false; }
    virtual bool has(uint64_t offset, size_t len) {
        (void)offset;
        (void)len;
        return true;
    }
    virtual void fetch(uint64_t offset, size_t len) {
        (void)offset;
        (void)len;
    }
    virtual void forget(uint64_t offset, size_t len) {
        (void)offset;
        (void)len;
    }
};

class MemorySource : public ByteSource {
public:
    explicit MemorySource(std::vector<uint8_t> data) : data_(std::move(data)) {}
    uint64_t size() const override { return data_.size(); }
    bool read(uint64_t offset, void* dst, size_t len) override;

private:
    std::vector<uint8_t> data_;
};

// Native only: reads a file on disk.
std::shared_ptr<ByteSource> openFileSource(const std::string& path, std::string* err);

}  // namespace cc

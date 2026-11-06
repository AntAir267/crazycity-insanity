#include "vfs/source.h"

#include <cstdio>
#include <cstring>

namespace cc {

bool MemorySource::read(uint64_t offset, void* dst, size_t len) {
    if (offset > data_.size() || len > data_.size() - offset) return false;
    if (len) std::memcpy(dst, data_.data() + offset, len);
    return true;
}

namespace {
class FileSource : public ByteSource {
public:
    FileSource(std::FILE* f, uint64_t size) : f_(f), size_(size) {}
    ~FileSource() override {
        if (f_) std::fclose(f_);
    }
    uint64_t size() const override { return size_; }
    bool read(uint64_t offset, void* dst, size_t len) override {
        if (offset > size_ || len > size_ - offset) return false;
        if (len == 0) return true;
        if (std::fseek(f_, long(offset), SEEK_SET) != 0) return false;
        return std::fread(dst, 1, len, f_) == len;
    }

private:
    std::FILE* f_;
    uint64_t size_;
};
}  // namespace

std::shared_ptr<ByteSource> openFileSource(const std::string& path, std::string* err) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "cannot open " + path;
        return nullptr;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        std::fclose(f);
        if (err) *err = "cannot size " + path;
        return nullptr;
    }
    return std::make_shared<FileSource>(f, uint64_t(sz));
}

}  // namespace cc

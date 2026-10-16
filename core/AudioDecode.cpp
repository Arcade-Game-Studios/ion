#include <ion/audio/Audio.hpp>
#include <ion/core/Log.hpp>

#include "AudioInternal.hpp"

#define STB_VORBIS_NO_PUSHDATA_API
#include "../third_party/stb_vorbis.c"
#undef R
#undef L
#undef C

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_SIMD
#include "../third_party/dr_mp3.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace ion {

namespace {

using namespace audio_detail;

std::string extensionOf(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return ext;
}

// Decodes everything a stream produces into a Sound.
Sound drain(AudioStream& stream) {
    std::vector<float> samples;
    std::vector<float> block(4096 * 2);
    for (;;) {
        size_t n = stream.read(block.data(), 4096);
        if (n == 0) break;
        samples.insert(samples.end(), block.begin(), block.begin() + n * stream.channels());
    }
    return makeSound(std::move(samples), stream.channels(), stream.sampleRate());
}

class OggStream final : public AudioStream {
public:
    ~OggStream() override {
        if (v_) stb_vorbis_close(v_);
    }
    bool openFile(const std::string& path) {
        int err = 0;
        v_ = stb_vorbis_open_filename(path.c_str(), &err, nullptr);
        return init_();
    }
    bool openMemory(const uint8_t* data, size_t size) {
        int err = 0;
        v_ = stb_vorbis_open_memory(data, (int)size, &err, nullptr);
        return init_();
    }
    uint32_t channels() const override { return channels_; }
    uint32_t sampleRate() const override { return rate_; }
    size_t read(float* out, size_t frames) override {
        int n = stb_vorbis_get_samples_float_interleaved(v_, (int)channels_, out,
                                                         (int)(frames * channels_));
        return n > 0 ? (size_t)n : 0;
    }
    bool rewind() override { return stb_vorbis_seek_start(v_) != 0; }

private:
    bool init_() {
        if (!v_) return false;
        stb_vorbis_info info = stb_vorbis_get_info(v_);
        if (info.channels < 1) return false;
        channels_ = info.channels > 1 ? 2 : 1; // extra channels are dropped
        rate_ = info.sample_rate;
        return rate_ > 0;
    }
    stb_vorbis* v_ = nullptr;
    uint32_t channels_ = 1, rate_ = 0;
};

class Mp3Stream final : public AudioStream {
public:
    ~Mp3Stream() override {
        if (open_) drmp3_uninit(&mp3_);
    }
    bool openFile(const std::string& path) {
        open_ = drmp3_init_file(&mp3_, path.c_str(), nullptr) != 0;
        return init_();
    }
    bool openMemory(const uint8_t* data, size_t size) {
        open_ = drmp3_init_memory(&mp3_, data, size, nullptr) != 0;
        return init_();
    }
    uint32_t channels() const override { return mp3_.channels > 1 ? 2 : 1; }
    uint32_t sampleRate() const override { return mp3_.sampleRate; }
    size_t read(float* out, size_t frames) override {
        return (size_t)drmp3_read_pcm_frames_f32(&mp3_, frames, out);
    }
    bool rewind() override { return drmp3_seek_to_pcm_frame(&mp3_, 0) != 0; }

private:
    bool init_() {
        if (!open_) return false;
        if (mp3_.channels < 1 || mp3_.channels > 2 || mp3_.sampleRate == 0) {
            drmp3_uninit(&mp3_);
            open_ = false;
        }
        return open_;
    }
    drmp3 mp3_{};
    bool open_ = false;
};

// Streams PCM/float WAV straight from disk.
class WavStream final : public AudioStream {
public:
    bool open(const std::string& path) {
        file_.open(path, std::ios::binary);
        if (!file_) return false;
        uint8_t hdr[12];
        if (!file_.read((char*)hdr, 12) || std::memcmp(hdr, "RIFF", 4) != 0 ||
            std::memcmp(hdr + 8, "WAVE", 4) != 0) {
            return false;
        }
        bool haveFmt = false;
        uint16_t format = 0;
        for (;;) {
            uint8_t ch[8];
            if (!file_.read((char*)ch, 8)) break;
            uint32_t size = rd32(ch + 4);
            if (std::memcmp(ch, "fmt ", 4) == 0 && size >= 16) {
                std::vector<uint8_t> body(size);
                if (!file_.read((char*)body.data(), size)) return false;
                format = rd16(body.data());
                channels_ = rd16(body.data() + 2);
                rate_ = rd32(body.data() + 4);
                bits_ = rd16(body.data() + 14);
                if (format == 0xFFFE && size >= 26) format = rd16(body.data() + 24);
                haveFmt = true;
            } else if (std::memcmp(ch, "data", 4) == 0) {
                dataStart_ = file_.tellg();
                dataBytes_ = size;
                break;
            } else {
                file_.seekg(size, std::ios::cur);
            }
            if (size & 1) file_.seekg(1, std::ios::cur);
        }
        if (!haveFmt || dataBytes_ == 0 || channels_ < 1 || channels_ > 2 || rate_ == 0 ||
            !supportedPcm(format, bits_, isFloat_)) {
            return false;
        }
        return true;
    }
    uint32_t channels() const override { return channels_; }
    uint32_t sampleRate() const override { return rate_; }
    size_t read(float* out, size_t frames) override {
        size_t frameBytes = (size_t)channels_ * (bits_ / 8);
        size_t wantBytes = std::min<size_t>(frames * frameBytes, dataBytes_ - consumed_);
        if (wantBytes == 0) return 0;
        scratch_.resize(wantBytes);
        file_.read((char*)scratch_.data(), (std::streamsize)wantBytes);
        size_t gotFrames = (size_t)file_.gcount() / frameBytes;
        consumed_ += gotFrames * frameBytes;
        convertPcm(scratch_.data(), gotFrames * channels_, bits_, isFloat_, out);
        return gotFrames;
    }
    bool rewind() override {
        file_.clear();
        file_.seekg(dataStart_);
        consumed_ = 0;
        return (bool)file_;
    }

private:
    std::ifstream file_;
    std::streampos dataStart_{};
    size_t dataBytes_ = 0, consumed_ = 0;
    uint16_t channels_ = 0, bits_ = 0;
    uint32_t rate_ = 0;
    bool isFloat_ = false;
    std::vector<uint8_t> scratch_;
};

template <typename S, typename... Args>
Sound decodeAll(const char* what, bool (S::*open)(Args...), Args... args) {
    S stream;
    if (!(stream.*open)(args...)) {
        ION_LOG_ERROR("audio: failed to decode %s", what);
        return nullptr;
    }
    return drain(stream);
}

} // namespace

Sound loadOgg(const std::string& path) {
    return decodeAll<OggStream, const std::string&>(path.c_str(), &OggStream::openFile, path);
}
Sound loadOggFromMemory(const uint8_t* data, size_t size) {
    return decodeAll<OggStream, const uint8_t*, size_t>("Ogg data", &OggStream::openMemory, data, size);
}
Sound loadMp3(const std::string& path) {
    return decodeAll<Mp3Stream, const std::string&>(path.c_str(), &Mp3Stream::openFile, path);
}
Sound loadMp3FromMemory(const uint8_t* data, size_t size) {
    return decodeAll<Mp3Stream, const uint8_t*, size_t>("MP3 data", &Mp3Stream::openMemory, data, size);
}

Sound loadSound(const std::string& path) {
    std::string ext = extensionOf(path);
    if (ext == "wav") return loadWav(path);
    if (ext == "ogg") return loadOgg(path);
    if (ext == "mp3") return loadMp3(path);
    ION_LOG_ERROR("audio: unsupported format '%s'", path.c_str());
    return nullptr;
}

std::unique_ptr<AudioStream> openStream(const std::string& path) {
    std::string ext = extensionOf(path);
    std::unique_ptr<AudioStream> out;
    bool ok = false;
    if (ext == "wav") {
        auto s = std::make_unique<WavStream>();
        ok = s->open(path);
        out = std::move(s);
    } else if (ext == "ogg") {
        auto s = std::make_unique<OggStream>();
        ok = s->openFile(path);
        out = std::move(s);
    } else if (ext == "mp3") {
        auto s = std::make_unique<Mp3Stream>();
        ok = s->openFile(path);
        out = std::move(s);
    } else {
        ION_LOG_ERROR("audio: unsupported format '%s'", path.c_str());
        return nullptr;
    }
    if (!ok) {
        ION_LOG_ERROR("audio: cannot open stream '%s'", path.c_str());
        return nullptr;
    }
    return out;
}

} // namespace ion

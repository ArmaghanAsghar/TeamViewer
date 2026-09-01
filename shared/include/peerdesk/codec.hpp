#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace peerdesk {

class H264Encoder {
public:
    H264Encoder() = default;
    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;
    ~H264Encoder();

    bool open(int width, int height, int fps, std::string& err);
    bool encode_rgb(std::span<const uint8_t> rgb, std::string& annexb, std::string& err);
    void close();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

class H264Decoder {
public:
    H264Decoder() = default;
    H264Decoder(const H264Decoder&) = delete;
    H264Decoder& operator=(const H264Decoder&) = delete;
    ~H264Decoder();

    bool open(std::string& err);
    bool decode_to_rgb(std::span<const uint8_t> annexb, std::vector<uint8_t>& rgb, int& width,
                       int& height, std::string& err);
    void close();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace peerdesk

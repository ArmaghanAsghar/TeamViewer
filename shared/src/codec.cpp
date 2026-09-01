#include "peerdesk/codec.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <memory>

namespace peerdesk {
namespace {

void log_av(std::string& err, const char* prefix, int rc) {
    char buf[128];
    av_strerror(rc, buf, sizeof(buf));
    err = std::string(prefix) + ": " + buf;
}

}  // namespace

struct H264Encoder::Impl {
    AVCodecContext* ctx = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    int64_t pts = 0;
    int width = 0;
    int height = 0;
};

H264Encoder::~H264Encoder() { close(); }

void H264Encoder::close() {
    if (!impl_) return;
    if (impl_->pkt) av_packet_free(&impl_->pkt);
    if (impl_->frame) av_frame_free(&impl_->frame);
    if (impl_->sws) sws_freeContext(impl_->sws);
    if (impl_->ctx) avcodec_free_context(&impl_->ctx);
    delete impl_;
    impl_ = nullptr;
}

bool H264Encoder::open(int width, int height, int fps, std::string& err) {
    close();
    if (width <= 0 || height <= 0 || fps <= 0) {
        err = "Invalid encoder size/fps";
        return false;
    }
    width &= ~1;
    height &= ~1;
    if (width < 2 || height < 2) {
        err = "Frame too small for H.264";
        return false;
    }

    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        err = "H.264 encoder not found (need FFmpeg built with libx264)";
        return false;
    }

    impl_ = new Impl();
    impl_->width = width;
    impl_->height = height;
    impl_->ctx = avcodec_alloc_context3(codec);
    if (!impl_->ctx) {
        err = "avcodec_alloc_context3 failed";
        close();
        return false;
    }
    impl_->ctx->width = width;
    impl_->ctx->height = height;
    impl_->ctx->time_base = {1, fps};
    impl_->ctx->framerate = {fps, 1};
    impl_->ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    impl_->ctx->gop_size = fps;
    impl_->ctx->max_b_frames = 0;
    impl_->ctx->bit_rate = 2'500'000;
    impl_->ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (impl_->ctx->priv_data) {
        av_opt_set(impl_->ctx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(impl_->ctx->priv_data, "tune", "zerolatency", 0);
        av_opt_set(impl_->ctx->priv_data, "repeat-headers", "1", 0);
    }

    int rc = avcodec_open2(impl_->ctx, codec, nullptr);
    if (rc < 0) {
        log_av(err, "avcodec_open2", rc);
        close();
        return false;
    }
    impl_->sws = sws_getContext(width, height, AV_PIX_FMT_RGB24, width, height, AV_PIX_FMT_YUV420P,
                                SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    impl_->frame = av_frame_alloc();
    impl_->pkt = av_packet_alloc();
    if (!impl_->sws || !impl_->frame || !impl_->pkt) {
        err = "FFmpeg frame alloc failed";
        close();
        return false;
    }
    impl_->frame->format = AV_PIX_FMT_YUV420P;
    impl_->frame->width = width;
    impl_->frame->height = height;
    rc = av_frame_get_buffer(impl_->frame, 32);
    if (rc < 0) {
        log_av(err, "av_frame_get_buffer", rc);
        close();
        return false;
    }
    return true;
}

bool H264Encoder::encode_rgb(std::span<const uint8_t> rgb, std::string& annexb, std::string& err) {
    annexb.clear();
    if (!impl_ || !impl_->ctx) {
        err = "Encoder not open";
        return false;
    }
    const size_t need = static_cast<size_t>(impl_->width) * static_cast<size_t>(impl_->height) * 3;
    if (rgb.size() < need) {
        err = "RGB buffer too small";
        return false;
    }
    uint8_t* src_slice[1] = {const_cast<uint8_t*>(rgb.data())};
    int src_stride[1] = {impl_->width * 3};
    sws_scale(impl_->sws, src_slice, src_stride, 0, impl_->height, impl_->frame->data,
              impl_->frame->linesize);
    impl_->frame->pts = impl_->pts++;
    int rc = avcodec_send_frame(impl_->ctx, impl_->frame);
    if (rc < 0) {
        log_av(err, "avcodec_send_frame", rc);
        return false;
    }
    while (true) {
        rc = avcodec_receive_packet(impl_->ctx, impl_->pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
        if (rc < 0) {
            log_av(err, "avcodec_receive_packet", rc);
            return false;
        }
        annexb.append(reinterpret_cast<const char*>(impl_->pkt->data),
                      static_cast<size_t>(impl_->pkt->size));
        av_packet_unref(impl_->pkt);
    }
    return true;
}

struct H264Decoder::Impl {
    AVCodecContext* ctx = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    int rgb_w = 0;
    int rgb_h = 0;
};

H264Decoder::~H264Decoder() { close(); }

void H264Decoder::close() {
    if (!impl_) return;
    if (impl_->pkt) av_packet_free(&impl_->pkt);
    if (impl_->frame) av_frame_free(&impl_->frame);
    if (impl_->sws) sws_freeContext(impl_->sws);
    if (impl_->ctx) avcodec_free_context(&impl_->ctx);
    delete impl_;
    impl_ = nullptr;
}

bool H264Decoder::open(std::string& err) {
    close();
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        err = "H.264 decoder not found";
        return false;
    }
    impl_ = new Impl();
    impl_->ctx = avcodec_alloc_context3(codec);
    impl_->frame = av_frame_alloc();
    impl_->pkt = av_packet_alloc();
    if (!impl_->ctx || !impl_->frame || !impl_->pkt) {
        err = "Decoder alloc failed";
        close();
        return false;
    }
    impl_->ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    const int rc = avcodec_open2(impl_->ctx, codec, nullptr);
    if (rc < 0) {
        log_av(err, "avcodec_open2 decode", rc);
        close();
        return false;
    }
    return true;
}

bool H264Decoder::decode_to_rgb(std::span<const uint8_t> annexb, std::vector<uint8_t>& rgb,
                                int& width, int& height, std::string& err) {
    if (!impl_ || !impl_->ctx) {
        err = "Decoder not open";
        return false;
    }
    if (annexb.empty()) return false;
    impl_->pkt->data = const_cast<uint8_t*>(annexb.data());
    impl_->pkt->size = static_cast<int>(annexb.size());
    int rc = avcodec_send_packet(impl_->ctx, impl_->pkt);
    impl_->pkt->data = nullptr;
    impl_->pkt->size = 0;
    if (rc < 0) {
        log_av(err, "avcodec_send_packet", rc);
        return false;
    }
    rc = avcodec_receive_frame(impl_->ctx, impl_->frame);
    if (rc == AVERROR(EAGAIN)) return false;
    if (rc < 0) {
        log_av(err, "avcodec_receive_frame", rc);
        return false;
    }
    width = impl_->frame->width;
    height = impl_->frame->height;
    if (impl_->sws == nullptr || impl_->rgb_w != width || impl_->rgb_h != height) {
        if (impl_->sws) sws_freeContext(impl_->sws);
        impl_->sws = sws_getContext(width, height, static_cast<AVPixelFormat>(impl_->frame->format),
                                    width, height, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, nullptr,
                                    nullptr, nullptr);
        impl_->rgb_w = width;
        impl_->rgb_h = height;
        if (!impl_->sws) {
            err = "sws_getContext decode failed";
            return false;
        }
    }
    rgb.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    uint8_t* dst[1] = {rgb.data()};
    int dst_stride[1] = {width * 3};
    sws_scale(impl_->sws, impl_->frame->data, impl_->frame->linesize, 0, height, dst, dst_stride);
    av_frame_unref(impl_->frame);
    return true;
}

}  // namespace peerdesk

#include "capture.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

#if defined(PEERDESK_HAVE_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xdamage.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

namespace peerdesk {

X11Capture::~X11Capture() { close(); }

void X11Capture::close() {
#if defined(PEERDESK_HAVE_X11)
    auto* dpy = static_cast<Display*>(dpy_);
    if (dpy && damage_) {
        XDamageDestroy(dpy, static_cast<Damage>(reinterpret_cast<uintptr_t>(damage_)));
        damage_ = nullptr;
    }
    if (dpy && use_shm_ && shm_image_ && shm_info_) {
        auto* img = static_cast<XImage*>(shm_image_);
        auto* info = static_cast<XShmSegmentInfo*>(shm_info_);
        XShmDetach(dpy, info);
        if (info->shmaddr) shmdt(info->shmaddr);
        XDestroyImage(img);
        delete info;
        shm_image_ = nullptr;
        shm_info_ = nullptr;
    }
    if (dpy) {
        XCloseDisplay(dpy);
        dpy_ = nullptr;
    }
#endif
}

bool X11Capture::open(std::string& err) {
#if !defined(PEERDESK_HAVE_X11)
    err = "X11 capture is only available on Linux hosts";
    return false;
#else
    close();
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        err = "Cannot open X11 display (check DISPLAY and permissions)";
        return false;
    }
    Screen* scr = DefaultScreenOfDisplay(dpy);
    width_ = WidthOfScreen(scr);
    height_ = HeightOfScreen(scr);
    if (width_ <= 0 || height_ <= 0) {
        XCloseDisplay(dpy);
        err = "X11 screen has invalid size";
        return false;
    }
    Window root = DefaultRootWindow(dpy);
    int ev_base = 0, err_base = 0;
    if (XDamageQueryExtension(dpy, &ev_base, &err_base)) {
        damage_event_base_ = ev_base;
        Damage dmg = XDamageCreate(dpy, root, XDamageReportNonEmpty);
        damage_ = reinterpret_cast<void*>(static_cast<uintptr_t>(dmg));
    }
    if (XShmQueryExtension(dpy)) {
        auto* info = new XShmSegmentInfo{};
        Visual* vis = DefaultVisual(dpy, DefaultScreen(dpy));
        const int depth = DefaultDepth(dpy, DefaultScreen(dpy));
        XImage* img = XShmCreateImage(dpy, vis, static_cast<unsigned>(depth), ZPixmap, nullptr, info,
                                      static_cast<unsigned>(width_), static_cast<unsigned>(height_));
        if (img) {
            info->shmid = shmget(IPC_PRIVATE, static_cast<size_t>(img->bytes_per_line) * img->height,
                                 IPC_CREAT | 0777);
            if (info->shmid >= 0) {
                info->shmaddr = img->data = static_cast<char*>(shmat(info->shmid, nullptr, 0));
                info->readOnly = False;
                if (info->shmaddr && XShmAttach(dpy, info)) {
                    shmctl(info->shmid, IPC_RMID, nullptr);
                    shm_image_ = img;
                    shm_info_ = info;
                    use_shm_ = true;
                }
            }
            if (!use_shm_) {
                if (img->data) shmdt(img->data);
                XDestroyImage(img);
                delete info;
            }
        } else {
            delete info;
        }
    }
    dpy_ = dpy;
    first_ = true;
    return true;
#endif
}

bool X11Capture::grab_rgb(std::vector<uint8_t>& rgb, std::string& err) {
#if !defined(PEERDESK_HAVE_X11)
    err = "X11 display closed";
    return false;
#else
    auto* dpy = static_cast<Display*>(dpy_);
    if (!dpy) {
        err = "X11 display closed";
        return false;
    }
    Window root = DefaultRootWindow(dpy);
    if (damage_ && !first_) {
        bool damaged = false;
        XEvent ev{};
        while (XCheckTypedEvent(dpy, damage_event_base_ + XDamageNotify, &ev)) {
            damaged = true;
            XDamageSubtract(dpy, static_cast<Damage>(reinterpret_cast<uintptr_t>(damage_)), None,
                            None);
        }
        if (!damaged && !last_rgb_.empty()) {
            rgb = last_rgb_;
            return true;
        }
    }
    first_ = false;

    auto copy_image = [&](XImage* img) {
        rgb.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 3);
        if (img->bits_per_pixel == 32) {
            for (int y = 0; y < height_; ++y) {
                const auto* row = reinterpret_cast<const uint32_t*>(
                    img->data + static_cast<size_t>(y) * static_cast<size_t>(img->bytes_per_line));
                for (int x = 0; x < width_; ++x) {
                    const uint32_t p = row[x];
                    const size_t i =
                        (static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)) *
                        3;
                    rgb[i + 0] = static_cast<uint8_t>((p >> 16) & 0xff);
                    rgb[i + 1] = static_cast<uint8_t>((p >> 8) & 0xff);
                    rgb[i + 2] = static_cast<uint8_t>(p & 0xff);
                }
            }
        } else {
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    const unsigned long p = XGetPixel(img, x, y);
                    const size_t i =
                        (static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)) *
                        3;
                    rgb[i + 0] = static_cast<uint8_t>((p >> 16) & 0xff);
                    rgb[i + 1] = static_cast<uint8_t>((p >> 8) & 0xff);
                    rgb[i + 2] = static_cast<uint8_t>(p & 0xff);
                }
            }
        }
    };

    if (use_shm_ && shm_image_) {
        auto* img = static_cast<XImage*>(shm_image_);
        if (!XShmGetImage(dpy, root, img, 0, 0, AllPlanes)) {
            err = "XShmGetImage failed (capture permission?)";
            return false;
        }
        copy_image(img);
    } else {
        XImage* img = XGetImage(dpy, root, 0, 0, static_cast<unsigned>(width_),
                                static_cast<unsigned>(height_), AllPlanes, ZPixmap);
        if (!img) {
            err = "XGetImage failed (capture permission?)";
            return false;
        }
        copy_image(img);
        XDestroyImage(img);
    }
    last_rgb_ = rgb;
    return true;
#endif
}

SyntheticCapture::SyntheticCapture(int w, int h) : width_(w), height_(h) {}

bool SyntheticCapture::open(std::string& err) {
    if (width_ <= 0 || height_ <= 0) {
        err = "Invalid synthetic size";
        return false;
    }
    return true;
}

void SyntheticCapture::note_input(const std::string& line) {
    std::lock_guard<std::mutex> g(mu_);
    last_input_ = line;
}

namespace {

void put_pixel(std::vector<uint8_t>& rgb, int w, int h, int x, int y, uint8_t r, uint8_t g,
               uint8_t b) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3;
    rgb[i] = r;
    rgb[i + 1] = g;
    rgb[i + 2] = b;
}

void fill_rect(std::vector<uint8_t>& rgb, int w, int h, int x, int y, int rw, int rh, uint8_t r,
               uint8_t g, uint8_t b) {
    for (int yy = y; yy < y + rh; ++yy)
        for (int xx = x; xx < x + rw; ++xx) put_pixel(rgb, w, h, xx, yy, r, g, b);
}

void draw_char(std::vector<uint8_t>& rgb, int w, int h, int x, int y, char c, uint8_t r, uint8_t g,
               uint8_t b) {
    const char* rows = " ### "
                       "#   #"
                       "#   #"
                       "#####"
                       "#   #"
                       "#   #"
                       "#   #";
    switch (c) {
        case '0':
            rows = " ### #   ##  ### # ##  ##   # ### ";
            break;
        case '1':
            rows = "  #   ##    #    #    #    #   ### ";
            break;
        case '2':
            rows = " ### #   #    #  ##  #   #    #####";
            break;
        case '3':
            rows = " ### #   #    #  ##     ##   # ### ";
            break;
        case '4':
            rows = "#   ##   ##   ######    #    #    #";
            break;
        case '5':
            rows = "######    ####     #    ##   # ### ";
            break;
        case '6':
            rows = " ### #    #### #   ##   ##   # ### ";
            break;
        case '7':
            rows = "#####    #   #   #    #    #    #  ";
            break;
        case '8':
            rows = " ### #   ##   # ### #   ##   # ### ";
            break;
        case '9':
            rows = " ### #   ##   # ####    #    # ### ";
            break;
        case ':':
            rows = "       #    #         #    #       ";
            break;
        case 'P':
            rows = "#### #   ##   #####    #    #    # ";
            break;
        case 'E':
            rows = "#####     #    ####     #     #####";
            break;
        case 'R':
            rows = "#### #   ##   #####  #  #  # #   # ";
            break;
        case 'D':
            rows = "#### #   ##   ##   ##   ##   ##### ";
            break;
        case 'S':
            rows = " ####     #     ###     #     #### ";
            break;
        case 'K':
            rows = "#   ##  # # #  ##   # #  #  # #   #";
            break;
        case ' ':
            rows = "                                   ";
            break;
        default:
            break;
    }
    for (int gy = 0; gy < 7; ++gy)
        for (int gx = 0; gx < 5; ++gx)
            if (rows[gy * 5 + gx] == '#') fill_rect(rgb, w, h, x + gx * 2, y + gy * 2, 2, 2, r, g, b);
}

void draw_text(std::vector<uint8_t>& rgb, int w, int h, int x, int y, const std::string& s, uint8_t r,
               uint8_t g, uint8_t b) {
    int cx = x;
    for (char ch : s) {
        draw_char(rgb, w, h, cx, y, ch, r, g, b);
        cx += 12;
    }
}

}  // namespace

bool SyntheticCapture::grab_rgb(std::vector<uint8_t>& rgb, std::string& err) {
    (void)err;
    rgb.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 3, 0);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const bool grid = (x % 64 == 0) || (y % 64 == 0);
            put_pixel(rgb, width_, height_, x, y, grid ? 28 : 12, grid ? 42 : 18, grid ? 68 : 32);
        }
    }
    fill_rect(rgb, width_, height_, 40, 36, width_ - 80, 88, 18, 32, 58);
    draw_text(rgb, width_, height_, 56, 52, "PEERDESK", 230, 236, 255);
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);
    char clock[32];
    std::snprintf(clock, sizeof(clock), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    draw_text(rgb, width_, height_, 56, 160, clock, 255, 210, 90);
    std::string input;
    {
        std::lock_guard<std::mutex> g(mu_);
        input = last_input_;
    }
    for (auto& ch : input)
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 32);
    draw_text(rgb, width_, height_, 56, 220, input.substr(0, 24), 220, 240, 220);
    fill_rect(rgb, width_, height_, width_ / 2 - 40, height_ / 2 - 40, 80, 80, 220, 80, 70);
    return true;
}

}  // namespace peerdesk

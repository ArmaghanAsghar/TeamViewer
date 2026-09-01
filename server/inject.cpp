#include "inject.hpp"

#if defined(PEERDESK_HAVE_X11)
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#endif

namespace peerdesk {

InputInject::~InputInject() {
#if defined(PEERDESK_HAVE_X11)
    if (dpy_) {
        XCloseDisplay(static_cast<Display*>(dpy_));
        dpy_ = nullptr;
    }
#endif
}

bool InputInject::open(std::string& err) {
#if !defined(PEERDESK_HAVE_X11)
    err = "XTEST input inject is only available on Linux hosts";
    return false;
#else
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        err = "Cannot open X11 display for input injection";
        return false;
    }
    int ev = 0, errn = 0, maj = 0, minv = 0;
    if (!XTestQueryExtension(dpy, &ev, &errn, &maj, &minv)) {
        XCloseDisplay(dpy);
        err = "XTEST extension missing (cannot inject mouse/keyboard)";
        return false;
    }
    dpy_ = dpy;
    return true;
#endif
}

void InputInject::apply_mouse(const MouseEvent& e) {
#if defined(PEERDESK_HAVE_X11)
    auto* dpy = static_cast<Display*>(dpy_);
    if (!dpy) return;
    XTestFakeMotionEvent(dpy, -1, e.x, e.y, CurrentTime);
    if (e.action == MouseAction::Down || e.action == MouseAction::Up) {
        const int button = e.button == 0 ? 1 : static_cast<int>(e.button);
        XTestFakeButtonEvent(dpy, button, e.action == MouseAction::Down ? True : False, CurrentTime);
    }
    if (e.action == MouseAction::Wheel) {
        const int button = e.wheel_delta > 0 ? 4 : 5;
        XTestFakeButtonEvent(dpy, button, True, CurrentTime);
        XTestFakeButtonEvent(dpy, button, False, CurrentTime);
    }
    XFlush(dpy);
#else
    (void)e;
#endif
}

void InputInject::apply_key(const KeyEvent& e) {
#if defined(PEERDESK_HAVE_X11)
    auto* dpy = static_cast<Display*>(dpy_);
    if (!dpy || e.keysym == 0) return;
    const KeyCode code = XKeysymToKeycode(dpy, static_cast<KeySym>(e.keysym));
    if (code == 0) return;
    XTestFakeKeyEvent(dpy, code, e.down ? True : False, CurrentTime);
    XFlush(dpy);
#else
    (void)e;
#endif
}

}  // namespace peerdesk

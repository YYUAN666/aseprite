// SHE library
// Copyright (C) 2017-2018  David Capello
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "she/x11/window.h"

#include "gfx/rect.h"
#include "she/event.h"
#include "she/x11/keys.h"

#include <X11/cursorfont.h>
#include <map>

#define KEY_TRACE(...)
#define MOUSE_TRACE(...)
#define EVENT_TRACE(...)

namespace she {

namespace {

// Event generated by the window manager when the close button on the
// window is pressed by the userh.
Atom wmDeleteMessage = 0;

// Cursor Without pixels to simulate a hidden X11 cursor
Cursor empty_xcursor = None;

// See https://bugs.freedesktop.org/show_bug.cgi?id=12871 for more
// information, it looks like the official way to convert a X Window
// into our own user data pointer (X11Window instance) is using a map.
std::map<::Window, X11Window*> g_activeWindows;

KeyModifiers get_modifiers_from_x(int state)
{
  int modifiers = kKeyNoneModifier;
  if (state & ShiftMask) {
    modifiers |= kKeyShiftModifier;
    KEY_TRACE("+SHIFT\n");
  }
  if (state & ControlMask) {
    modifiers |= kKeyCtrlModifier;
    KEY_TRACE("+CTRL\n");
  }
  // Mod1Mask is Alt, and Mod5Mask is AltGr
  if (state & (Mod1Mask | Mod5Mask)) {
    modifiers |= kKeyAltModifier;
    KEY_TRACE("+ALT\n");
  }
  // Mod4Mask is Windows key
  if (state & Mod4Mask) {
    modifiers |= kKeyWinModifier;
    KEY_TRACE("+WIN\n");
  }
  return (KeyModifiers)modifiers;
}

bool is_mouse_wheel_button(int button)
{
  return (button == Button4 || button == Button5 ||
          button == 6 || button == 7);
}

gfx::Point get_mouse_wheel_delta(int button)
{
  gfx::Point delta(0, 0);
  switch (button) {
    // Vertical wheel
    case Button4: delta.y = -1; break;
    case Button5: delta.y = +1; break;
    // Horizontal wheel
    case 6: delta.x = -1; break;
    case 7: delta.x = +1; break;
  }
  return delta;
}

Event::MouseButton get_mouse_button_from_x(int button)
{
  switch (button) {
    case Button1: MOUSE_TRACE("LeftButton\n");   return Event::LeftButton;
    case Button2: MOUSE_TRACE("MiddleButton\n"); return Event::MiddleButton;
    case Button3: MOUSE_TRACE("RightButton\n");  return Event::RightButton;
    case 8:       MOUSE_TRACE("X1Button\n");     return Event::X1Button;
    case 9:       MOUSE_TRACE("X2Button\n");     return Event::X2Button;
  }
  MOUSE_TRACE("Unknown Button %d\n", button);
  return Event::NoneButton;
}

} // anonymous namespace

// static
X11Window* X11Window::getPointerFromHandle(Window handle)
{
  auto it = g_activeWindows.find(handle);
  if (it != g_activeWindows.end())
    return it->second;
  else
    return nullptr;
}

// static
void X11Window::addWindow(X11Window* window)
{
  ASSERT(g_activeWindows.find(window->handle()) == g_activeWindows.end());
  g_activeWindows[window->handle()] = window;
}

// static
void X11Window::removeWindow(X11Window* window)
{
  auto it = g_activeWindows.find(window->handle());
  ASSERT(it != g_activeWindows.end());
  if (it != g_activeWindows.end()) {
    ASSERT(it->second == window);
    g_activeWindows.erase(it);
  }
}

X11Window::X11Window(::Display* display, int width, int height, int scale)
  : m_display(display)
  , m_gc(nullptr)
  , m_cursor(None)
  , m_scale(scale)
{
  // Initialize special messages (just the first time a X11Window is
  // created)
  if (!wmDeleteMessage)
    wmDeleteMessage = XInternAtom(m_display, "WM_DELETE_WINDOW", False);

  ::Window root = XDefaultRootWindow(m_display);

  XSetWindowAttributes swa;
  swa.event_mask = (StructureNotifyMask | ExposureMask | PropertyChangeMask |
                    EnterWindowMask | LeaveWindowMask | FocusChangeMask |
                    ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                    KeyPressMask | KeyReleaseMask);

  m_window = XCreateWindow(
    m_display, root,
    0, 0, width, height, 0,
    CopyFromParent,
    InputOutput,
    CopyFromParent,
    CWEventMask,
    &swa);

  XMapWindow(m_display, m_window);
  XSetWMProtocols(m_display, m_window, &wmDeleteMessage, 1);

  m_gc = XCreateGC(m_display, m_window, 0, nullptr);

  X11Window::addWindow(this);
}

X11Window::~X11Window()
{
  XFreeGC(m_display, m_gc);
  XDestroyWindow(m_display, m_window);

  X11Window::removeWindow(this);
}

void X11Window::setScale(const int scale)
{
  m_scale = scale;
  resizeDisplay(clientSize());
}

void X11Window::setTitle(const std::string& title)
{
  XTextProperty prop;
  prop.value = (unsigned char*)title.c_str();
  prop.encoding = XA_STRING;
  prop.format = 8;
  prop.nitems = std::strlen((char*)title.c_str());
  XSetWMName(m_display, m_window, &prop);
}

gfx::Size X11Window::clientSize() const
{
  Window root;
  int x, y;
  unsigned int width, height, border, depth;
  XGetGeometry(m_display, m_window, &root,
               &x, &y, &width, &height, &border, &depth);
  return gfx::Size(int(width), int(height));
}

gfx::Size X11Window::restoredSize() const
{
  Window root;
  int x, y;
  unsigned int width, height, border, depth;
  XGetGeometry(m_display, m_window, &root,
               &x, &y, &width, &height, &border, &depth);
  return gfx::Size(int(width), int(height));
}

void X11Window::captureMouse()
{
}

void X11Window::releaseMouse()
{
}

void X11Window::setMousePosition(const gfx::Point& position)
{
}

void X11Window::updateWindow(const gfx::Rect& unscaledBounds)
{
  XEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.xexpose.type = Expose;
  ev.xexpose.display = x11display();
  ev.xexpose.window = handle();
  ev.xexpose.x = unscaledBounds.x*m_scale;
  ev.xexpose.y = unscaledBounds.y*m_scale;
  ev.xexpose.width = unscaledBounds.w*m_scale;
  ev.xexpose.height = unscaledBounds.h*m_scale;
  XSendEvent(m_display, m_window, False,
             ExposureMask, (XEvent*)&ev);
}

bool X11Window::setNativeMouseCursor(NativeCursor cursor)
{
  Cursor xcursor = None;

  switch (cursor) {
    case kNoCursor: {
      if (empty_xcursor == None) {
        char data = 0;
        Pixmap image = XCreateBitmapFromData(
          m_display, m_window, (char*)&data, 1, 1);

        XColor color;
        empty_xcursor = XCreatePixmapCursor(
          m_display, image, image, &color, &color, 0, 0);

        XFreePixmap(m_display, image);
      }
      xcursor = empty_xcursor;
      break;
    }
    case kArrowCursor:
      xcursor = XCreateFontCursor(m_display, XC_arrow);
      break;
    case kCrosshairCursor:
      xcursor = XCreateFontCursor(m_display, XC_crosshair);
      break;
    case kIBeamCursor:
      xcursor = XCreateFontCursor(m_display, XC_xterm);
      break;
    case kWaitCursor:
      xcursor = XCreateFontCursor(m_display, XC_watch);
      break;
    case kLinkCursor:
      xcursor = XCreateFontCursor(m_display, XC_hand1);
      break;
    case kHelpCursor:
      xcursor = XCreateFontCursor(m_display, XC_question_arrow);
      break;
    case kForbiddenCursor:
      xcursor = XCreateFontCursor(m_display, XC_X_cursor);
      break;
    case kMoveCursor:
      xcursor = XCreateFontCursor(m_display, XC_fleur);
      break;
    case kSizeNCursor:
      xcursor = XCreateFontCursor(m_display, XC_top_side);
      break;
    case kSizeNSCursor:
      xcursor = XCreateFontCursor(m_display, XC_sb_v_double_arrow);
      break;
    case kSizeSCursor:
      xcursor = XCreateFontCursor(m_display, XC_bottom_side);
      break;
    case kSizeWCursor:
      xcursor = XCreateFontCursor(m_display, XC_left_side);
      break;
    case kSizeECursor:
      xcursor = XCreateFontCursor(m_display, XC_right_side);
      break;
    case kSizeWECursor:
      xcursor = XCreateFontCursor(m_display, XC_sb_h_double_arrow);
      break;
    case kSizeNWCursor:
      xcursor = XCreateFontCursor(m_display, XC_top_left_corner);
      break;
    case kSizeNECursor:
      xcursor = XCreateFontCursor(m_display, XC_top_right_corner);
      break;
    case kSizeSWCursor:
      xcursor = XCreateFontCursor(m_display, XC_bottom_left_corner);
      break;
    case kSizeSECursor:
      xcursor = XCreateFontCursor(m_display, XC_bottom_right_corner);
      break;
  }

  if (m_cursor != None) {
    if (m_cursor != empty_xcursor) // Don't delete empty_xcursor
      XFreeCursor(m_display, m_cursor);
    m_cursor = None;
  }

  if (xcursor != None) {
    m_cursor = xcursor;
    XDefineCursor(m_display, m_window, xcursor);
    return true;
  }
  else
    return false;
}

bool X11Window::setNativeMouseCursor(const she::Surface* surface,
                                     const gfx::Point& focus,
                                     const int scale)
{
  return false;
}

void X11Window::processX11Event(XEvent& event)
{
  switch (event.type) {

    case ConfigureNotify: {
      gfx::Size newSize(event.xconfigure.width,
                        event.xconfigure.height);

      if (newSize.w > 0 &&
          newSize.h > 0 &&
          newSize != clientSize()) {
        resizeDisplay(newSize);
      }
      break;
    }

    case Expose: {
      gfx::Rect rc(event.xexpose.x, event.xexpose.y,
                   event.xexpose.width, event.xexpose.height);
      paintGC(rc);
      break;
    }

    case KeyPress:
    case KeyRelease: {
      Event ev;

      ev.setType(event.type == KeyPress ? Event::KeyDown: Event::KeyUp);

      KeySym keysym = XLookupKeysym(&event.xkey, 0);
      ev.setScancode(x11_keysym_to_scancode(keysym));

      int modifiers = (int)get_modifiers_from_x(event.xkey.state);
      switch (keysym) {
        case XK_Shift_L:
        case XK_Shift_R:
          modifiers |= kKeyShiftModifier;
          break;
        case XK_Control_L:
        case XK_Control_R:
          modifiers |= kKeyCtrlModifier;
          break;
        case XK_Alt_L:
        case XK_Alt_R:
          modifiers |= kKeyAltModifier;
          break;
        case XK_Meta_L:
        case XK_Super_L:
        case XK_Meta_R:
        case XK_Super_R:
          modifiers |= kKeyWinModifier;
          break;
      }
      ev.setModifiers((KeyModifiers)modifiers);
      KEY_TRACE("%s state=%04x keycode=%04x\n",
                (event.type == KeyPress ? "KeyPress": "KeyRelease"),
                event.xkey.state,
                event.xkey.keycode);

#ifndef NDEBUG
      {
        char* str = XKeysymToString(keysym);
        KEY_TRACE(" > %s\n", str);
      }
#endif

      queueEvent(ev);
      break;
    }

    case ButtonPress:
    case ButtonRelease: {
      Event ev;

      if (is_mouse_wheel_button(event.xbutton.button)) {
        ev.setType(Event::MouseWheel);
        ev.setWheelDelta(get_mouse_wheel_delta(event.xbutton.button));
      }
      else {
        ev.setType(event.type == ButtonPress? Event::MouseDown:
                                              Event::MouseUp);
        ev.setButton(get_mouse_button_from_x(event.xbutton.button));
      }
      ev.setModifiers(get_modifiers_from_x(event.xbutton.state));
      ev.setPosition(gfx::Point(event.xbutton.x / m_scale,
                                event.xbutton.y / m_scale));

      queueEvent(ev);
      break;
    }

    case MotionNotify: {
      Event ev;
      ev.setType(Event::MouseMove);
      ev.setModifiers(get_modifiers_from_x(event.xmotion.state));
      ev.setPosition(gfx::Point(event.xmotion.x / m_scale,
                                event.xmotion.y / m_scale));
      queueEvent(ev);
      break;
    }

    case EnterNotify:
    case LeaveNotify:
      // "mode" can be NotifyGrab or NotifyUngrab when middle mouse
      // button is pressed/released. We must not generated
      // MouseEnter/Leave events on those cases, only on NotifyNormal
      // (when mouse leaves/enter the X11 window).
      if (event.xcrossing.mode == NotifyNormal) {
        Event ev;
        ev.setType(event.type == EnterNotify ? Event::MouseEnter:
                                               Event::MouseLeave);
        ev.setModifiers(get_modifiers_from_x(event.xcrossing.state));
        ev.setPosition(gfx::Point(event.xcrossing.x / m_scale,
                                  event.xcrossing.y / m_scale));
        queueEvent(ev);
      }
      break;

    case ClientMessage:
      // When the close button is pressed
      if (Atom(event.xclient.data.l[0]) == wmDeleteMessage) {
        Event ev;
        ev.setType(Event::CloseDisplay);
        queueEvent(ev);
      }
      break;

  }
}

} // namespace she

// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/automation/ui_controls.h"

#include "app/keyboard_code_conversion_win.h"
#include "app/keyboard_codes.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/string_util.h"
#include "base/win_util.h"
#include "base/ref_counted.h"
#include "base/task.h"
#include "views/view.h"

namespace ui_controls {

namespace {

void Checkpoint(const char* message, const base::TimeTicks& start_time) {
  LOG(INFO) << message << " : "
            << (base::TimeTicks::Now() - start_time).InMilliseconds()
            << " ms" << std::flush;
}

// InputDispatcher ------------------------------------------------------------

// InputDispatcher is used to listen for a mouse/keyboard event. When the
// appropriate event is received the task is notified.
class InputDispatcher : public base::RefCounted<InputDispatcher> {
 public:
  InputDispatcher(Task* task, WPARAM message_waiting_for);

  // Invoked from the hook. If mouse_message matches message_waiting_for_
  // MatchingMessageFound is invoked.
  void DispatchedMessage(WPARAM mouse_message);

  // Invoked when a matching event is found. Uninstalls the hook and schedules
  // an event that notifies the task.
  void MatchingMessageFound();

 private:
  friend class base::RefCounted<InputDispatcher>;

  ~InputDispatcher();

   // Notifies the task and release this (which should delete it).
  void NotifyTask();

  // The task we notify.
  scoped_ptr<Task> task_;

  // Message we're waiting for. Not used for keyboard events.
  const WPARAM message_waiting_for_;

  DISALLOW_COPY_AND_ASSIGN(InputDispatcher);
};

// Have we installed the hook?
bool installed_hook_ = false;

// Return value from SetWindowsHookEx.
HHOOK next_hook_ = NULL;

// If a hook is installed, this is the dispatcher.
InputDispatcher* current_dispatcher_ = NULL;

// Callback from hook when a mouse message is received.
LRESULT CALLBACK MouseHook(int n_code, WPARAM w_param, LPARAM l_param) {
  HHOOK next_hook = next_hook_;
  if (n_code == HC_ACTION) {
    DCHECK(current_dispatcher_);
    current_dispatcher_->DispatchedMessage(w_param);
  }
  return CallNextHookEx(next_hook, n_code, w_param, l_param);
}

// Callback from hook when a key message is received.
LRESULT CALLBACK KeyHook(int n_code, WPARAM w_param, LPARAM l_param) {
  base::TimeTicks start_time = base::TimeTicks::Now();
  char msg[512];
  base::snprintf(msg, 512, "KeyHook starts: %d", n_code);
  Checkpoint(msg, start_time);

  HHOOK next_hook = next_hook_;
  base::snprintf(msg, 512, "n_code == HC_ACTION: %d, %d",
          l_param, !!(l_param & (1 << 30)));
  Checkpoint(msg, start_time);
  if (n_code == HC_ACTION) {
    DCHECK(current_dispatcher_);
    if (l_param & (1 << 30)) {  // Only send on key up.
      Checkpoint("MatchingMessageFound", start_time);
      current_dispatcher_->MatchingMessageFound();
    } else {
      Checkpoint("Not key up", start_time);
    }
  }
  Checkpoint("KeyHook ends, calling next hook.", start_time);
  return CallNextHookEx(next_hook, n_code, w_param, l_param);
}

// Installs dispatcher as the current hook.
void InstallHook(InputDispatcher* dispatcher, bool key_hook) {
  DCHECK(!installed_hook_);
  current_dispatcher_ = dispatcher;
  installed_hook_ = true;
  if (key_hook) {
    next_hook_ = SetWindowsHookEx(WH_KEYBOARD, &KeyHook, NULL,
                                  GetCurrentThreadId());
  } else {
    // NOTE: I originally tried WH_CALLWNDPROCRET, but for some reason I
    // didn't get a mouse message like I do with MouseHook.
    next_hook_ = SetWindowsHookEx(WH_MOUSE, &MouseHook, NULL,
                                  GetCurrentThreadId());
  }
  DCHECK(next_hook_);
}

// Uninstalls the hook set in InstallHook.
void UninstallHook(InputDispatcher* dispatcher) {
  if (current_dispatcher_ == dispatcher) {
    installed_hook_ = false;
    current_dispatcher_ = NULL;
    UnhookWindowsHookEx(next_hook_);
  }
}

InputDispatcher::InputDispatcher(Task* task, UINT message_waiting_for)
    : task_(task), message_waiting_for_(message_waiting_for) {
  InstallHook(this, message_waiting_for == WM_KEYUP);
}

InputDispatcher::~InputDispatcher() {
  // Make sure the hook isn't installed.
  UninstallHook(this);
}

void InputDispatcher::DispatchedMessage(WPARAM message) {
  if (message == message_waiting_for_)
    MatchingMessageFound();
}

void InputDispatcher::MatchingMessageFound() {
  UninstallHook(this);
  // At the time we're invoked the event has not actually been processed.
  // Use PostTask to make sure the event has been processed before notifying.
  MessageLoop::current()->PostDelayedTask(
      FROM_HERE, NewRunnableMethod(this, &InputDispatcher::NotifyTask), 0);
}

void InputDispatcher::NotifyTask() {
  task_->Run();
  Release();
}

// Private functions ----------------------------------------------------------

// Populate the INPUT structure with the appropriate keyboard event
// parameters required by SendInput
bool FillKeyboardInput(app::KeyboardCode key, INPUT* input, bool key_up) {
  memset(input, 0, sizeof(INPUT));
  input->type = INPUT_KEYBOARD;
  input->ki.wVk = app::WindowsKeyCodeForKeyboardCode(key);
  input->ki.dwFlags = key_up ? KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP :
                               KEYEVENTF_EXTENDEDKEY;

  return true;
}

// Send a key event (up/down)
bool SendKeyEvent(app::KeyboardCode key, bool up) {
  INPUT input = { 0 };

  if (!FillKeyboardInput(key, &input, up))
    return false;

  if (!::SendInput(1, &input, sizeof(INPUT)))
    return false;

  return true;
}

bool SendKeyPressImpl(app::KeyboardCode key,
                      bool control, bool shift, bool alt,
                      Task* task) {
  base::TimeTicks start_time = base::TimeTicks::Now();
  Checkpoint("SendKeyPressImpl starts", start_time);

  scoped_refptr<InputDispatcher> dispatcher(
      task ? new InputDispatcher(task, WM_KEYUP) : NULL);

  // If a pop-up menu is open, it won't receive events sent using SendInput.
  // Check for a pop-up menu using its window class (#32768) and if one
  // exists, send the key event directly there.
  Checkpoint("FindWindow", start_time);
  HWND popup_menu = ::FindWindow(L"#32768", 0);
  if (popup_menu != NULL && popup_menu == ::GetTopWindow(NULL)) {
    Checkpoint("Found popup window", start_time);
    WPARAM w_param = app::WindowsKeyCodeForKeyboardCode(key);
    LPARAM l_param = 0;
    Checkpoint("Send WM_KEYDOWN", start_time);
    ::SendMessage(popup_menu, WM_KEYDOWN, w_param, l_param);
    Checkpoint("Send WM_KEYUP", start_time);
    ::SendMessage(popup_menu, WM_KEYUP, w_param, l_param);

    Checkpoint("Send Done", start_time);
    if (dispatcher.get())
      dispatcher->AddRef();
    return true;
  }

  Checkpoint("Found no popup window", start_time);

  INPUT input[8] = { 0 }; // 8, assuming all the modifiers are activated

  UINT i = 0;
  if (control) {
    Checkpoint("FillKeyboardInput Control", start_time);
    if (!FillKeyboardInput(app::VKEY_CONTROL, &input[i], false))
      return false;
    i++;
  }

  if (shift) {
    Checkpoint("FillKeyboardInput Shift", start_time);
    if (!FillKeyboardInput(app::VKEY_SHIFT, &input[i], false))
      return false;
    i++;
  }

  if (alt) {
    Checkpoint("FillKeyboardInput Alt", start_time);
    if (!FillKeyboardInput(app::VKEY_MENU, &input[i], false))
      return false;
    i++;
  }

  Checkpoint("FillKeyboardInput 1", start_time);
  if (!FillKeyboardInput(key, &input[i], false))
    return false;
  i++;

  Checkpoint("FillKeyboardInput 2", start_time);
  if (!FillKeyboardInput(key, &input[i], true))
    return false;
  i++;

  if (alt) {
    Checkpoint("FillKeyboardInput Alt2", start_time);
    if (!FillKeyboardInput(app::VKEY_MENU, &input[i], true))
      return false;
    i++;
  }

  if (shift) {
    Checkpoint("FillKeyboardInput Shift2", start_time);
    if (!FillKeyboardInput(app::VKEY_SHIFT, &input[i], true))
      return false;
    i++;
  }

  if (control) {
    Checkpoint("FillKeyboardInput Ctrl2", start_time);
    if (!FillKeyboardInput(app::VKEY_CONTROL, &input[i], true))
      return false;
    i++;
  }

  Checkpoint("SendInput called", start_time);
  if (::SendInput(i, input, sizeof(INPUT)) != i)
    return false;

  Checkpoint("SendInput done", start_time);

  if (dispatcher.get())
    dispatcher->AddRef();

  Checkpoint("Test done", start_time);
  return true;
}

bool SendMouseMoveImpl(long x, long y, Task* task) {
  // First check if the mouse is already there.
  POINT current_pos;
  ::GetCursorPos(&current_pos);
  if (x == current_pos.x && y == current_pos.y) {
    if (task)
      MessageLoop::current()->PostTask(FROM_HERE, task);
    return true;
  }

  INPUT input = { 0 };

  int screen_width = ::GetSystemMetrics(SM_CXSCREEN) - 1;
  int screen_height  = ::GetSystemMetrics(SM_CYSCREEN) - 1;
  LONG pixel_x  = static_cast<LONG>(x * (65535.0f / screen_width));
  LONG pixel_y = static_cast<LONG>(y * (65535.0f / screen_height));

  input.type = INPUT_MOUSE;
  input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
  input.mi.dx = pixel_x;
  input.mi.dy = pixel_y;

  scoped_refptr<InputDispatcher> dispatcher(
      task ? new InputDispatcher(task, WM_MOUSEMOVE) : NULL);

  if (!::SendInput(1, &input, sizeof(INPUT)))
    return false;

  if (dispatcher.get())
    dispatcher->AddRef();

  return true;
}

bool SendMouseEventsImpl(MouseButton type, int state, Task* task) {
  DWORD down_flags = MOUSEEVENTF_ABSOLUTE;
  DWORD up_flags = MOUSEEVENTF_ABSOLUTE;
  UINT last_event;

  switch (type) {
    case LEFT:
      down_flags |= MOUSEEVENTF_LEFTDOWN;
      up_flags |= MOUSEEVENTF_LEFTUP;
      last_event = (state & UP) ? WM_LBUTTONUP : WM_LBUTTONDOWN;
      break;

    case MIDDLE:
      down_flags |= MOUSEEVENTF_MIDDLEDOWN;
      up_flags |= MOUSEEVENTF_MIDDLEUP;
      last_event = (state & UP) ? WM_MBUTTONUP : WM_MBUTTONDOWN;
      break;

    case RIGHT:
      down_flags |= MOUSEEVENTF_RIGHTDOWN;
      up_flags |= MOUSEEVENTF_RIGHTUP;
      last_event = (state & UP) ? WM_RBUTTONUP : WM_RBUTTONDOWN;
      break;

    default:
      NOTREACHED();
      return false;
  }

  scoped_refptr<InputDispatcher> dispatcher(
      task ? new InputDispatcher(task, last_event) : NULL);

  INPUT input = { 0 };
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = down_flags;
  if ((state & DOWN) && !::SendInput(1, &input, sizeof(INPUT)))
    return false;

  input.mi.dwFlags = up_flags;
  if ((state & UP) && !::SendInput(1, &input, sizeof(INPUT)))
    return false;

  if (dispatcher.get())
    dispatcher->AddRef();

  return true;
}

}  // namespace

// public functions -----------------------------------------------------------

bool SendKeyPress(gfx::NativeWindow window, app::KeyboardCode key,
                  bool control, bool shift, bool alt, bool command) {
  DCHECK(command == false);  // No command key on Windows
  return SendKeyPressImpl(key, control, shift, alt, NULL);
}

bool SendKeyPressNotifyWhenDone(gfx::NativeWindow window,
                                app::KeyboardCode key,
                                bool control, bool shift, bool alt,
                                bool command,
                                Task* task) {
  DCHECK(command == false);  // No command key on Windows
  return SendKeyPressImpl(key, control, shift, alt, task);
}

bool SendMouseMove(long x, long y) {
  return SendMouseMoveImpl(x, y, NULL);
}

bool SendMouseMoveNotifyWhenDone(long x, long y, Task* task) {
  return SendMouseMoveImpl(x, y, task);
}

bool SendMouseEvents(MouseButton type, int state) {
  return SendMouseEventsImpl(type, state, NULL);
}

bool SendMouseEventsNotifyWhenDone(MouseButton type, int state, Task* task) {
  return SendMouseEventsImpl(type, state, task);
}

bool SendMouseClick(MouseButton type) {
  return SendMouseEventsImpl(type, UP | DOWN, NULL);
}

void MoveMouseToCenterAndPress(views::View* view, MouseButton button,
                               int state, Task* task) {
  DCHECK(view);
  DCHECK(view->GetWidget());
  gfx::Point view_center(view->width() / 2, view->height() / 2);
  views::View::ConvertPointToScreen(view, &view_center);
  SendMouseMove(view_center.x(), view_center.y());
  SendMouseEventsNotifyWhenDone(button, state, task);
}

}  // ui_controls

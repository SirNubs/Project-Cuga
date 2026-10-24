#pragma once

// MHP3RD_INPUT_SCRIPT: scripted input for testing the interface without a
// person at the controls, the way MHP3RD_AUTO_CONFIRM walks through the game.
//
// The script is a list of `frame:action` steps separated by semicolons. The
// frame counts window-event pumps: one per game frame while the game runs, one
// per interface frame while a screen is up. Actions:
//
//   key NAME [N]      press a key and release it N frames later, 4 by default
//                     (SDL key names: Escape, Down, Return, Q). While the game
//                     has input it reaches the game through the bindings too
//   mouse DX DY       move the mouse by DX, DY counts
//   click BUTTON [N]  press a mouse button (left, middle, right, x1, x2) for N
//                     frames, 4 by default
//   pad BUTTON[+...] [N]
//                     press buttons of a virtual gamepad and release them N
//                     frames later, 4 by default (SDL names: a, b, x, y,
//                     start, leftstick, rightstick, leftshoulder, dpup,
//                     dpdown, dpleft, dpright, ...)
//   axis NAME VALUE   hold an axis of the virtual gamepad at VALUE, -1 to 1
//                     (leftx, lefty, rightx, righty, lefttrigger, righttrigger)
//   joy K attach [NAME]
//                     a test joystick numbered K that SDL has no mapping for,
//                     like a controller it does not know: 12 buttons, 4 axes
//                     and a hat, USB ids 0810:0001
//   joy K button N [F] press its button N for F frames, 4 by default
//   joy K hat MASK [F] push its hat (1 up, 2 right, 4 down, 8 left) for F frames
//   joy K axis N VALUE hold its axis N at VALUE, -1 to 1
//   joy K detach      unplug it
//   finger ID down|move|up X Y
//                     a finger of a virtual touch screen, X and Y from 0 to 1
//                     across the window (up needs no position)
//   hold ID X Y [N]   a finger down at X Y and lifted N frames later, 4 by default
//   swipe ID X1 Y1 X2 Y2 [N]
//                     a finger down at X1 Y1, moved to X2 Y2 over N frames
//                     (8 by default) and lifted
//   drag X1 Y1 X2 Y2 [N]
//                     the pointer pressed at X1 Y1, moved to X2 Y2 over N
//                     frames (8 by default) and released, as a touch sends it
//                     to the interface (X and Y from 0 to 1)
//   text STRING       type text
//   drop PATH         drop a file onto the window
//   shot NAME         write the window image to MHP3RD_SCREENSHOT_DIR/NAME.bmp
//   quit              close the window
//
// For example: MHP3RD_INPUT_SCRIPT="300:key Escape;330:shot menu;360:pad leftstick+rightstick"
//
// A script with mouse steps counts the pointer as captured for the game
// without taking the real one, so it works with the window in the background,
// and only its own mouse steps reach the game.
//
// MHP3RD_INPUT_LIVE names a file read while the game runs: each line appended
// to it is one step, and its frame counts from when the line is read, so
// `echo "0:shot now" >> file` captures the window within a few frames. It
// always connects the virtual pad. Used to drive two instances side by side,
// for example in ad hoc tests.
namespace mhp3rd::ui::script {

// Reads the script and, if it presses gamepad buttons, connects the virtual pad.
void attach();
// Runs the steps due at this frame. Called before each pump.
void tick();

} // namespace mhp3rd::ui::script

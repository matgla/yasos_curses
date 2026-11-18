// Copyright (c) 2025 Mateusz Stadnik <matgla@live.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// getch's key translation, fed through a pipe on stdin.

#include "utest.h"

#include <ncurses.h>

#include <string.h>
#include <unistd.h>

// getch on `bytes`, written to a pipe that replaces stdin for the call.
static int getch_from(const char *bytes, int *second) {
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    return -2;
  }
  const int saved = dup(STDIN_FILENO);
  dup2(pipe_fds[0], STDIN_FILENO);
  close(pipe_fds[0]);
  if (write(pipe_fds[1], bytes, strlen(bytes)) < 0) {
    return -2;
  }
  close(pipe_fds[1]);
  const int first = getch();
  if (second != NULL) {
    *second = getch();
  }
  dup2(saved, STDIN_FILENO);
  close(saved);
  return first;
}

UTEST(keys, backspace_key_is_key_backspace) {
  EXPECT_EQ(KEY_BACKSPACE, getch_from("\x7f", NULL));
}

// Ctrl-H stays 8, as with ncurses on a terminal whose Backspace sends DEL:
// yasvi gives it a meaning of its own (to the file tree).
UTEST(keys, ctrl_h_stays_itself) {
  int second = 0;
  EXPECT_EQ(8, getch_from("\x08\x7f", &second));
  EXPECT_EQ(KEY_BACKSPACE, second);
}

UTEST(keys, enter_and_arrows) {
  int second = 0;
  EXPECT_EQ('\n', getch_from("\r\nx", &second));
  EXPECT_EQ('x', second);
  EXPECT_EQ(KEY_UP, getch_from("\x1b[A", NULL));
}

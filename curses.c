/*
 Copyright (c) 2025 Mateusz Stadnik

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "include/ncurses.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <termios.h>
#include <unistd.h>

#define CLEAR_SCREEN "\033[2J"
#define CLEAR_SCREEN_LEN 4
#define CLEAR_TO_EOL "\033[K"
#define CLEAR_TO_EOL_LEN 3

#define CURSOR_HIDE "\033[?25l"
#define CURSOR_HIDE_LEN 6
#define CURSOR_SHOW "\033[?25h"
#define CURSOR_SHOW_LEN 6
#define CURSOR_HOME "\033[H"
#define CURSOR_HOME_LEN 3

#define RESET_STYLE "\033[0m"
#define RESET_STYLE_LEN 4
#define RESET_COLOR_STYLE "\033[39;49m"
#define RESET_COLOR_STYLE_LEN 8

#define BOLD_MODE_ON "\033[1m"
#define BOLD_MODE_ON_LEN 4
#define BOLD_MODE_OFF "\033[22m"
#define BOLD_MODE_OFF_LEN 5

#define ITALIC_MODE_ON "\033[3m"
#define ITALIC_MODE_ON_LEN 4
#define ITALIC_MODE_OFF "\033[23m"
#define ITALIC_MODE_OFF_LEN 5

#define COLOR_ATTRIBUTE (1 << 7)

WINDOW *stdscr = NULL;

struct termios original_stdout_termios;
struct termios current_stdout_termios;
struct termios original_stdin_termios;
struct termios current_stdin_termios;

int original_fcntl_flags = 0;
int current_fcntl_flags = 0;
int cursor_enabled = 1;

typedef struct {
  uint8_t bg : 4;
  uint8_t fg : 4;
} ColorPair;

ColorPair color_pairs[16] = {0};

#ifdef __GNUC__
char *itoa(int n, char *s, int base) {
  char *p = s;
  int sign = n < 0 ? -1 : 1;
  if (sign < 0)
    n = -n;
  do {
    *p++ = "0123456789abcdef"[n % base];
    n /= base;
  } while (n);
  if (sign < 0)
    *p++ = '-';
  *p-- = '\0';
  while (s < p) {
    char tmp = *s;
    *s++ = *p;
    *p-- = tmp;
  }
  return s;
}

#endif

void getmaxyx_(WINDOW *win, int *y, int *x) {
  if (y != NULL) {
    *y = win->y;
  }
  if (x != NULL) {
    *x = win->x;
  }
}

int start_color(void) {
  return 0;
}

int init_pair(short pair, short f, short b) {
  if (pair < 0 || pair > 15) {
    return -1;
  }
  color_pairs[pair].fg = f;
  color_pairs[pair].bg = b;
  return 0;
}

int cbreak(void) {
  current_stdout_termios.c_lflag &= ~(ICANON);
  current_stdin_termios.c_lflag &= ~(ICANON);
  tcsetattr(STDOUT_FILENO, TCSAFLUSH, &current_stdout_termios);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &current_stdin_termios);
  return 0;
}

int noecho(void) {
  current_stdout_termios.c_lflag &= ~(ECHO);
  current_stdin_termios.c_lflag &= ~(ECHO);
  tcsetattr(STDOUT_FILENO, TCSAFLUSH, &current_stdout_termios);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &current_stdin_termios);
  return 0;
}

int curs_set(int visibility) {
  cursor_enabled = visibility;
  if (visibility == 0) {
    write(STDOUT_FILENO, CURSOR_HIDE, CURSOR_HIDE_LEN);
  } else if (visibility == 1) {
    write(STDOUT_FILENO, CURSOR_SHOW, CURSOR_SHOW_LEN);
  } else {
    return -1;
  }
  return 0;
}

int nodelay(WINDOW *win, int bf) {
  (void)win;
  (void)bf;
  current_fcntl_flags = fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK;
  fcntl(STDIN_FILENO, F_SETFL, current_fcntl_flags);
  return 0;
}

int getch(void) {
  int ch = 0;
  int readed = read(STDIN_FILENO, &ch, 1);
  int i = 0;
  if (readed <= 0) {
    return -1;
  }
  if (ch == 0x1B) {
    char seq = 0;
    while (read(STDIN_FILENO, &seq, 1) == 0) {
      struct timespec remaining, request = {0, 1000000};
      nanosleep(&request, &remaining);
      if (i++ > 10) {
        return ch;
      }
    }
    if (seq != '[') {
      return ch;
    }
    seq = 0;
    while (read(STDIN_FILENO, &seq, 1) == 0) {
      read(STDIN_FILENO, &seq, 1);
    }
    switch (seq) {
    case 'A':
      ch = KEY_UP;
      break;
    case 'B':
      ch = KEY_DOWN;
      break;
    case 'C':
      ch = KEY_RIGHT;
      break;
    case 'D':
      ch = KEY_LEFT;
      break;
    }
  } else if (ch == 0x08 || ch == 0x7F) {
    // Handle backspace
    ch = KEY_BACKSPACE;
  }
  return ch;
}

int waddch(WINDOW *win, const char ch) {
  return write(STDOUT_FILENO, &ch, 1);
}

int mvwaddch(WINDOW *win, int y, int x, const char ch) {
  if (y < 0 || y >= win->y || x < 0 || x >= win->x) {
    return -1; // Out of bounds
  }
  wmove(win, y, x); // Move cursor to the specified position
  waddch(win, ch);  // Add the character at the current cursor position
  return 0;
}

int mvaddch(int y, int x, const char ch) {
  return mvwaddch(stdscr, y, x, ch);
}

static int write_color_code(int color_pair) {
  char buffer[32];
  if (color_pair < 0 || color_pair > 15) {
    return -1; // Invalid color pair
  }
  const ColorPair pair = color_pairs[color_pair];
  if (pair.fg < 0 || pair.fg > 15 || pair.bg < 0 || pair.bg > 15) {
    return -1; // Invalid color
  }
  buffer[0] = '\e'; // ESC
  buffer[1] = '[';
  char *ptr = buffer + 2;
  itoa(pair.fg + 30, ptr, 10); // Foreground color
  ptr += strlen(ptr);
  *ptr++ = ';';
  itoa(pair.bg + 40, ptr, 10); // Background color
  return write(STDOUT_FILENO, buffer, strlen(ptr));
}

int wattron(WINDOW *win, int attr) {
  // if (attr & A_ITALIC) {
  //   write(STDOUT_FILENO, ITALIC_MODE_ON, ITALIC_MODE_ON_LEN);
  // }
  // if (attr & A_BOLD) {
  //   write(STDOUT_FILENO, BOLD_MODE_ON, BOLD_MODE_ON_LEN);
  // }
  // if (attr & COLOR_ATTRIBUTE) {
  //   const int color_pair = attr & 0x7f; // Extract color pair
  //   write_color_code(color_pair);
  // }
  return 0;
}

int wattroff(WINDOW *win, int attr) {
  // if (attr & A_ITALIC) {
  //   write(STDOUT_FILENO, ITALIC_MODE_OFF, ITALIC_MODE_OFF_LEN);
  // }
  // if (attr & A_BOLD) {
  //   write(STDOUT_FILENO, BOLD_MODE_OFF, BOLD_MODE_OFF_LEN);
  // }
  // if (attr & COLOR_ATTRIBUTE) {
  //   write(STDOUT_FILENO, RESET_COLOR_STYLE, RESET_COLOR_STYLE_LEN);
  // }

  return 0;
}

int attron(int attr) {
  return wattron(stdscr, attr);
}

int attroff(int attr) {
  return wattroff(stdscr, attr);
}

void refresh(void) {
  wrefresh(stdscr);
  return;
}

int napms(int ms) {
  // temporary implementation
  for (int i = 0; i < ms; ++i) {
    for (int j = 0; j < 10000; ++j) {
    }
  }
  return 0;
}

int keypad(WINDOW *win, int bf) {
  (void)win;
  (void)bf;
  return 0;
}

int wendwin(WINDOW *win) {
  if (win != NULL) {
    free(win);
  }
  return 0;
}

int endwin(void) {
  tcsetattr(STDOUT_FILENO, TCSAFLUSH, &original_stdout_termios);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_stdin_termios);
  fcntl(STDIN_FILENO, F_SETFL, &original_fcntl_flags);
  write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_LEN);
  write(STDOUT_FILENO, CURSOR_SHOW, CURSOR_SHOW_LEN);
  write(STDOUT_FILENO, RESET_STYLE, RESET_STYLE_LEN);
  wendwin(stdscr);
  stdscr = NULL;
  return 0;
}

int COLOR_PAIR(int color) {
  return COLOR_ATTRIBUTE | (color & 0x7f);
}

WINDOW *newwin(int nlines, int ncols, int begin_y, int begin_x) {
  (void)begin_y;
  (void)begin_x;
  WINDOW *win = malloc(sizeof(WINDOW));
  win->x = ncols;
  win->y = nlines;
  return win;
}

WINDOW *initscr(void) {
  if (stdscr != NULL) {
    endwin(); // Clean up previous window
  }
  stdscr = newwin(24, 80, 0, 0);
  tcgetattr(STDOUT_FILENO, &original_stdout_termios);
  tcgetattr(STDIN_FILENO, &original_stdin_termios);
  original_fcntl_flags = fcntl(STDIN_FILENO, F_GETFL, &original_fcntl_flags);

  current_stdout_termios = original_stdout_termios;
  current_stdin_termios = original_stdin_termios;

  write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_LEN);

  return stdscr;
}

int box(WINDOW *win, int verch, int horch) {
  for (int i = 0; i < win->x; i++) {
    mvwaddch(win, win->y - 1, i, horch);
    mvwaddch(win, 0, i, horch);
  }
  for (int i = 0; i < win->y; i++) {
    mvwaddch(win, i, 0, verch);
    mvwaddch(win, i, win->x - 1, verch);
  }
  return 0;
}

int wrefresh(WINDOW *win) {
  // refresh buffer after buffering implementation on terminal side
  return 0;
}

int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  wmove(win, y, x);
  vprintf(fmt, args);
  fflush(stdout);
  va_end(args);
  return 0;
}

int mvprintw(int y, int x, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  move(y, x);
  int ret = vprintf(fmt, args);
  fflush(stdout);
  va_end(args);
  return ret;
}

int mvwaddnstr(WINDOW *win, int y, int x, const char *str, int n) {
  int len = strnlen(str, n);
  if (len + x > win->x) {
    len = win->x - x;
  }
  wmove(win, y, x);
  write(STDOUT_FILENO, str, len);
  return len;
}

int mvwaddstr(WINDOW *win, int y, int x, const char *str) {
  const int len = strlen(str);
  wmove(win, y, x);
  write(STDERR_FILENO, str, len);
  return len;
}

int wmove(WINDOW *win, int y, int x) {
  if (y < 0 || y >= win->y || x < 0 || x >= win->x) {
    return -1; // Out of bounds
  }
  char buffer[64];
  buffer[0] = '\e';
  buffer[1] = '[';
  char *ptr = buffer + 2;
  itoa(y + 1, buffer + 2, 10);
  ptr += strlen(ptr);
  *ptr++ = ';';
  itoa(x + 1, ptr, 10);
  ptr += strlen(ptr);
  const int len = strlen(buffer);
  *ptr++ = 'H';

  write(STDOUT_FILENO, buffer, len + 1);
  fsync(STDOUT_FILENO);
  // printf("X[%d;%dH", y + 1, x + 1); // Move cursor to (y, x)
  // fflush(stdout);
  return 0;
}

int wclear(WINDOW *win) {
  // TODO: erase only window content
  write(STDOUT_FILENO, CLEAR_SCREEN, strlen(CLEAR_SCREEN));
  return 0;
}

int clear(void) {
  return wclear(stdscr);
}

int raw(void) {
  current_stdout_termios.c_lflag &= ~(ICANON | ECHO);
  current_stdin_termios.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDOUT_FILENO, TCSAFLUSH, &current_stdout_termios);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &current_stdin_termios);
  return 0;
}

int wclrtoeol(WINDOW *win) {
  write(STDOUT_FILENO, CLEAR_TO_EOL, CLEAR_TO_EOL_LEN);
  return 0;
}

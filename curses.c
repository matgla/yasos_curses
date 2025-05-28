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

#include <termios.h>
#include <unistd.h>

#define CLEAR_SCREEN "\033[2J"
#define CURSOR_HIDE "\033[?25l"
#define CURSOR_SHOW "\033[?25h"
#define RESET_STYLE "\033[0m"
#define CURSOR_SAVE "\0337"
#define CURSOR_RESTORE "\0338"

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

static void set_cell_dirty(WINDOW *win, int y, int x) {
  win->attribute_map[y][x] = win->current_attribute;
  win->attribute_map[y][x].dirty = 1;
}

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
    printf(CURSOR_HIDE);
  } else if (visibility == 1) {
    printf(CURSOR_SHOW);
  } else {
    return -1;
  }
  return 0;
}

int nodelay(WINDOW *win, int bf) {
  (void)win;
  (void)bf;
  current_fcntl_flags = current_fcntl_flags | O_NONBLOCK;
  fcntl(STDIN_FILENO, F_SETFL, &current_fcntl_flags);
  return 0;
}

int getch(void) {
  int ch = 0;
  read(STDIN_FILENO, &ch, 1);
  if (ch == 0x1B) {
    char seq = 0;
    while (read(STDIN_FILENO, &seq, 1) == 0) {
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

int mvwaddch(WINDOW *win, int y, int x, const char ch) {
  if (y < 0 || y >= win->y || x < 0 || x >= win->x) {
    return -1; // Out of bounds
  }
  // printf("\033[%d;%dH", y, x);
  win->cursor_x = x + 2;
  win->cursor_y = y + 1;

  win->lines[y][x] = ch;
  set_cell_dirty(win, y, x);
  return 0;
}

int mvaddch(int y, int x, const char ch) {
  return mvwaddch(stdscr, y, x, ch);
}

int wattron(WINDOW *win, int attr) {
  if (attr & A_ITALIC) {
    win->current_attribute.italic = 1;
  }
  if (attr & A_BOLD) {
    win->current_attribute.bold = 1;
  }
  if (attr & COLOR_ATTRIBUTE) {
    win->current_attribute.color = attr & 0x7f; // Store color pair
    win->current_attribute.color_enabled = 1;
  }
  return 0;
}

int wattroff(WINDOW *win, int attr) {
  if (attr & A_ITALIC) {
    win->current_attribute.italic = 0;
  }
  if (attr & A_BOLD) {
    win->current_attribute.bold = 0;
  }
  if (attr & COLOR_ATTRIBUTE) {
    win->current_attribute.color_enabled = 0;
    win->current_attribute.color = 0;
  }

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
    for (int i = 0; i < win->y; ++i) {
      free(win->lines[i]);
      free(win->attribute_map[i]);
    }
    free(win->lines);
    free(win->line_buffer);
    free(win->attribute_map);
    free(win);
  }
  return 0;
}

int endwin(void) {
  tcsetattr(STDOUT_FILENO, TCSAFLUSH, &original_stdout_termios);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_stdin_termios);
  fcntl(STDIN_FILENO, F_SETFL, &original_fcntl_flags);
  printf(CLEAR_SCREEN);
  printf(CURSOR_SHOW);
  printf(RESET_STYLE);
  fflush(stdout);
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
  win->cursor_x = 1;
  win->cursor_y = 1;
  win->line_buffer = malloc(sizeof(char) * (win->x + 1));
  win->lines = malloc(sizeof(char *) * win->y);
  for (int i = 0; i < win->y; ++i) {
    win->lines[i] = malloc(sizeof(char) * (win->x));
    memset(win->lines[i], ' ', win->x);
  }

  win->attribute_map = malloc(sizeof(Attribute *) * win->y);
  for (int i = 0; i < win->y; ++i) {
    win->attribute_map[i] = malloc(sizeof(Attribute) * (win->x));
    memset(win->attribute_map[i], 0, sizeof(Attribute) * win->x);
  }
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

  printf(CLEAR_SCREEN);
  fflush(stdout);

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
  printf(CURSOR_HIDE);
  for (int y = 0; y < win->y; ++y) {
    for (int x = 0; x < win->x; ++x) {
      if (win->attribute_map[y][x].dirty == 1) {
        win->attribute_map[y][x].dirty = 0; // Reset modified map
        if (win->attribute_map[y][x].color_enabled) {
          const int fg = color_pairs[win->attribute_map[y][x].color & 0x7f].fg;
          const int bg = color_pairs[win->attribute_map[y][x].color & 0x7f].bg;

          printf("\033[%d;%dm", fg + 30, bg + 40);
        } else {
          printf("\033[39m");
        }
        printf("\033[%d;%dH%c", y + 1, x + 1, win->lines[y][x]);
      }
    }
    fflush(stdout);
  }

  printf("\033[%d;%dH", win->cursor_y, win->cursor_x);
  fflush(stdout);
  if (cursor_enabled) {
    printf(CURSOR_SHOW);
  }
  fflush(stdout);

  return 0;
}
static int vmvwprintw(WINDOW *win, int y, int x, const char *fmt,
                      va_list args) {
  if (y < 0 || y >= win->y || x < 0 || x >= win->x) {
    return -1; // Out of bounds
  }

  int changed = vsnprintf(win->line_buffer, win->x - x, fmt, args);
  memcpy(win->lines[y] + x, win->line_buffer, changed); // without 0 at end
  for (int i = 0; i < changed; ++i) {
    set_cell_dirty(win, y, x + i);
  }

  win->cursor_x = x + changed + 1;
  win->cursor_y = y + 1;
  return changed;
}

int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vmvwprintw(win, y, x, fmt, args);
  va_end(args);
  return 0;
}

int mvprintw(int y, int x, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vmvwprintw(stdscr, y, x, fmt, args);
  va_end(args);
  return 0;
}

int mvwaddnstr(WINDOW *win, int y, int x, const char *str, int n) {
  for (int i = 0; i < n; ++i) {
    if (str[i] == '\0') {
      break; // Stop if we reach the end of the string or the window width
    }
    mvwaddch(win, y, x + i, str[i]);
  }
  return 0;
}

int mvaddnstr(int y, int x, const char *str, int n) {
  return mvwaddnstr(stdscr, y, x, str, n);
}

int mvaddstr(int y, int x, const char *str) {
  return mvwaddnstr(stdscr, y, x, str, stdscr->x - x);
}

int wmove(WINDOW *win, int y, int x) {
  if (y < 0 || y >= win->y || x < 0 || x >= win->x) {
    return -1; // Out of bounds
  }
  win->cursor_x = x + 1;
  win->cursor_y = y + 1;
  // printf("\033[%d;%dH", win->cursor_y, win->cursor_x);
  // fflush(stdout);
  return 0;
}

int move(int y, int x) {
  return wmove(stdscr, y, x);
}

int wclear(WINDOW *win) {
  win->cursor_x = 1;
  win->cursor_y = 1;
  for (int y = 0; y < win->y; ++y) {
    for (int x = 0; x < win->x; ++x) {
      if (win->lines[y][x] != ' ') {
        win->lines[y][x] = ' ';
        set_cell_dirty(win, y, x);
      }
    }
  }
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

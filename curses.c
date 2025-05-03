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
#include <stdio.h>
#include <stdlib.h>

#include <termios.h>

#define CLEAR_SCREEN "\033[2J"
#define CUROR_HIDE "\033[?25l"
#define CURSOR_SHOW "\033[?25h"
#define RESET_STYLE "\033[0m"

#define COLOR_ATTRIUTE (1 << 16)

WINDOW default_window = {};

WINDOW *stdscr = NULL;

struct termios original_stdout_termios = {};
struct termios current_stdout_termios = {};
struct termios original_stdin_termios = {};
struct termios current_stdin_termios = {};

int original_fcntl_flags = 0;

short color_pairs[16] = {0};

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
  color_pairs[pair] = (f << 4) | b;
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
  if (visibility == 0) {
    printf(CUROR_HIDE);
  } else if (visibility == 1) {
    printf(CURSOR_SHOW);
  } else {
    return -1;
  }
  return 0;
}

int nodelay(WINDOW *win, int bf) {
  int current_fcntl_flags = current_fcntl_flags | O_NONBLOCK;
  fcntl(STDIN_FILENO, F_SETFL, &current_fcntl_flags);
  return 0;
}

int getch(void) {
  int ch = 0;
  read(STDIN_FILENO, &ch, 1);
  return ch;
}

int mvaddch(int y, int x, const char ch) {
  printf("\033[%d;%dH%c", y, x, ch);
  return 0;
}

int attron(int attr) {
  if (attr & COLOR_ATTRIUTE) {
    int fg = ((attr >> 4) & 0xF) + 30;
    int bg = (attr & 0xF) + 40;

    printf("\033[%d;%dm\n", fg, bg);
  }
  return 0;
}

int attroff(int attr) {
  return 0;
}

void refresh(void) {
  fflush(stdout);
  return;
}

int napms(int ms) {
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

  return 0;
}

int COLOR_PAIR(int color) {
  if (color < 0 || color > 15) {
    return 0;
  }

  return COLOR_ATTRIUTE | color_pairs[color];
}

WINDOW *initscr(void) {
  default_window.x = 80;
  default_window.y = 24;
  stdscr = &default_window;
  tcgetattr(STDOUT_FILENO, &original_stdout_termios);
  tcgetattr(STDIN_FILENO, &original_stdin_termios);
  original_fcntl_flags = fcntl(STDIN_FILENO, F_GETFL, &original_fcntl_flags);

  current_stdout_termios = original_stdout_termios;
  current_stdin_termios = original_stdin_termios;

  printf(CLEAR_SCREEN);
  fflush(stdout);

  return &default_window;
}
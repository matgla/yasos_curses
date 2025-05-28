/**
 * ncurses.h
 *
 * Copyright (C) 2025 Mateusz Stadnik <matgla@live.com>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version
 * 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program. If not, see
 * <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>

#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7

#define KEY_LEFT 260
#define KEY_RIGHT 261
#define KEY_UP 258
#define KEY_DOWN 259
#define KEY_BACKSPACE 263

#define A_NORMAL 0
#define A_ITALIC (1 << 8)
#define A_BOLD (1 << 9)
#define A_BRIGHT (1 << 10)

int start_color(void);
int init_pair(short pair, short f, short b);

typedef struct {
  uint32_t color : 7;
  uint32_t color_enabled : 1;
  uint32_t bold : 1;
  uint32_t italic : 1;
  // uint8_t bold;
  // uint8_t italic;
  uint32_t clear : 1;
  uint32_t dirty : 1;
} Attribute;

typedef struct {
  int x;
  int y;
  int cursor_x;
  int cursor_y;
  char **lines;
  char *line_buffer;
  Attribute current_attribute;
  Attribute **attribute_map;
} WINDOW;

WINDOW *initscr(void);

void getmaxyx_(WINDOW *win, int *y, int *x);
#define getmaxyx(win, y, x) getmaxyx_(win, &y, &x)

int clear(void);
int raw(void);
int cbreak(void);
int noecho(void);
int curs_set(int visibility);
int nodelay(WINDOW *win, int bf);
int getch(void);
int mvaddch(int y, int x, const char ch);
int mvwaddch(WINDOW *win, int y, int x, const char ch);
int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...);
int mvprintw(int y, int x, const char *fmt, ...);
int mvaddstr(int y, int x, const char *str);
int mvaddnstr(int y, int x, const char *str, int n);
int move(int y, int x);
int attron(int attr);
int attroff(int attr);
int wattron(WINDOW *win, int attr);
int wattroff(WINDOW *win, int attr);
void refresh(void);
int napms(int ms);
int endwin(void);
WINDOW *newwin(int nlines, int ncols, int begin_y, int begin_x);
int box(WINDOW *win, int verch, int horch);
int wrefresh(WINDOW *win);

int COLOR_PAIR(int color);

int keypad(WINDOW *win, int bf);

int wclrtoeol(WINDOW *win);
#define clrtoeol() wclrtoeol(stdscr)

extern WINDOW *stdscr;

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

#define COLOR_PAIRS_COUNT 16
ColorPair color_pairs[COLOR_PAIRS_COUNT] = {0};

// Attributes currently active on the terminal. Every attribute change goes
// through write_attributes(), which emits one SGR sequence for the whole set,
// so this mirrors what the terminal was last told and lets a redundant
// attrset() (the editor issues one per drawn token) cost nothing.
static attr_t current_attributes = A_NORMAL;

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
  static int swallow_lf = 0;
  int ch = 0;
  int readed = read(STDIN_FILENO, &ch, 1);
  int i = 0;
  if (readed <= 0) {
    return -1;
  }
  // Normalize Enter across terminals to a single '\n'. In raw mode CR and LF
  // arrive unmodified, so a CRLF is two bytes; map CR to '\n' and swallow the
  // LF that immediately follows it, so one keypress is one line break whether
  // the terminal sends CR, LF or CRLF. The flag survives across calls (the LF
  // may arrive on a later non-blocking read) and is reset by any other byte.
  if (ch == '\r') {
    swallow_lf = 1;
    return '\n';
  }
  if (ch == '\n') {
    if (swallow_lf) {
      swallow_lf = 0;
      return getch();  // drop the LF half of a CRLF; fetch the next key
    }
    return '\n';
  }
  swallow_lf = 0;
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
  (void)win;
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

#define COLOR_PAIR_MASK (COLOR_ATTRIBUTE - 1)

static char *append_number(char *ptr, unsigned int value) {
  char digits[8];
  int n = 0;
  do {
    digits[n++] = (char)('0' + (value % 10));
    value /= 10;
  } while (value != 0);
  while (n-- > 0) {
    *ptr++ = digits[n];
  }
  return ptr;
}

// Emit a single SGR sequence describing the whole attribute set. attrset()
// semantics are "replace, don't add", so the sequence always opens with the
// reset parameter 0 and then re-states every attribute that is on; one write()
// per change keeps the serial traffic down compared to a code per attribute.
static void write_attributes(attr_t attrs) {
  char buffer[64];
  char *ptr = buffer;
  *ptr++ = '\033';
  *ptr++ = '[';
  *ptr++ = '0';
  if (attrs & A_BOLD) {
    *ptr++ = ';';
    *ptr++ = '1';
  }
  if (attrs & A_DIM) {
    *ptr++ = ';';
    *ptr++ = '2';
  }
  if (attrs & A_ITALIC) {
    *ptr++ = ';';
    *ptr++ = '3';
  }
  if (attrs & A_UNDERLINE) {
    *ptr++ = ';';
    *ptr++ = '4';
  }
  if (attrs & A_REVERSE) {
    *ptr++ = ';';
    *ptr++ = '7';
  }
  if (attrs & COLOR_ATTRIBUTE) {
    const unsigned int pair_index = attrs & COLOR_PAIR_MASK;
    if (pair_index < COLOR_PAIRS_COUNT) {
      unsigned int fg = color_pairs[pair_index].fg;
      const unsigned int bg = color_pairs[pair_index].bg;
      if (attrs & A_BRIGHT) {
        fg |= 8;
      }
      // 0-7 are the base colours (30-37 / 40-47), 8-15 the bright aixterm
      // variants (90-97 / 100-107).
      *ptr++ = ';';
      ptr = append_number(ptr, fg < 8 ? fg + 30 : fg - 8 + 90);
      *ptr++ = ';';
      ptr = append_number(ptr, bg < 8 ? bg + 40 : bg - 8 + 100);
    }
  }
  *ptr++ = 'm';
  write(STDOUT_FILENO, buffer, (size_t)(ptr - buffer));
}

static int set_attributes(attr_t attrs) {
  if (attrs != current_attributes) {
    current_attributes = attrs;
    write_attributes(attrs);
  }
  return 0;
}

int wattrset(WINDOW *win, attr_t attrs) {
  (void)win;
  return set_attributes(attrs);
}

int attrset(attr_t attrs) {
  return wattrset(stdscr, attrs);
}

int wattron(WINDOW *win, int attr) {
  (void)win;
  attr_t attrs = current_attributes | (attr_t)attr;
  // A colour pair replaces the active pair; OR-ing the pair numbers together
  // would name a third, unrelated pair.
  if ((attr_t)attr & COLOR_ATTRIBUTE) {
    attrs = (attrs & ~(attr_t)COLOR_PAIR_MASK) | ((attr_t)attr & COLOR_PAIR_MASK);
  }
  return set_attributes(attrs);
}

int wattroff(WINDOW *win, int attr) {
  (void)win;
  attr_t attrs = current_attributes & ~(attr_t)attr;
  if ((attr_t)attr & COLOR_ATTRIBUTE) {
    attrs &= ~(attr_t)COLOR_PAIR_MASK; // dropping the pair drops its number too
  }
  return set_attributes(attrs);
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
  fcntl(STDIN_FILENO, F_SETFL, original_fcntl_flags);
  write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_LEN);
  write(STDOUT_FILENO, CURSOR_SHOW, CURSOR_SHOW_LEN);
  write(STDOUT_FILENO, RESET_STYLE, RESET_STYLE_LEN);
  current_attributes = A_NORMAL; // RESET_STYLE just cleared them on the wire
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

// Detect the real terminal size over a serial line via the Cursor Position
// Report handshake: park the cursor at the far bottom-right (the terminal
// clamps the move to its actual extent), ask for the cursor position with
// ESC[6n, and parse the ESC[<rows>;<cols>R reply. A raw serial line carries no
// size and the kernel's TIOCGWINSZ is hardcoded, so this is the only way to
// learn the geometry. Updates stdscr on success and leaves it untouched when
// the terminal doesn't answer (dumb pipe / no CPR support) -- callers keep the
// 24x80 default. Needs raw, non-blocking input, set up and restored here.
static void detect_terminal_size(void) {
  struct termios saved, raw_mode;
  if (tcgetattr(STDIN_FILENO, &saved) != 0) {
    return;
  }
  raw_mode = saved;
  raw_mode.c_lflag &= ~(ICANON | ECHO);
  raw_mode.c_oflag = 0;  // kernel "raw" mode: deliver the reply bytes verbatim
  raw_mode.c_iflag &= ~(ICRNL | INLCR | IGNCR);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_mode);
  int saved_flags = fcntl(STDIN_FILENO, F_GETFL);
  fcntl(STDIN_FILENO, F_SETFL, saved_flags | O_NONBLOCK);

  static const char query[] = "\033[999;999H\033[6n";
  write(STDOUT_FILENO, query, sizeof(query) - 1);

  char buf[32];
  int n = 0;
  // Poll up to ~250ms; stop as soon as the reply terminator 'R' arrives.
  for (int tries = 0; tries < 250 && n < (int)sizeof(buf) - 1; ++tries) {
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1) {
      buf[n++] = c;
      if (c == 'R') {
        break;
      }
    } else {
      struct timespec req = {0, 1000000};  // 1ms
      nanosleep(&req, NULL);
    }
  }
  buf[n] = '\0';

  fcntl(STDIN_FILENO, F_SETFL, saved_flags);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);

  // Parse "ESC [ rows ; cols R".
  int rows = 0, cols = 0;
  const char *p = buf;
  while (*p && *p != '\033') {
    p++;
  }
  if (p[0] == '\033' && p[1] == '[') {
    p += 2;
    while (*p >= '0' && *p <= '9') {
      rows = rows * 10 + (*p++ - '0');
    }
    if (*p == ';') {
      p++;
      while (*p >= '0' && *p <= '9') {
        cols = cols * 10 + (*p++ - '0');
      }
    }
  }
  if (rows >= 2 && rows <= 1000 && cols >= 2 && cols <= 1000) {
    stdscr->y = rows;
    stdscr->x = cols;
  }
}

WINDOW *initscr(void) {
  if (stdscr != NULL) {
    endwin(); // Clean up previous window
  }
  stdscr = newwin(24, 80, 0, 0);
  tcgetattr(STDOUT_FILENO, &original_stdout_termios);
  tcgetattr(STDIN_FILENO, &original_stdin_termios);
  original_fcntl_flags = fcntl(STDIN_FILENO, F_GETFL);

  current_stdout_termios = original_stdout_termios;
  current_stdin_termios = original_stdin_termios;

  write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_LEN);
  write(STDOUT_FILENO, RESET_STYLE, RESET_STYLE_LEN);
  current_attributes = A_NORMAL;
  detect_terminal_size();
  write(STDOUT_FILENO, CURSOR_HOME, CURSOR_HOME_LEN);

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
  // Enter true raw mode. The yasos UART driver treats c_oflag==0 as "raw" and
  // otherwise maps CR->NL on input; left non-raw, a CRLF-sending terminal
  // delivers two newlines per Enter (one extra blank line). Clear c_oflag (and
  // the CR/NL input maps) so Enter arrives as its raw byte(s) and getch()
  // normalizes CR / LF / CRLF to a single '\n'. endwin() restores the original
  // termios, so the shell keeps its cooked output afterwards.
  current_stdout_termios.c_oflag = 0;
  current_stdin_termios.c_oflag = 0;
  current_stdin_termios.c_iflag &= ~(ICRNL | INLCR | IGNCR);
  tcsetattr(STDOUT_FILENO, TCSANOW, &current_stdout_termios);
  tcsetattr(STDIN_FILENO, TCSANOW, &current_stdin_termios);
  return 0;
}

int wclrtoeol(WINDOW *win) {
  write(STDOUT_FILENO, CLEAR_TO_EOL, CLEAR_TO_EOL_LEN);
  return 0;
}

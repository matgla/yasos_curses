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
#include <stdbool.h>
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

/* ------------------------------------------------------------------------ */
/* Output buffer                                                            */
/*                                                                          */
/* Everything this library sends to the terminal is staged here and written  */
/* in one go. A serial console is the slow part of every draw, and one       */
/* write() per character (which is what this library used to do) both costs  */
/* a syscall each and lets the terminal render a half-finished screen.       */
/* ------------------------------------------------------------------------ */

#define OUTPUT_BUFFER_SIZE 512

static char output_buffer[OUTPUT_BUFFER_SIZE];
static size_t output_length = 0;

static void output_flush(void) {
  if (output_length != 0) {
    write(STDOUT_FILENO, output_buffer, output_length);
    output_length = 0;
  }
}

static void output_char(char c) {
  if (output_length == OUTPUT_BUFFER_SIZE) {
    output_flush();
  }
  output_buffer[output_length++] = c;
}

static void output_string(const char *str, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    output_char(str[i]);
  }
}

/* ------------------------------------------------------------------------ */
/* Screen buffers                                                           */
/*                                                                          */
/* A cell holds the character an application drew plus the attributes that   */
/* were active when it did. Attributes live in the low 14 bits of attr_t (7  */
/* bits of colour pair, the "pair is set" flag and the display bits), so a   */
/* uint16_t carries all of them and keeps a cell at four bytes.              */
/*                                                                          */
/* The virtual screen is what the application has drawn since the last       */
/* refresh; the physical screen is what the terminal is currently showing.   */
/* refresh() emits the difference and nothing else, so an application that   */
/* repaints the whole screen on every keystroke -- clearing a status bar and */
/* painting the same text back over it, say -- costs no terminal traffic at  */
/* all where the result did not change. That is what stops a redrawn bar     */
/* from visibly blanking and refilling.                                      */
/* ------------------------------------------------------------------------ */

// The distinct attribute sets an application actually uses are few -- a colour
// pair per syntax token, plus bold and dim -- so a cell carries a one-byte
// index into a table of them rather than the attributes themselves. That keeps
// the two screen buffers at two bytes a cell, which is worth having on a board
// with half a megabyte of RAM. Interning happens in attrset(), not per drawn
// character, so the lookup costs nothing on the hot path.
#define ATTRIBUTE_TABLE_SIZE 256

static attr_t attribute_table[ATTRIBUTE_TABLE_SIZE] = {A_NORMAL};
static int attribute_table_used = 1;

static uint8_t attribute_intern(attr_t attrs) {
  for (int i = 0; i < attribute_table_used; ++i) {
    if (attribute_table[i] == attrs) {
      return (uint8_t)i;
    }
  }
  if (attribute_table_used < ATTRIBUTE_TABLE_SIZE) {
    attribute_table[attribute_table_used] = attrs;
    return (uint8_t)(attribute_table_used++);
  }
  return 0;  // table full: plain text beats mis-coloured text
}

typedef struct {
  uint8_t character;
  uint8_t attributes;
} Cell;

// The two screens are compared with memcmp, so a cell must not carry padding:
// uninitialised padding bytes would report changes that are not there.
_Static_assert(sizeof(Cell) == 2, "a screen cell must be free of padding");

#define BLANK_CHARACTER ' '

/* Repainting this many unchanged cells is cheaper than the cursor-position
   escape it would take to skip over them, so short gaps inside a changed run
   are just redrawn. */
#define CURSOR_MOVE_COST 6

static Cell *virtual_screen = NULL;
static Cell *physical_screen = NULL;
static int screen_rows = 0;
static int screen_columns = 0;

/* Where the next drawing call writes, moved by wmove() and advanced by the
   character-emitting calls. */
static int cursor_y = 0;
static int cursor_x = 0;

/* Where the terminal's own cursor is, or -1 when that is unknown (nothing has
   been written yet, or the last write left the cursor in the pending-wrap
   state at the end of a line). */
static int terminal_y = -1;
static int terminal_x = -1;

/* The attributes drawing calls stamp into cells (kept both as the value and as
   its table index), and the SGR state the terminal was last told about. */
static attr_t pen_attributes = A_NORMAL;
static uint8_t pen_index = 0;
static attr_t terminal_attributes = A_NORMAL;

/* Set by clear(): the next refresh erases the terminal before diffing. */
static bool full_repaint_pending = false;

static Cell blank_cell(void) {
  Cell cell;
  cell.character = (uint8_t)BLANK_CHARACTER;
  cell.attributes = 0;  // entry 0 of the table is always A_NORMAL
  return cell;
}

static bool cells_equal(const Cell *a, const Cell *b) {
  return a->character == b->character && a->attributes == b->attributes;
}

static void screen_fill(Cell *screen, Cell cell) {
  const int count = screen_rows * screen_columns;
  for (int i = 0; i < count; ++i) {
    screen[i] = cell;
  }
}

static void screen_free(void) {
  free(virtual_screen);
  free(physical_screen);
  virtual_screen = NULL;
  physical_screen = NULL;
}

// Allocate the two screen buffers. Returns false when there is no memory for
// them, in which case the drawing calls fall back to painting straight through
// to the terminal -- correct, just without the "only changed cells" saving.
static bool screen_allocate(int rows, int columns) {
  screen_free();
  screen_rows = rows;
  screen_columns = columns;
  const size_t count = (size_t)rows * (size_t)columns;
  virtual_screen = malloc(count * sizeof(Cell));
  physical_screen = malloc(count * sizeof(Cell));
  if (virtual_screen == NULL || physical_screen == NULL) {
    screen_free();
    return false;
  }
  screen_fill(virtual_screen, blank_cell());
  screen_fill(physical_screen, blank_cell());
  return true;
}

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

static void output_cursor_move(int y, int x) {
  char sequence[24];
  char *ptr = sequence;
  *ptr++ = '\033';
  *ptr++ = '[';
  ptr = append_number(ptr, (unsigned int)(y + 1));
  *ptr++ = ';';
  ptr = append_number(ptr, (unsigned int)(x + 1));
  *ptr++ = 'H';
  output_string(sequence, (size_t)(ptr - sequence));
  terminal_y = y;
  terminal_x = x;
}

// Emit a single SGR sequence describing the whole attribute set. attrset()
// semantics are "replace, don't add", so the sequence always opens with the
// reset parameter 0 and then re-states every attribute that is on; one escape
// per change keeps the serial traffic down compared to a code per attribute.
static void output_attributes(attr_t attrs) {
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
    const unsigned int pair_index = attrs & (COLOR_ATTRIBUTE - 1);
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
  output_string(buffer, (size_t)(ptr - buffer));
  terminal_attributes = attrs;
}

// Stamp one character into the virtual screen. Without the buffers (allocation
// failed) it goes straight to the terminal, which is what this library did
// before double buffering.
static void screen_put(int y, int x, char ch) {
  if (y < 0 || y >= screen_rows || x < 0 || x >= screen_columns) {
    return;
  }
  if (virtual_screen != NULL) {
    Cell *cell = &virtual_screen[y * screen_columns + x];
    cell->character = (uint8_t)ch;
    cell->attributes = pen_index;
    return;
  }
  if (terminal_y != y || terminal_x != x) {
    output_cursor_move(y, x);
  }
  if (terminal_attributes != pen_attributes) {
    output_attributes(pen_attributes);
  }
  output_char(ch);
  terminal_x++;
  output_flush();
}

static void cursor_advance(void) {
  if (++cursor_x >= screen_columns) {
    cursor_x = 0;
    if (cursor_y + 1 < screen_rows) {
      cursor_y++;
    }
  }
}

// Paint one row's worth of changed cells, from run_start up to (not including)
// run_end, and record them as displayed.
static void screen_emit_run(int y, int run_start, int run_end) {
  Cell *virtual_row = &virtual_screen[y * screen_columns];
  Cell *physical_row = &physical_screen[y * screen_columns];

  if (terminal_y != y || terminal_x != run_start) {
    output_cursor_move(y, run_start);
  }
  for (int x = run_start; x < run_end; ++x) {
    const attr_t attributes = attribute_table[virtual_row[x].attributes];
    if (attributes != terminal_attributes) {
      output_attributes(attributes);
    }
    output_char((char)virtual_row[x].character);
    physical_row[x] = virtual_row[x];
  }
  terminal_x = run_end;
  if (terminal_x >= screen_columns) {
    // The terminal is in the pending-wrap state; the next absolute move
    // cancels it, but until then its cursor position is not ours to predict.
    terminal_x = -1;
    terminal_y = -1;
  }
}

// Send the difference between the virtual and physical screens, then park the
// terminal cursor where the application left it.
static void screen_update(void) {
  if (virtual_screen == NULL) {
    output_flush();
    return;
  }

  const size_t screen_bytes =
      (size_t)screen_rows * (size_t)screen_columns * sizeof(Cell);
  const bool contents_differ =
      memcmp(virtual_screen, physical_screen, screen_bytes) != 0;
  const bool cursor_misplaced =
      terminal_y != cursor_y || terminal_x != cursor_x;

  if (!full_repaint_pending && !contents_differ && !cursor_misplaced) {
    return;  // nothing on screen changed: not a single byte goes out
  }

  // Hide the cursor while painting so it does not visibly run over the screen,
  // and only when the application wanted it visible in the first place.
  if (cursor_enabled) {
    output_string(CURSOR_HIDE, CURSOR_HIDE_LEN);
  }

  if (full_repaint_pending) {
    output_attributes(A_NORMAL);
    output_string(CLEAR_SCREEN, CLEAR_SCREEN_LEN);
    screen_fill(physical_screen, blank_cell());
    terminal_y = -1;
    terminal_x = -1;
    full_repaint_pending = false;
  }

  for (int y = 0; y < screen_rows; ++y) {
    const Cell *virtual_row = &virtual_screen[y * screen_columns];
    const Cell *physical_row = &physical_screen[y * screen_columns];
    int x = 0;
    while (x < screen_columns) {
      if (cells_equal(&virtual_row[x], &physical_row[x])) {
        ++x;
        continue;
      }
      // Extend the run past short stretches of unchanged cells: repainting
      // them costs less than the escape needed to jump over them.
      int run_end = x + 1;
      int unchanged = 0;
      for (int scan = x; scan < screen_columns; ++scan) {
        if (!cells_equal(&virtual_row[scan], &physical_row[scan])) {
          run_end = scan + 1;
          unchanged = 0;
        } else if (++unchanged > CURSOR_MOVE_COST) {
          break;
        }
      }
      screen_emit_run(y, x, run_end);
      x = run_end;
    }
  }

  if (terminal_y != cursor_y || terminal_x != cursor_x) {
    output_cursor_move(cursor_y, cursor_x);
  }
  if (cursor_enabled) {
    output_string(CURSOR_SHOW, CURSOR_SHOW_LEN);
  }
  output_flush();
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
  if (visibility != 0 && visibility != 1) {
    return -1;
  }
  cursor_enabled = visibility;
  output_flush();
  if (visibility == 0) {
    write(STDOUT_FILENO, CURSOR_HIDE, CURSOR_HIDE_LEN);
  } else {
    write(STDOUT_FILENO, CURSOR_SHOW, CURSOR_SHOW_LEN);
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
  screen_put(cursor_y, cursor_x, ch);
  cursor_advance();
  return 0;
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

static int set_attributes(attr_t attrs) {
  if (attrs != pen_attributes) {
    pen_attributes = attrs;
    pen_index = attribute_intern(attrs);
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
  attr_t attrs = pen_attributes | (attr_t)attr;
  // A colour pair replaces the active pair; OR-ing the pair numbers together
  // would name a third, unrelated pair.
  if ((attr_t)attr & COLOR_ATTRIBUTE) {
    attrs = (attrs & ~(attr_t)(COLOR_ATTRIBUTE - 1)) |
            ((attr_t)attr & (COLOR_ATTRIBUTE - 1));
  }
  return set_attributes(attrs);
}

int wattroff(WINDOW *win, int attr) {
  (void)win;
  attr_t attrs = pen_attributes & ~(attr_t)attr;
  if ((attr_t)attr & COLOR_ATTRIBUTE) {
    // dropping the pair drops its number too
    attrs &= ~(attr_t)(COLOR_ATTRIBUTE - 1);
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
  output_flush();
  tcsetattr(STDOUT_FILENO, TCSAFLUSH, &original_stdout_termios);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_stdin_termios);
  fcntl(STDIN_FILENO, F_SETFL, original_fcntl_flags);
  write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_LEN);
  write(STDOUT_FILENO, CURSOR_SHOW, CURSOR_SHOW_LEN);
  write(STDOUT_FILENO, RESET_STYLE, RESET_STYLE_LEN);
  terminal_attributes = A_NORMAL; // RESET_STYLE just cleared them on the wire
  pen_attributes = A_NORMAL;
  pen_index = 0;
  terminal_y = -1;
  terminal_x = -1;
  screen_free();
  screen_rows = 0;
  screen_columns = 0;
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
  attribute_table[0] = A_NORMAL;
  attribute_table_used = 1;
  pen_attributes = A_NORMAL;
  pen_index = 0;
  terminal_attributes = A_NORMAL;
  detect_terminal_size();
  write(STDOUT_FILENO, CURSOR_HOME, CURSOR_HOME_LEN);

  // The screen was just cleared and the cursor parked at the top left, so both
  // buffers start out matching what the terminal shows.
  screen_allocate(stdscr->y, stdscr->x);
  screen_rows = stdscr->y;
  screen_columns = stdscr->x;
  cursor_y = 0;
  cursor_x = 0;
  terminal_y = 0;
  terminal_x = 0;
  full_repaint_pending = false;

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
  (void)win;
  screen_update();
  return 0;
}

// Common tail of the string-drawing calls: stamp len characters starting at
// (y, x) and leave the cursor just past them.
static int screen_add_string(WINDOW *win, int y, int x, const char *str,
                             int len) {
  if (len > win->x - x) {
    len = win->x - x;
  }
  if (len < 0) {
    len = 0;
  }
  if (wmove(win, y, x) != 0) {
    return 0;
  }
  for (int i = 0; i < len; ++i) {
    screen_put(y, x + i, str[i]);
  }
  cursor_y = y;
  cursor_x = x + len;
  if (cursor_x >= screen_columns) {
    // Same wrap the character-at-a-time path takes when it runs off the row.
    cursor_x = 0;
    if (cursor_y + 1 < screen_rows) {
      cursor_y++;
    }
  }
  return len;
}

int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  if (len < 0) {
    return -1;
  }
  if (len > (int)sizeof(buffer) - 1) {
    len = (int)sizeof(buffer) - 1;
  }
  return screen_add_string(win, y, x, buffer, len);
}

int mvprintw(int y, int x, const char *fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  if (len < 0) {
    return -1;
  }
  if (len > (int)sizeof(buffer) - 1) {
    len = (int)sizeof(buffer) - 1;
  }
  return screen_add_string(stdscr, y, x, buffer, len);
}

int mvwaddnstr(WINDOW *win, int y, int x, const char *str, int n) {
  return screen_add_string(win, y, x, str, (int)strnlen(str, (size_t)n));
}

int mvwaddstr(WINDOW *win, int y, int x, const char *str) {
  return screen_add_string(win, y, x, str, (int)strlen(str));
}

int wmove(WINDOW *win, int y, int x) {
  if (y < 0 || y >= win->y || x < 0 || x >= win->x) {
    return -1; // Out of bounds
  }
  cursor_y = y;
  cursor_x = x;
  return 0;
}

int wclear(WINDOW *win) {
  (void)win;
  // TODO: erase only window content
  if (virtual_screen != NULL) {
    screen_fill(virtual_screen, blank_cell());
  }
  cursor_y = 0;
  cursor_x = 0;
  full_repaint_pending = true;
  if (virtual_screen == NULL) {
    output_flush();
    write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_LEN);
    terminal_y = -1;
    terminal_x = -1;
  }
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
  (void)win;
  if (virtual_screen == NULL) {
    output_flush();
    write(STDOUT_FILENO, CLEAR_TO_EOL, CLEAR_TO_EOL_LEN);
    return 0;
  }
  if (cursor_y < 0 || cursor_y >= screen_rows) {
    return 0;
  }
  Cell *row = &virtual_screen[cursor_y * screen_columns];
  for (int x = cursor_x; x < screen_columns; ++x) {
    row[x] = blank_cell();
  }
  return 0;
}

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

#include "utest.h"

#include "terminal_model.h"

#include <ncurses.h>

#include <string.h>

// stdout is a file here, so the terminal-size handshake gets no reply and
// stdscr keeps its 24x80 default -- the geometry the model mirrors.
typedef struct {
  OutputCapture capture;
  TerminalModel model;
  char bytes[65536];
  size_t last_frame_length;
} ScreenFixture;

static void screen_begin(ScreenFixture *fixture) {
  capture_begin(&fixture->capture);
  initscr();
  model_reset(&fixture->model);
  fixture->last_frame_length =
      capture_take(&fixture->capture, fixture->bytes, sizeof(fixture->bytes));
  model_feed(&fixture->model, fixture->bytes, fixture->last_frame_length);
}

static void screen_end(ScreenFixture *fixture) {
  endwin();
  capture_end(&fixture->capture);
}

// Replay one refresh() worth of output into the model and report how many bytes
// it took.
static size_t screen_frame(ScreenFixture *fixture) {
  refresh();
  fixture->last_frame_length =
      capture_take(&fixture->capture, fixture->bytes, sizeof(fixture->bytes));
  fixture->model.painted_characters = 0;
  model_feed(&fixture->model, fixture->bytes, fixture->last_frame_length);
  return fixture->last_frame_length;
}

// A status bar drawn the way yasvi draws its toolbar: blank the row first, then
// paint the widgets back over it.
static void draw_status_bar(int row, const char *mode, const char *filename) {
  attrset(A_NORMAL);
  move(row, 0);
  for (int x = 0; x < 80; ++x) {
    addch(' ');
  }
  attrset(A_BOLD);
  mvaddnstr(row, 0, mode, (int)strlen(mode));
  attrset(A_NORMAL);
  mvaddnstr(row, 14, filename, (int)strlen(filename));
}

UTEST(screen, first_paint_reaches_the_terminal) {
  ScreenFixture fixture;
  screen_begin(&fixture);

  draw_status_bar(23, "-- NORMAL --", "hello.c");
  screen_frame(&fixture);

  EXPECT_STREQ("-- NORMAL --  hello.c", model_row(&fixture.model, 23));
  screen_end(&fixture);
}

UTEST(screen, redrawing_the_same_content_writes_nothing) {
  ScreenFixture fixture;
  screen_begin(&fixture);

  draw_status_bar(23, "-- NORMAL --", "hello.c");
  screen_frame(&fixture);

  // The blank-then-repaint above lands on cells that already hold exactly that,
  // so the second frame must not put a single byte on the wire. This is what
  // keeps a repainted bar from visibly flickering.
  draw_status_bar(23, "-- NORMAL --", "hello.c");
  EXPECT_EQ(0u, screen_frame(&fixture));

  EXPECT_STREQ("-- NORMAL --  hello.c", model_row(&fixture.model, 23));
  screen_end(&fixture);
}

UTEST(screen, one_changed_character_repaints_one_character) {
  ScreenFixture fixture;
  screen_begin(&fixture);

  draw_status_bar(23, "-- NORMAL --", "hello.c");
  screen_frame(&fixture);

  draw_status_bar(23, "-- NORMAL --", "hellp.c");
  screen_frame(&fixture);

  EXPECT_EQ(1, fixture.model.painted_characters);
  EXPECT_STREQ("-- NORMAL --  hellp.c", model_row(&fixture.model, 23));
  screen_end(&fixture);
}

UTEST(screen, short_gaps_inside_a_run_are_repainted_not_skipped) {
  ScreenFixture fixture;
  screen_begin(&fixture);

  mvaddstr(0, 0, "aXbbbbbbbbbbXc");
  screen_frame(&fixture);

  // Two changes three columns apart: jumping between them costs more escape
  // bytes than repainting the untouched cells, so the run is emitted whole.
  mvaddstr(0, 0, "aYbbbYbbbbbbXc");
  screen_frame(&fixture);
  EXPECT_EQ(5, fixture.model.painted_characters);

  // Far apart, the cursor move wins and only the two cells are repainted.
  mvaddstr(0, 0, "aZbbbYbbbbbbZc");
  screen_frame(&fixture);
  EXPECT_EQ(2, fixture.model.painted_characters);

  EXPECT_STREQ("aZbbbYbbbbbbZc", model_row(&fixture.model, 0));
  screen_end(&fixture);
}

UTEST(screen, attributes_land_on_the_cells_that_were_drawn_with_them) {
  ScreenFixture fixture;
  screen_begin(&fixture);

  attrset(A_BOLD);
  mvaddstr(1, 0, "bold");
  attrset(A_NORMAL);
  mvaddstr(1, 4, "plain");
  screen_frame(&fixture);

  EXPECT_TRUE(fixture.model.cells[1][0].bold);
  EXPECT_TRUE(fixture.model.cells[1][3].bold);
  EXPECT_FALSE(fixture.model.cells[1][4].bold);
  EXPECT_STREQ("boldplain", model_row(&fixture.model, 1));

  // An attribute change with no content change still has to reach the wire.
  attrset(A_NORMAL);
  mvaddstr(1, 0, "bold");
  screen_frame(&fixture);
  EXPECT_FALSE(fixture.model.cells[1][0].bold);

  screen_end(&fixture);
}

UTEST(screen, attrset_alone_writes_nothing) {
  ScreenFixture fixture;
  screen_begin(&fixture);

  mvaddstr(2, 0, "text");
  screen_frame(&fixture);

  // Attributes are a property of the cells drawn next, not a command; setting
  // one without drawing must not cost an escape sequence. yasvi issues one
  // attrset per highlighted token, most of them redundant.
  attrset(A_BOLD);
  attrset(A_NORMAL);
  attron(A_DIM);
  attroff(A_DIM);
  EXPECT_EQ(0u, screen_frame(&fixture));

  screen_end(&fixture);
}

UTEST(screen, clear_erases_the_screen_and_repaints_what_is_left) {
  ScreenFixture fixture;
  screen_begin(&fixture);

  mvaddstr(3, 0, "before");
  mvaddstr(4, 0, "gone");
  screen_frame(&fixture);

  clear();
  mvaddstr(3, 0, "after");
  screen_frame(&fixture);

  EXPECT_STREQ("after", model_row(&fixture.model, 3));
  EXPECT_STREQ("", model_row(&fixture.model, 4));
  screen_end(&fixture);
}

UTEST(screen, clrtoeol_blanks_the_rest_of_the_row) {
  ScreenFixture fixture;
  screen_begin(&fixture);

  mvaddstr(5, 0, "keep this and drop the rest");
  screen_frame(&fixture);

  move(5, 10);
  clrtoeol();
  screen_frame(&fixture);

  EXPECT_STREQ("keep this", model_row(&fixture.model, 5));
  screen_end(&fixture);
}

UTEST(screen, mvprintw_formats_into_the_screen) {
  ScreenFixture fixture;
  screen_begin(&fixture);

  mvprintw(6, 2, "%d:%d/%d", 12, 34, 567);
  screen_frame(&fixture);

  EXPECT_STREQ("  12:34/567", model_row(&fixture.model, 6));
  screen_end(&fixture);
}

UTEST(screen, drawing_is_clipped_to_the_screen) {
  ScreenFixture fixture;
  screen_begin(&fixture);

  // Runs off the right edge: the tail is dropped rather than wrapping onto the
  // next row and corrupting it.
  mvaddstr(7, 75, "0123456789");
  mvaddstr(8, 0, "next row");
  screen_frame(&fixture);

  EXPECT_STREQ("                                                              "
               "             01234",
               model_row(&fixture.model, 7));
  EXPECT_STREQ("next row", model_row(&fixture.model, 8));
  screen_end(&fixture);
}

UTEST(screen, the_cursor_ends_where_the_application_left_it) {
  ScreenFixture fixture;
  screen_begin(&fixture);

  mvaddstr(9, 0, "line");
  move(9, 2);
  screen_frame(&fixture);

  EXPECT_EQ(9, fixture.model.y);
  EXPECT_EQ(2, fixture.model.x);

  // A cursor move with no content change is still worth a frame.
  move(11, 7);
  screen_frame(&fixture);
  EXPECT_EQ(11, fixture.model.y);
  EXPECT_EQ(7, fixture.model.x);

  screen_end(&fixture);
}

// A deterministic pseudo-random shuffle of the screen, checked against a
// reference kept alongside: whatever the diff decides to emit, the terminal has
// to end up holding exactly what was drawn.
UTEST(screen, random_draws_leave_the_terminal_matching_what_was_drawn) {
  ScreenFixture fixture;
  screen_begin(&fixture);

  char reference[MODEL_ROWS][MODEL_COLUMNS];
  memset(reference, ' ', sizeof(reference));

  unsigned int seed = 20250831u;
  for (int frame = 0; frame < 40; ++frame) {
    const int edits = 1 + (int)(seed % 20u);
    for (int edit = 0; edit < edits; ++edit) {
      seed = seed * 1103515245u + 12345u;
      const int y = (int)((seed >> 16) % MODEL_ROWS);
      seed = seed * 1103515245u + 12345u;
      const int x = (int)((seed >> 16) % MODEL_COLUMNS);
      seed = seed * 1103515245u + 12345u;
      const int length = 1 + (int)((seed >> 16) % 12u);

      char text[16];
      for (int i = 0; i < length; ++i) {
        seed = seed * 1103515245u + 12345u;
        text[i] = (char)('a' + (seed >> 16) % 26u);
      }
      text[length] = '\0';

      mvaddstr(y, x, text);
      for (int i = 0; i < length && x + i < MODEL_COLUMNS; ++i) {
        reference[y][x + i] = text[i];
      }
    }
    screen_frame(&fixture);

    for (int y = 0; y < MODEL_ROWS; ++y) {
      for (int x = 0; x < MODEL_COLUMNS; ++x) {
        ASSERT_EQ(reference[y][x], fixture.model.cells[y][x].character);
      }
    }
  }

  screen_end(&fixture);
}

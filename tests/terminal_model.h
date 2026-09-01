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

// A terminal just large enough to check what the library puts on the wire:
// stdout is captured into a file, replayed through the escape sequences this
// library emits (cursor position, erase, SGR) and the resulting screen is
// compared against what the drawing calls asked for. Only the sequences the
// library itself produces are understood; anything else is skipped.

#pragma once

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MODEL_ROWS 24
#define MODEL_COLUMNS 80

typedef struct {
  char character;
  int bold;
} ModelCell;

typedef struct {
  ModelCell cells[MODEL_ROWS][MODEL_COLUMNS];
  int y;
  int x;
  int bold;
  int cursor_visible;
  // Printable characters replayed since the last reset: the measure of how
  // much the library actually repainted.
  int painted_characters;
} TerminalModel;

typedef struct {
  int fd;
  int saved_stdout;
  off_t mark;
  char path[64];
} OutputCapture;

static void capture_begin(OutputCapture *capture) {
  snprintf(capture->path, sizeof(capture->path), "build/capture-%d.out",
           (int)getpid());
  capture->fd = open(capture->path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  capture->saved_stdout = dup(STDOUT_FILENO);
  dup2(capture->fd, STDOUT_FILENO);
  capture->mark = 0;
}

static void capture_end(OutputCapture *capture) {
  dup2(capture->saved_stdout, STDOUT_FILENO);
  close(capture->saved_stdout);
  close(capture->fd);
  unlink(capture->path);
}

// Everything written since the previous take, so a test can measure one frame.
static size_t capture_take(OutputCapture *capture, char *out, size_t max) {
  const off_t end = lseek(capture->fd, 0, SEEK_END);
  size_t available = (size_t)(end - capture->mark);
  if (available > max - 1) {
    available = max - 1;
  }
  lseek(capture->fd, capture->mark, SEEK_SET);
  ssize_t taken = read(capture->fd, out, available);
  if (taken < 0) {
    taken = 0;
  }
  out[taken] = '\0';
  capture->mark = end;
  lseek(capture->fd, 0, SEEK_END);
  return (size_t)taken;
}

static void model_reset(TerminalModel *model) {
  for (int y = 0; y < MODEL_ROWS; ++y) {
    for (int x = 0; x < MODEL_COLUMNS; ++x) {
      model->cells[y][x].character = ' ';
      model->cells[y][x].bold = 0;
    }
  }
  model->y = 0;
  model->x = 0;
  model->bold = 0;
  model->cursor_visible = 1;
  model->painted_characters = 0;
}

static void model_feed(TerminalModel *model, const char *bytes, size_t length) {
  size_t i = 0;
  while (i < length) {
    if (bytes[i] != '\033') {
      if (model->y < MODEL_ROWS && model->x < MODEL_COLUMNS) {
        model->cells[model->y][model->x].character = bytes[i];
        model->cells[model->y][model->x].bold = model->bold;
      }
      model->painted_characters++;
      if (++model->x >= MODEL_COLUMNS) {
        model->x = 0;
        if (model->y + 1 < MODEL_ROWS) {
          model->y++;
        }
      }
      i++;
      continue;
    }

    i++;  // ESC
    if (i >= length || bytes[i] != '[') {
      continue;
    }
    i++;  // [

    int parameters[8] = {0};
    int count = 0;
    int private_marker = 0;
    int seen_digits = 0;
    while (i < length && ((bytes[i] >= '0' && bytes[i] <= '9') ||
                          bytes[i] == ';' || bytes[i] == '?')) {
      if (bytes[i] == '?') {
        private_marker = 1;
      } else if (bytes[i] == ';') {
        if (count < 7) {
          count++;
        }
        seen_digits = 1;
      } else {
        parameters[count] = parameters[count] * 10 + (bytes[i] - '0');
        seen_digits = 1;
      }
      i++;
    }
    const char final = (i < length) ? bytes[i++] : '\0';

    switch (final) {
      case 'H':
        model->y = seen_digits && parameters[0] > 0 ? parameters[0] - 1 : 0;
        model->x = (count >= 1 && parameters[1] > 0) ? parameters[1] - 1 : 0;
        if (model->y >= MODEL_ROWS) {
          model->y = MODEL_ROWS - 1;
        }
        if (model->x >= MODEL_COLUMNS) {
          model->x = MODEL_COLUMNS - 1;
        }
        break;
      case 'J':
        if (parameters[0] == 2) {
          const int y = model->y, x = model->x;
          model_reset(model);
          model->y = y;
          model->x = x;
        }
        break;
      case 'K':
        for (int x = model->x; x < MODEL_COLUMNS; ++x) {
          model->cells[model->y][x].character = ' ';
          model->cells[model->y][x].bold = 0;
        }
        break;
      case 'm': {
        // The library always writes a full "replace" sequence opening with 0.
        model->bold = 0;
        for (int p = 0; p <= count; ++p) {
          if (parameters[p] == 1) {
            model->bold = 1;
          }
        }
        break;
      }
      case 'h':
        if (private_marker && parameters[0] == 25) {
          model->cursor_visible = 1;
        }
        break;
      case 'l':
        if (private_marker && parameters[0] == 25) {
          model->cursor_visible = 0;
        }
        break;
      default:
        break;
    }
  }
}

// The row as text, so a failing test prints something readable.
static const char *model_row(const TerminalModel *model, int y) {
  static char row[MODEL_COLUMNS + 1];
  for (int x = 0; x < MODEL_COLUMNS; ++x) {
    row[x] = model->cells[y][x].character;
  }
  row[MODEL_COLUMNS] = '\0';
  // Trailing blanks carry no information and only make the diff noisy.
  for (int x = MODEL_COLUMNS - 1; x >= 0 && row[x] == ' '; --x) {
    row[x] = '\0';
  }
  return row;
}

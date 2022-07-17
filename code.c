/****************************************\
 ========================================

                  CODE

        minimalist terminal based
              text editor

                   by

            Code Monkey King

 ========================================
\****************************************/

// libraries
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

// definitions
#define CONTROL(k) ((k) & 0x1f)
#define CLEAR_SCREEN "\x1b[2J"
#define CLEAR_LINE "\x1b[K"
#define RESET_CURSOR "\x1b[H"
#define GET_CURSOR "\x1b[6n"
#define CURSOR_MAX "\x1b[999C\x1b[999B"
#define HIDE_CURSOR "\x1b[?25l"
#define SHOW_CURSOR "\x1b[?25h"
#define INIT_BUFFER {NULL, 0}

// original terminal settings
struct termios coocked_mode;

// editor variables
int ROWS = 80;
int COLS = 24;
int cury = 0;
int curx = 0;

// editor's internal buffer
struct buffer {
  char *string;
  int len;
};

// append string to buffer
void append_buffer(struct buffer *buf, const char *string, int len) {
  char *new_string = realloc(buf->string, buf->len + len);
  
  if (new_string == NULL) return;
  memcpy(&new_string[buf->len], string, len);
  buf->string = new_string;
  buf->len += len;
}

// free buffer
void clear_buffer(struct buffer *buf) {
  free(buf->string);
}

// clear screen
void clear_screen() {
  write(STDOUT_FILENO, CLEAR_SCREEN, 4);
  write(STDOUT_FILENO, RESET_CURSOR, 3);
}

// error handling
void die(const char *message) {
  clear_screen();
  perror(message);
  exit(1);
}

// restore terminal back to default mode
void restore_terminal() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &coocked_mode);
}

// enable terminal raw mode
void raw_mode() {
  if (tcgetattr(STDIN_FILENO, &coocked_mode) == -1) die("tcgetattr");
  atexit(restore_terminal);
  
  struct termios raw_mode = coocked_mode;
  raw_mode.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw_mode.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw_mode.c_oflag &= ~(OPOST);
  raw_mode.c_cflag |= (CS8);
  raw_mode.c_cc[VMIN] = 0;
  raw_mode.c_cc[VTIME] = 1;
  
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_mode) == -1) die("tcsetattr");
}

// read a single key from STDIN
int read_key() {
  int bytes;
  char c;
  
  while ((bytes = read(STDIN_FILENO, &c, 1)) == -1)
    if (bytes == -1 && errno != EAGAIN) die("read");
  
  return c;
}

// process keypress
void read_keyboard() {
  int c = read_key();
  
  switch(c) {
    case CONTROL('q'):
      clear_screen();
      exit(0);
      break;
  }
}

// render editable file
void print_buffer(struct buffer *buf) {
  for (int row = 0; row < ROWS; row++) {
    if (row == ROWS / 3) {
      char info[100];
      int infolen = snprintf(info, sizeof(info), "Minimalist code editor");
      if (infolen > COLS) infolen = COLS;
      int padding = (COLS - infolen) / 2;
      if (padding) {
        append_buffer(buf, "~", 1);
        padding--;
      } while(padding--) append_buffer(buf, " ", 1);
      append_buffer(buf, info, infolen);
    } else {
      append_buffer(buf, "~", 1);
    }
    
    append_buffer(buf, CLEAR_LINE, 3);
    if (row < ROWS - 1) {
      append_buffer(buf, "\r\n", 2);
    }
  }
}

// refresh screen
void update_screen() {
  struct buffer buf = INIT_BUFFER;
  write(STDOUT_FILENO, HIDE_CURSOR, 6);
  write(STDOUT_FILENO, RESET_CURSOR, 3);
  print_buffer(&buf);
  write(STDOUT_FILENO, RESET_CURSOR, 3);
  write(STDOUT_FILENO, SHOW_CURSOR, 6);
  write(STDOUT_FILENO, buf.string, buf.len);
  clear_buffer(&buf);
}

// get cursor ROW, COL position
int get_cursor(int *rows, int * cols) {
  char buf[32];
  unsigned int i = 0;
  
  if (write(STDOUT_FILENO, GET_CURSOR, 4) != 4) return -1;
  
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  } buf[i] = '\0';
  
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  
  return 0;
}

// get terminal window size
int get_window_size(int *rows, int *cols) {
  struct winsize ws;
  
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, CURSOR_MAX, 12) != 12) return -1;
    return get_cursor(rows, cols);
  } else {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
  }
}

int main() {
  raw_mode();
  get_window_size(&ROWS, &COLS);

  while (1) {
    update_screen();
    read_keyboard();
  }
}





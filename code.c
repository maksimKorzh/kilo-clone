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
#define SET_CURSOR "\x1b[%d;%dH"
#define CURSOR_MAX "\x1b[999C\x1b[999B"
#define HIDE_CURSOR "\x1b[?25l"
#define SHOW_CURSOR "\x1b[?25h"
#define INIT_BUFFER {NULL, 0}

// special keys
enum keys {
  ARROW_LEFT = 256,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  HOME,
  END
};

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

// control cursor
void move_cursor(int key) {
  switch(key) {
    case ARROW_LEFT: if (curx != 0) curx--; break;
    case ARROW_RIGHT: if (curx != COLS - 1) curx++; break;
    case ARROW_UP: if (cury != 0)cury--; break;
    case ARROW_DOWN: if (cury != ROWS - 1)cury++; break;
  }
}

// read a single key from STDIN
int read_key() {
  int bytes;
  char c;
  
  while ((bytes = read(STDIN_FILENO, &c, 1)) != 1)
    if (bytes == -1 && errno != EAGAIN) die("read");
  
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    
    //printf("%s\r\n", seq);
    //return 0;
    
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME;
            case '4': return END;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME;
            case '8': return END;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME;
          case 'F': return END;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME;
        case 'F': return END;
      }
    } return '\x1b';
  } else return c;
}

// process keypress
void read_keyboard() {
  int c = read_key();
  
  switch(c) {
    case CONTROL('q'):
      clear_screen();
      exit(0);
      break;
    
    case HOME: curx = 0; break;
    case END: curx = COLS - 1; break;
    
    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = COLS;
        while (times--) move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      } break;
    
    case ARROW_LEFT:
    case ARROW_RIGHT:
    case ARROW_UP:
    case ARROW_DOWN:
      move_cursor(c);
      break;
    default:
      //printf("KEY: %c\r\n", c);
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
  append_buffer(&buf, HIDE_CURSOR, 6);
  append_buffer(&buf, RESET_CURSOR, 3);
  print_buffer(&buf);
  char curpos[32];
  snprintf(curpos, sizeof(curpos), SET_CURSOR, cury + 1, curx + 1);
  append_buffer(&buf, curpos, strlen(curpos));
  append_buffer(&buf, SHOW_CURSOR, 6);
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
  if (get_window_size(&ROWS, &COLS) == -1) die("get_window_size");

  while (1) {
    update_screen();
    read_keyboard();
  }
}





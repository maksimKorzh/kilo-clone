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
#include <sys/types.h>

// editor definition
#define CONTROL(k) ((k) & 0x1f)
#define CLEAR_SCREEN "\x1b[2J"
#define CLEAR_LINE "\x1b[K"
#define RESET_CURSOR "\x1b[H"
#define GET_CURSOR "\x1b[6n"
#define SET_CURSOR "\x1b[%d;%dH"
#define CURSOR_MAX "\x1b[999C\x1b[999B"
#define HIDE_CURSOR "\x1b[?25l"
#define SHOW_CURSOR "\x1b[?25h"

/****************************************\
 ========================================

                 SETTINGS

 ========================================
\****************************************/

// special keys
enum { ARROW_LEFT = 1000, ARROW_RIGHT, ARROW_UP, ARROW_DOWN, PAGE_UP, PAGE_DOWN, HOME, END };
int keys_group_1[] = { HOME, -1, -1, END, PAGE_UP, PAGE_DOWN, HOME, END };
int keys_group_2[] = { ARROW_UP, ARROW_DOWN, ARROW_RIGHT, ARROW_LEFT, -1, END, -1, HOME };
int keys_group_3[] = { END, -1, HOME};

// editor variables
int ROWS = 80;
int COLS = 24;
int cury = 0;
int curx = 0;
int lines_number = 0;
int row_offset = 0;
int col_offset = 0;

// editor's 'screen'
struct buffer {
  char *string;
  int len;
};

// line of text representation
typedef struct text_buffer {
  char *string;
  int len;
} text_buffer;

// editor's lines of text
text_buffer *text = NULL;

/****************************************\
 ========================================

                 TERMINAL

 ========================================
\****************************************/

// original terminal settings
struct termios coocked_mode;

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
void restore_terminal() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &coocked_mode); }

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

// control cursor
void move_cursor(int key) {
  switch(key) {
    case ARROW_LEFT: if (curx != 0) curx--; break;
    case ARROW_RIGHT: curx++; break;
    case ARROW_UP: if (cury != 0)cury--; break;
    case ARROW_DOWN: if (cury != lines_number)cury++; break;
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
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') return keys_group_1[seq[1] - '1'];
      } else return keys_group_2[seq[1] - 'A'];
    } else if (seq[0] == 'O') {
      return keys_group_3[seq[1] - 'F'];
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


/****************************************\
 ========================================

                  EDITOR

 ========================================
\****************************************/

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

// render editable file
void print_buffer(struct buffer *buf) {
  for (int row = 0; row < ROWS; row++) {
    int bufrow = row + row_offset;
    if (bufrow >= lines_number) {
      if (lines_number == 0 && row == ROWS / 3) {
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
    } else {
      int len = text[bufrow].len - col_offset;
      if (len < 0) len = 0;
      if (len > COLS) len = COLS;
      append_buffer(buf, &text[bufrow].string[col_offset], len);
    }
    
    append_buffer(buf, CLEAR_LINE, 3);
    if (row < ROWS - 1) {
      append_buffer(buf, "\r\n", 2);
    }
  }
}

// scroll text
void scroll_buffer() {
  if (cury < row_offset) { row_offset = cury; }
  if (cury >= row_offset + ROWS) { row_offset = cury - ROWS + 1; }
  if (curx < col_offset) col_offset = curx;
  if (curx >= col_offset + COLS) col_offset = curx - COLS + 1;
}

// append row
void append_row(char *string, size_t len) {
  text = realloc(text, sizeof(text_buffer) * (lines_number + 1));
  int linenum = lines_number;
  text[linenum].string = malloc(len + 1);
  text[linenum].len = len;
  memcpy(text[linenum].string, string, len);
  text[linenum].string[len] = '\0';
  lines_number++;
}

// refresh screen
void update_screen() {
  scroll_buffer();
  struct buffer buf = {NULL, 0};
  append_buffer(&buf, HIDE_CURSOR, 6);
  append_buffer(&buf, RESET_CURSOR, 3);
  print_buffer(&buf);
  char curpos[32];
  snprintf(curpos, sizeof(curpos), SET_CURSOR, (cury - row_offset) + 1, (curx - col_offset) + 1);
  append_buffer(&buf, curpos, strlen(curpos));
  append_buffer(&buf, SHOW_CURSOR, 6);
  write(STDOUT_FILENO, buf.string, buf.len);
  clear_buffer(&buf);
}


/****************************************\
 ========================================

                 FILE I/O

 ========================================
\****************************************/

void open_file(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");
  
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap, fp)) != -1) {  
    while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r')) linelen--;
    append_row(line, linelen);
  }

  free(line);
  fclose(fp);
}

/****************************************\
 ========================================

                   MAIN

 ========================================
\****************************************/

int main() {
  raw_mode();
  if (get_window_size(&ROWS, &COLS) == -1) die("get_window_size");
  open_file("code.c");
  
  
  while (1) {
    update_screen();
    read_keyboard();
  }
}





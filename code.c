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
#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <sys/ioctl.h>
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
#define INVERT_VIDEO "\x1b[7m"
#define RESTORE_VIDEO "\x1b[m"

/****************************************\
 ========================================

                 SETTINGS

 ========================================
\****************************************/

// configurables
int TAB_WIDTH = 4;

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
int renderx = 0;
int lastx = 0;
int userx = 0;
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
  char *render;
  int len;
  int render_len;
} text_buffer;

// editor's lines of text
text_buffer *text = NULL;

// current filename
char *filename = NULL;

// info message
char info_message[80];
time_t info_message_time = 0;

// original terminal settings
struct termios coocked_mode;

/****************************************\
 ========================================

                 TERMINAL

 ========================================
\****************************************/

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
  lastx = userx;
  text_buffer *row = (cury >= lines_number) ? NULL : &text[cury];
  switch(key) {
    case ARROW_LEFT:
      if (curx != 0) { curx--; userx--; }
      else if (cury > 0) {
        cury--;
        curx = text[cury].len;
      }
      break;
    
    case ARROW_RIGHT:
      if (row && curx < row->len) { curx++; userx++; }
      else if (row && curx == row->len) {
        cury++;
        curx = 0;
      }
      break;
    
    case ARROW_UP:
      if (cury != 0) {
        cury--;
        curx = lastx;
      } else curx = 0;
      break;
    
    case ARROW_DOWN:
      if (cury != lines_number) {
        cury++;
        curx = lastx;
      } break;
  }
  
  row = (cury >= lines_number) ? NULL : &text[cury];
  int rowlen = row ? row->len : 0;
  if (curx > rowlen) curx = rowlen;
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
    case END:
      if(cury < lines_number) curx = text[cury].len;
      break;
    
    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) cury = row_offset;
        else if (c == PAGE_DOWN) {
          cury = row_offset + ROWS - 1;
          if (cury > lines_number) cury = lines_number;
        }
        
        int times = ROWS;
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
      int len = text[bufrow].render_len - col_offset;
      if (len < 0) len = 0;
      if (len > COLS) len = COLS;
      append_buffer(buf, &text[bufrow].render[col_offset], len);
    }
    
    append_buffer(buf, CLEAR_LINE, 3);
    append_buffer(buf, "\r\n", 2);
  }
}

// convert curx to renderx
int curx_to_renderx(text_buffer *row, int current_x) {
  int render_x = 0;
  for (int col = 0; col < current_x; col++) {
    if (row->string[col] == '\t') render_x += (TAB_WIDTH - 1) - (render_x % TAB_WIDTH);
    render_x++;
  } return render_x;
}

// scroll text
void scroll_buffer() {
  renderx = 0;
  if (cury < lines_number) renderx = curx_to_renderx(&text[cury], curx);
  if (cury < row_offset) { row_offset = cury; }
  if (cury >= row_offset + ROWS) { row_offset = cury - ROWS + 1; }
  if (renderx < col_offset) col_offset = renderx;
  if (renderx >= col_offset + COLS) col_offset = renderx - COLS + 1;
}

// update row
void update_row(text_buffer *row) {
  int tabs = 0;
  for (int col = 0; col < row->len; col++)
    if (row->string[col] == '\t') tabs++;
  
  free(row->render);
  row->render = malloc(row->len + tabs*(TAB_WIDTH - 1) + 1);
  int index = 0;
  for (int col = 0; col < row->len; col++) {
    if (row->string[col] == '\t') {
      row->render[index++] = ' ';
      while (index % TAB_WIDTH != 0) row->render[index++] = ' ';
    } else row->render[index++] = row->string[col];
  }
  row->render[index] = '\0';
  row->render_len = index;
}

// append row
void append_row(char *string, size_t len) {
  text = realloc(text, sizeof(text_buffer) * (lines_number + 1));
  int linenum = lines_number;
  text[linenum].string = malloc(len + 1);
  text[linenum].len = len;
  memcpy(text[linenum].string, string, len);
  text[linenum].string[len] = '\0';
  text[linenum].render = NULL;
  text[linenum].render_len = 0;
  update_row(&text[linenum]);
  lines_number++;
}

// draw status bar
void print_status_bar(struct buffer *buf) {
  append_buffer(buf, INVERT_VIDEO, 4);
  char message_left[80]; char message_right[80];
  int len_left = snprintf(message_left, sizeof(message_left), "%.20s - %d lines", filename ? filename : "[No file]", lines_number);
  int len_right = snprintf(message_right, sizeof(message_right), "Row %d, Col %d", cury + 1, curx + 1);
  if (len_left > COLS) len_left = COLS;
  append_buffer(buf, message_left, len_left);
  while (len_left < COLS) {
    if (COLS - len_left == len_right) {
      append_buffer(buf, message_right, len_right);
      break;
    } else {
		  append_buffer(buf, " ", 1);
		  len_left++;
    }
  }
  
  append_buffer(buf, RESTORE_VIDEO, 3);
  append_buffer(buf, "\r\n", 2);
}

// print a message under the status bar
void print_info_message(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(info_message, sizeof(info_message), fmt, ap);
  va_end(ap);
  info_message_time = time(NULL);
}

// print message bar
void print_message_bar(struct buffer *buf) {
  append_buffer(buf, CLEAR_LINE, 3);
  int msglen = strlen(info_message);
  if (msglen > COLS) msglen = COLS;
  if (msglen && time(NULL) - info_message_time < 5)
    append_buffer(buf, info_message, msglen);
}

// refresh the screen
void update_screen() {
  scroll_buffer();
  struct buffer buf = {NULL, 0};
  append_buffer(&buf, HIDE_CURSOR, 6);
  append_buffer(&buf, RESET_CURSOR, 3);
  print_buffer(&buf);
  print_status_bar(&buf);
  print_message_bar(&buf);
  char curpos[32];
  snprintf(curpos, sizeof(curpos), SET_CURSOR, (cury - row_offset) + 1, (renderx - col_offset) + 1);
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

void open_file(char *file_name) {
  free(filename);
  filename = strdup(file_name);
  
  FILE *fp = fopen(file_name, "r");
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
  ROWS -= 2;
  open_file("code.c");
  print_info_message("                       Press 'Ctrl-e' to enter command mode");  
  
  while (1) {
    update_screen();
    read_keyboard();
  }
}


// ddddddddddddddddddddddddddddddddd

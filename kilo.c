// Step 165

/*** include ***/
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

#define CTRL_KEY(k) ((k) & 0x1f) // Binary & operation

enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};
/*** data ***/
struct editorSyntax{
    char* filetype;
    char** filematch;
    char** keywords;
    char* singleline_comment_start;
    int flags;
};
//Editor row, counts size of chars and a buffer of chars
typedef struct erow{
    int size;
    int rsize;
    char* chars;
    char* render;
    unsigned char *hl;
} erow;

struct editorConfig {
    int cx, cy; //cursor x, cursor y
    int rx; // index into render field
    int rowoff; // row offset, what rowoff of the file the user is currently on
    int coloff; // column offset
    int screenrows;
    int screencols;
    int numrows;
    erow *row; //editor can have multiple buffer rows
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct editorSyntax *syntax;
    struct termios orig_termios;
};

/*** filetypes ***/
char* C_HL_extensions[] = {".c", ".h", ".cpp", NULL};

char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

struct editorSyntax HLDB[] = {
    {  "c",
       C_HL_extensions,
       C_HL_keywords,
       "//",
       HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
                              
};
/*** types ***/
// Append buffer
// change name to be slightly more meaningful, we're not code golfing this
struct abuf {
    char *buf;
    int len;
};
// Constructor for our append buffer
#define ABUF_INIT {NULL, 0}


struct editorConfig CONFIG;

/*** Prototypes ***/
void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen();

/*** terminal ***/
void enableRawMode();
void disableRawMode();
int editorReadKey();
void editorProcessKeyPress();
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int * rows, int *cols);

/*** syntax highlighting ***/
void editorUpdateSyntax(erow *row);
int editorSyntaxToColor(int hl);
int is_seperator(int c);
void editorSelectSyntaxHighlight();

/*** file i/o ***/
char* editorRowsToString(int *buflen);
void editorOpen(char* filename);
void editorSave();

/*** append buffer ***/
void abAppend(struct abuf *ab, const char* string, int len);
void abFree(struct abuf *ab);

/*** find ***/
void editorFindCallback(char *query, int key);
void editorFind();

/*** output ***/
void editorRefreshScreen();
void editorDrawStatusBar(struct abuf *ab);
void editorDrawRows(struct abuf *ab);
void editorScroll();
void editorDrawMessageBar(struct abuf *ab);

/*** row operations ***/
void editorUpdateRow(erow *row);
void editorAppendRow(char* s, size_t len);
void editorFreeRow(erow *row);
void editorDelRow(int at);
int editorRowCxToRx(erow * row, int cx);
int editorRowRxToCx(erow *row, int rx);
void editorRowInsertChar(erow *row, int at, int input);
void editorRowDelChar(erow *row, int at);
void editorRowAppendString(erow *row, char *s, size_t len);

/*** editor operations ***/
void editorInsertChar(int input);
void editorDelChar();
void editorInsertNewline();

/*** input ***/
void editorMoveCursor(int key);
char *editorPrompt(char* prompt,void (*callback)(char *, int));

/*** init ***/
void initEditor();

// error handling

void die(const char *s);

/*** init ***/
int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    if(argc >= 2){
        editorOpen(argv[1]); // pass in the first argument as a filename
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-f = find");
    // Read 1 byte at a time
    while(1){
        editorRefreshScreen();
        editorProcessKeyPress();
    }
    return 0;
}

/*** terminal ***/

void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &CONFIG.orig_termios) == -1){
        die("tcsetattr");
    }
    atexit(disableRawMode);

    struct termios raw = CONFIG.orig_termios;

    tcgetattr(STDIN_FILENO,&raw);

    // Flags to enable raw mode
    raw.c_iflag &= ~(BRKINT | ICRNL | IXON | ISTRIP | INPCK);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 0; // Value sets minimum number of bytes of input needed before read() can return. Set so it returns right away
    raw.c_cc[VTIME] = 1; // Maximum amount of time read waits to return, in tenths of seconds

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1){
        die("tcsetattr");
    }
}

void disableRawMode(){
    if(tcsetattr(STDERR_FILENO,TCSAFLUSH, &CONFIG.orig_termios) == -1){
        die("tcsetattrint");
    }
}

// Error handling
void die(const char *s){
    // Clear screen, and print error
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

int editorReadKey(){
    int nread;
    char input;

    while((nread = read(STDIN_FILENO, &input, 1)) != 1) {
        if(nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }

    // '\x1b' = 27
    if(input == '\x1b'){
        char seq[3];
        //??
        if (read(STDIN_FILENO, &seq[0], 1) != 1){
            return '\x1b';
        }

        if (read(STDIN_FILENO, &seq[1], 1) != 1){
            return '\x1b';
        }

        if(seq[0] == '['){
            if(seq[1] >= '0' && seq[1] <= '9'){
                if(read(STDIN_FILENO, &seq[2], 1) !=1){
                    return '\x1b';
                }
                if(seq[2] == '~'){
                    switch (seq[1]){
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == '0'){
            switch(seq[1]){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    } else {
        return input;
    }
}

void editorProcessKeyPress(){
    static int quit_times = KILO_QUIT_TIMES;
    int input = editorReadKey();

    switch(input) {
    case '\r':
        editorInsertNewline();
        break;

    case CTRL_KEY('q'):
        if(CONFIG.dirty && quit_times > 0) {
            // Clear screen and exit on quit
            editorSetStatusMessage("WARNING!!! File has unstaved changes. Press Ctrl-Q %d more times to quit.", quit_times);
            quit_times--;
            return;
        }
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
            break;

    case CTRL_KEY('s'):
        editorSave();
        break;

    case HOME_KEY:
        CONFIG.cx =0;
        break;

    case END_KEY:
        if(CONFIG.cy < CONFIG.numrows){
            CONFIG.cx = CONFIG.row[CONFIG.cy].size;
        }
        break;
    case CTRL_KEY('f'): {
        editorFind();
        break;
    }
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        if( input == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        break;
    case PAGE_UP:
    case PAGE_DOWN:
        {
            if(input == PAGE_UP){
                CONFIG.cy = CONFIG.rowoff;
            } else if (input == PAGE_DOWN){
                CONFIG.cy = CONFIG.rowoff + CONFIG.screenrows - 1;
                if(CONFIG.cy > CONFIG.numrows){
                    CONFIG.cy = CONFIG.numrows;
                }
            }
            int times = CONFIG.screenrows;
            while(times--){
                editorMoveCursor(input == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
        }
        break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(input);
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
        editorInsertChar(input);
        break;
    }

    quit_times = KILO_QUIT_TIMES;
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    // Esc + get argument 6 of Device Status Report
    // if we didn't write 4 bytes, error
    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4){
        return -1;
    }

    while (i < sizeof(buf) - 1) {
        if(read(STDIN_FILENO, &buf[i], 1) != 1){
            break;
        }

        if(buf[i] == 'R'){
            break;
        }

        i++;
    }

    // Null terminate buffer
    buf[i] = '\0';

    // If the second element does not equal
    // opening brace, then whats up with that?
    if(buf[0] != '\x1b' || buf[1] != '['){
        return -1;
    }

    // If we do not scan in two integers seperated by ;, then panic
    if(sscanf(&buf[2], "%d;%d", rows, cols) !=2){
        return -1;
    }

    return 0;
}

int getWindowSize(int *rows, int *cols){
    struct winsize ws;

    // If we can't for some reason find screen resolution, or get some wonky
    // value try moving the cursor to the bottom right else set rows and cols
    // accordingly
    if(1 || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        // If we fail to process 12 bites return -1
        // 999C and 999B mean go as far right and as far down as you can
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12){
            return -1;
        }
        // Return result of cursor position  after setting it to bottom right
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;

        return 0;
    }
}

/*** syntax highlighting ***/
int editorSyntaxToColor(int hl) {
    switch(hl){
    case HL_COMMENT: return 36;
    case HL_KEYWORD1: return 33;
    case HL_KEYWORD2: return 32;
    case HL_STRING: return 35;
    case HL_NUMBER: return 31;
    case HL_MATCH: return 34;
    default: return 37;
    }
}

void editorUpdateSyntax(erow *row){
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    if(CONFIG.syntax == NULL){
        return;
    }

    char** keywords = CONFIG.syntax->keywords;

    char* scs = CONFIG.syntax->singleline_comment_start;
    int scs_len = scs ? strlen(scs) : 0;
    
    int prev_sep = 1;
    int in_string = 0;
    
    int i = 0;
    
    while(i < row->rsize){
        char character = row->render[i];
        unsigned char prev_hl = (i > 0 ) ? row->hl[i - 1] : HL_NORMAL;

        if(scs_len && !in_string){
            if(!strncmp(&row->render[i], scs, scs_len)){
                memset(&row->hl[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }
        
        if(CONFIG.syntax->flags & HL_HIGHLIGHT_STRINGS){
            if(in_string){
                row->hl[i] = HL_STRING;
                if(character =='\\' && i + 1 < row->rsize){
                    row->hl[i+1] = HL_STRING;
                    i += 2;
                    continue;
                }
                if(character == in_string) {
                    in_string = 0;
                }
                i++;
                prev_sep = 1;
                continue;   
            } else {
                if (character =='"' || character == '\'') {
                    in_string = character;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }
        if(CONFIG.syntax->flags & HL_HIGHLIGHT_NUMBERS){
            if((isdigit(character) && (prev_sep || prev_hl == HL_NUMBER)) ||
               (character == '.' && prev_hl == HL_NUMBER)){
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep) {
            int j;
            for(j = 0; keywords[j]; j++){
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen-1] == '|';
                if(kw2) {
                    klen --;
                }

                if(!strncmp(&row->render[i], keywords[j], klen) &&
                   is_seperator(row->render[i + klen])) {
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL){
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_seperator(character);
        i++;
    }
}

int is_seperator(int c){
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorSelectSyntaxHighlight(){
    CONFIG.syntax = NULL;
    if(CONFIG.filename == NULL){
        return;
    }

    char* ext  = strrchr(CONFIG.filename, '.');

    for(unsigned int j = 0; j < HLDB_ENTRIES; j++){
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;
        while(s->filematch[i]){
            int is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
                (!is_ext && strstr(CONFIG.filename, s->filematch[i]))) {
                CONFIG.syntax = s;

                int filerow;
                for(filerow = 0; filerow < CONFIG.numrows; filerow++){
                    editorUpdateSyntax(&CONFIG.row[filerow]);
                }
                
                return;
            }
            i++;
        }
    }
}

/*** row operations ***/
void editorUpdateRow(erow *row){
    int tabs = 0;
    int j;

    for(j=0; j < row->size; j++){
        if(row->chars[j] == '\t'){
           tabs++;
        }
    }

    free(row->render);
    row->render = malloc(row->size + tabs*(KILO_TAB_STOP -1) + 1);

    int idx = 0;

    for(j=0; j< row->size; j++){
        if(row->chars[j] == '\t'){
            while(idx % KILO_TAB_STOP != 0){
                row -> render[idx++] = ' ';
            }

        } else {
            row->render[idx++] = row->chars[j];
        }
    }

    row->render[idx] = '\0';
    row->rsize = idx;

    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char* s, size_t len){
    if(at < 0 || at > CONFIG.numrows) return;

    CONFIG.row = realloc(CONFIG.row, sizeof(erow) * (CONFIG.numrows + 1));
    memmove(&CONFIG.row[at + 1], &CONFIG.row[at], sizeof(erow) * (CONFIG.numrows - at));

    CONFIG.row[at].size = len;
    CONFIG.row[at].chars = malloc(len + 1);
    memcpy(CONFIG.row[at].chars, s, len);
    CONFIG.row[at].chars[len] = '\0';

    CONFIG.row[at].rsize = 0;
    CONFIG.row[at].render = NULL;
    CONFIG.row[at].hl = NULL;
    editorUpdateRow(&CONFIG.row[at]);

    CONFIG.numrows++;
    CONFIG.dirty++;
}
void editorFreeRow(erow *row){
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void editorDelRow(int at){
    if( at < 0 || at >= CONFIG.numrows) return;
    editorFreeRow(&CONFIG.row[at]);
    memmove(&CONFIG.row[at], &CONFIG.row[at + 1], sizeof(erow) * (CONFIG.numrows - at -1));
    CONFIG.numrows--;
    CONFIG.dirty++;
}
void editorRowInsertChar(erow * row, int at, int input){
    if(at < 0 || at > row->size){
        at = row->size;
    }

    row->chars = realloc(row->chars, row->size + 2); //reallocate enough space for new char and nullbyte
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = input;
    editorUpdateRow(row);
    CONFIG.dirty++;
}

void editorRowDelChar(erow *row, int at){
    if(at < 0 || at >=row->size) return;

    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    CONFIG.dirty++;
}

int editorRowCxToRx(erow *row, int cx){
    int rx = 0;
    int j;
    for(j=0; j < cx; j++){
        if(row->chars[j] == '\t'){
           rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        }
        rx++;
    }
    return rx;
}

int editorRowRxToCx(erow *row, int rx){
    int cur_rx = 0;
    int cx;

    for(cx = 0; cx < row->size; cx++){
        if(row->chars[cx] == '\t'){
            cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
        }
        cur_rx++;

        if(cur_rx > rx){
            return cx;
        }
    }
    return cx;
}

void editorRowAppendString(erow *row, char *s, size_t len){
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    CONFIG.dirty++;
}

/*** editor operations **/
void editorInsertChar(int input){
    if(CONFIG.cy == CONFIG.numrows){
        editorInsertRow(CONFIG.numrows,"", 0);
    }
    editorRowInsertChar(&CONFIG.row[CONFIG.cy], CONFIG.cx, input);
    CONFIG.cx++;
}

void editorInsertNewline() {
  if (CONFIG.cx == 0) {
    editorInsertRow(CONFIG.cy, "", 0);
  } else {
    erow *row = &CONFIG.row[CONFIG.cy];

    editorInsertRow(CONFIG.cy + 1, &row->chars[CONFIG.cx], row->size - CONFIG.cx);
    row = &CONFIG.row[CONFIG.cy];
    row->size = CONFIG.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  CONFIG.cy++;
  CONFIG.cx = 0;
}

void editorDelChar(){
    // if the cursor is past the end of the file there is nothing to delete
    if(CONFIG.cy == CONFIG.numrows) return;
    if(CONFIG.cx == 0 && CONFIG.cy == 0) return;

    erow *row = &CONFIG.row[CONFIG.cy];
    if(CONFIG.cx > 0){
        editorRowDelChar(row, CONFIG.cx - 1);
        CONFIG.cx--;
    } else {
        CONFIG.cx = CONFIG.row[CONFIG.cy - 1].size;
        editorRowAppendString(&CONFIG.row[CONFIG.cy -1], row->chars, row->size);
        editorDelRow(CONFIG.cy);
        CONFIG.cy--;
    }
}

/*** file i/o ***/

/*
 * Convert a buffer to a single string
 */
char* editorRowsToString(int *buflen){
    int totlen=0;
    int j;
    for(j=0; j<CONFIG.numrows; j++){
        totlen += CONFIG.row[j].size + 1;
    }
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;

    for(j=0; j < CONFIG.numrows; j++){
        memcpy(p, CONFIG.row[j].chars, CONFIG.row[j].size);
        p += CONFIG.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void editorOpen(char* filename){
    free(CONFIG.filename);
    CONFIG.filename = strdup(filename);

    editorSelectSyntaxHighlight();
    
    FILE *fp = fopen(filename, "r");

    if(!fp){
        die("fopen");
    }

    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen; // Why ssize_t?
    while((linelen = getline(&line, &linecap, fp)) != -1) {//Draw as many rows as possible
        while(linelen > 0 && (line[linelen - 1] == '\n' ||
                              line[linelen - 1] == '\r')){
            linelen--;
        }
        editorInsertRow(CONFIG.numrows, line, linelen);
    }

    free(line);
    fclose(fp);
    CONFIG.dirty = 0;
}

void editorSave(){
    if(CONFIG.filename == NULL){
        CONFIG.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if(CONFIG.filename == NULL) {
            editorSetStatusMessage("Save aborted");
            return;
        }
        editorSelectSyntaxHighlight();
    }

    int len;
    char *buf = editorRowsToString(&len);


    int fd = open(CONFIG.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1){
        if (ftruncate(fd, len) != -1){
            if (write(fd, buf, len) == len){
                close(fd);
                free(buf);
                CONFIG.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O errors: %s", strerror(errno));
}

/*** append buffer ***/
// appendbuffer append
void abAppend(struct abuf *ab, const char* string, int len) {
    // ab = append buffer to add string to
    // string = string to copy
    // len = length

    // increase the size of our append buffer to be the length of ab + new string length
    char *new = realloc(ab->buf, ab->len + len);

    // error out
    if (new == NULL) return;

    // copy string into new buffer starting at end of buffer
    memcpy(&new[ab->len], string, len);

    ab->buf = new;
    ab->len += len;
}

// append buffer free
void abFree(struct abuf *ab){
    free(ab->buf);
}


/*** output ***/
void editorDrawRows(struct abuf *ab){
    int y;
    for(y = 0; y < CONFIG.screenrows; y++){
        int filerow = y + CONFIG.rowoff;
        if (filerow >= CONFIG.numrows) {
            // Put welcome message in top third of screen
            if(CONFIG.numrows ==0 && y== CONFIG.screenrows / 3) {
                // But only If text buffer is empty
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);

                // /* TODO:  *truncate message if screen too short
                if(welcomelen > CONFIG.screencols) {
                    welcomelen = CONFIG.screencols;
                }

                int padding = (CONFIG.screencols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) {
                    abAppend(ab, " ", 1);
                }
                abAppend(ab, welcome, welcomelen);
            } else {
                // For each row add ~\r\n to the string
                abAppend(ab,"~", 1);
            }
        } else {
            int len = CONFIG.row[filerow].size - CONFIG.coloff; //Handle multiple rows
            // because len can now be negative, need to be sure its min is 0
            if(len < 0){
                len = 0;
            }

            if(len > CONFIG.screencols){
                len = CONFIG.screencols;
            }
            
            char* row = &CONFIG.row[filerow].render[CONFIG.coloff];
            unsigned char* hl = &CONFIG.row[filerow].hl[CONFIG.coloff];
            int current_color = -1;
            
            int j;
            for(j=0; j < len; j++) {
                if(hl[j] == HL_NORMAL) {
                    if(current_color != -1){
                        abAppend(ab, "\x1b[39m", 5);
                        current_color = -1;
                    }
                    abAppend(ab, &row[j], 1);
                } else {
                    int color = editorSyntaxToColor(hl[j]);

                    if(color != current_color){
                        current_color = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, clen);
                    }
                    // at filerow print as many characters, starting at offset as there is screensize or chars left
                    abAppend(ab, &row[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5);
        }
        // K erases part of current line
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    CONFIG.filename ? CONFIG.filename : "[No Name]", CONFIG.numrows,
    CONFIG.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
    CONFIG.syntax ? CONFIG.syntax->filetype : "no ft", CONFIG.cy + 1, CONFIG.numrows);
  if (len > CONFIG.screencols) len = CONFIG.screencols;
  abAppend(ab, status, len);
  while (len < CONFIG.screencols) {
    if (CONFIG.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}
void editorRefreshScreen(){
    editorScroll();
    struct abuf ab = ABUF_INIT;

    // l = turn off
    // ?25 = cursor
    abAppend(&ab, "\x1b[?25l", 6);

    // Write four bytes to terminal
    // \x1b = escape character (27)
    // [J = erase screeen
    // [J2 = clear entire screen
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);
    char buf[32];
    // Specify the exact position in the terminal the cursor should be drawn atexit
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (CONFIG.cy - CONFIG.rowoff) + 1, (CONFIG.rx - CONFIG.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    // h = turn on
    // ?25 = cursor
    abAppend(&ab, "\x1b[?25h]", 6);
    write(STDOUT_FILENO, ab.buf, ab.len);
    abFree(&ab);
}

void editorScroll() {
    CONFIG.rx = 0;

    if(CONFIG.cy < CONFIG.numrows) {
        CONFIG.rx = editorRowCxToRx(&CONFIG.row[CONFIG.cy], CONFIG.cx);// Ensure cursor moves properly with tabs
    }

    if (CONFIG.cy < CONFIG.rowoff) {
        CONFIG.rowoff = CONFIG.cy;
    }
    if (CONFIG.cy >= CONFIG.rowoff + CONFIG.screenrows) {
        CONFIG.rowoff = CONFIG.cy - CONFIG.screenrows + 1;
    }
    if(CONFIG.rx  < CONFIG.coloff){
        CONFIG.coloff = CONFIG.cx;
    }
    if(CONFIG.rx >= CONFIG.coloff + CONFIG.screencols){
        CONFIG.coloff = CONFIG.cx - CONFIG.screencols + 1;
    }
}

void editorSetStatusMessage(const char* fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(CONFIG.statusmsg, sizeof(CONFIG.statusmsg), fmt, ap);
    va_end(ap);
    CONFIG.statusmsg_time = time(NULL);
}

void editorDrawMessageBar(struct abuf *ab){
    abAppend(ab, "\x1b[k", 3);
    int msglen = strlen(CONFIG.statusmsg);
    if(msglen > CONFIG.screencols){
        msglen = CONFIG.screencols;
    }
    if(msglen && time(NULL) - CONFIG.statusmsg_time < 5){
        abAppend(ab, CONFIG.statusmsg, msglen);
    }

}


/*** input ***/
void editorMoveCursor(int key){
    erow *row = (CONFIG.cy >= CONFIG.numrows) ? NULL : &CONFIG.row[CONFIG.cy];

    switch(key) {

    case ARROW_LEFT:
        if (CONFIG.cx != 0){
            CONFIG.cx--;
        } else if (CONFIG.cy > 0){ // Go one row up and to the end of the line
            CONFIG.cy--;
            CONFIG.cx = CONFIG.row[CONFIG.cy].size;
        }
        break;
    case ARROW_RIGHT:
        if(row && CONFIG.cx < row->size){
            CONFIG.cx++;
        } else if(row && CONFIG.cx == row->size){
            CONFIG.cy++;
            CONFIG.cx = 0;
        }
        break;
    case ARROW_UP:
        if(CONFIG.cy > 0){
            CONFIG.cy--;
        }
        break;
    case ARROW_DOWN:
        if(CONFIG.cy < CONFIG.numrows){
            CONFIG.cy++;
        }
        break;
    }

    row = (CONFIG.cy >= CONFIG.numrows) ? NULL : &CONFIG.row[CONFIG.cy];

    int rowlen = row ? row->size : 0;
    if(CONFIG.cx > rowlen){
        CONFIG.cx = rowlen;
    }
}

/*** find ***/

void editorFindCallback(char *query, int key){
    static int last_match = -1;
    static int direction = 1;

    static int saved_hl_line;
    static char* saved_hl = NULL;

    if(saved_hl){
        memcpy(CONFIG.row[saved_hl_line].hl, saved_hl, CONFIG.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }
    
    if( key == '\r' || key == '\x1b'){
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN){
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP){
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1){
        direction = 1;
    }
    int current = last_match;
    int i;
    for(i = 0; i < CONFIG.numrows; i++){
        current += direction;
        if(current == -1){
            current = CONFIG.numrows - 1;
        } else if (current == CONFIG.numrows){
            current = 0;
        }

        erow * row = &CONFIG.row[current];

        char *match = strstr(row->render, query);
        if(match) {
            last_match = current;
            CONFIG.cy = current;
            CONFIG.cx = editorRowRxToCx(row, match - row->render);
            CONFIG.rowoff = CONFIG.numrows;

            saved_hl_line = current;
            saved_hl = malloc(row->rsize);
            memcpy(saved_hl, row->hl, row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void editorFind(){
    int saved_cx = CONFIG.cx;
    int saved_cy = CONFIG.cy;

    int saved_coloff = CONFIG.coloff;
    int saved_rowoff = CONFIG.rowoff;

    char *query = editorPrompt("Search: %s (ESC to cancel/Arrows/Enter)", editorFindCallback);
    if(query){
        free(query);
    } else{
        CONFIG.cx = saved_cx;
        CONFIG.cy = saved_cy;
        CONFIG.coloff = saved_coloff;
        CONFIG.rowoff = saved_rowoff;
    }
}

char *editorPrompt(char *prompt,void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = (char *) malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();
    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      editorSetStatusMessage("");
      if(callback){
          callback(buf, c);
      }
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        if(callback){
            callback(buf, c);
        }
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = (char *) realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
    if(callback){
        callback(buf, c);
    }
  }
}
/*** Init ***/
void initEditor(){

    CONFIG.cx = 0;
    CONFIG.cy = 0;
    CONFIG.rx = 0;
    CONFIG.rowoff = 0;
    CONFIG.coloff = 0;
    CONFIG.numrows = 0;
    CONFIG.row = NULL;
    CONFIG.dirty = 0;
    CONFIG.filename = NULL;
    CONFIG.statusmsg[0] = '\0';
    CONFIG.statusmsg_time = 0;
    CONFIG.syntax = NULL;
    
    if(getWindowSize(&CONFIG.screenrows, &CONFIG.screencols) == -1){
        die("getWindowsize");
    }

    // Clear screen entirely before starting, this stops some artifacts from still ebing rendered
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    CONFIG.screenrows -= 2; // Save 1 row at bottom for a status bar
}

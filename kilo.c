// http://viewsourcecode.org/snaptoken/kilo/04.aTextViewer.html step 70

/*** include ***/

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>


/*** defines ***/
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8

#define CTRL_KEY(k) ((k) & 0x1f) // Binary & operation

enum editorKey {
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

/*** data ***/
//Editor row, counts size of chars and a buffer of chars
typedef struct erow{
    int size;
    int rsize;
    char* chars;
    char* render;
} erow;

struct editorConfig {
    int cx, cy; //cursor x, cursor y
    int rx; // index into render field
    int rowoff; // row offset, what rowoff of the file the user is currently on
    int coloff; // column offset
    int screenrows;
    int screencols;
    int numrows;
    erow* row; //editor can have multiple buffer rows
    struct termios orig_termios;
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

/*** terminal ***/
void enableRawMode();
void disableRawMode();
int editorReadKey();
void editorProcessKeyPress();
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int * rows, int *cols);

/*** file i/o ***/
void editorOpen(char* filename);

/*** append buffer ***/
void abAppend(struct abuf *ab, const char* string, int len);
void abFree(struct abuf *ab);
//thing

/*** output ***/
void editorRefreshScreen();
void editorDrawRows(struct abuf *ab);
void editorScroll();

/*** row operations ***/
void editorUpdateRow(erow *row);
void editorAppendRow(char* s, size_t len);

/*** input ***/
void editorMoveCursor(int key);

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
    int input = editorReadKey();

    switch(input){
        case CTRL_KEY('q'):

            // Clear screen and exit on quit
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            CONFIG.cx =0;
            break;
        case END_KEY:
            CONFIG.cx = CONFIG.screencols - 1;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                int time = CONFIG.screenrows;
                while(time--){
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
        }
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
}

void editorAppendRow(char* s, size_t len){
    CONFIG.row = realloc(CONFIG.row, sizeof(erow) * (CONFIG.numrows + 1));

    int at = CONFIG.numrows;
    CONFIG.row[at].size = len;
    CONFIG.row[at].chars = malloc(len + 1);
    memcpy(CONFIG.row[at].chars, s, len);
    CONFIG.row[at].chars[len] = '\0';

    CONFIG.row[at].rsize = 0;
    CONFIG.row[at].render = NULL;
    editorUpdateRow(&CONFIG.row[at]);

    CONFIG.numrows++;
}

/*** file i/o ***/
void editorOpen(char* filename){
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
        editorAppendRow(line, linelen);
    }

    free(line);
    fclose(fp);
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
            // at filerow print as many characters, starting at offset as there is screensize or chars left
            abAppend(ab, &CONFIG.row[filerow].chars[CONFIG.coloff], len);
        }
        // K erases part of current line
        abAppend(ab, "\x1b[K", 3);
        if ( y < CONFIG.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
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

    char buf[32];
    // Specify the exact position in the terminal the cursor should be drawn atexit
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (CONFIG.cy + CONFIG.rowoff) + 1, (CONFIG.rx - CONFIG.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    // h = turn on
    // ?25 = cursor
    abAppend(&ab, "\x1b[?25h]", 6);
    write(STDOUT_FILENO, ab.buf, ab.len);
    abFree(&ab);
}

void editorScroll() {
    CONFIG.rx = CONFIG.cx;
    
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
/*** init ***/
void initEditor(){
    CONFIG.cx = 0;
    CONFIG.cy = 0;
    CONFIG.rx = 0;
    CONFIG.rowoff = 0;
    CONFIG.coloff = 0;
    CONFIG.numrows = 0;
    CONFIG.row = NULL;
    
    if(getWindowSize(&CONFIG.screenrows, &CONFIG.screencols) == -1){
        die("getWindowsize");
    }
}

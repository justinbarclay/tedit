/*** include ***/

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>


/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f) // Binary & operation


/*** data ***/
struct editorConfig {
    int screenrows;
    int screencols;
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
char editorReadKey();
void editorProcessKeyPress();
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int * rows, int *cols);
void abAppend(struct abuf *ab, const char* string, int len);
void abFree(struct abuf *ab);

/*** output ***/
void editorRefreshScreen();
void editorDrawRows();

/*** init ***/
void initEditor();

// error handling

void die(const char *s);

/*** init ***/
int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
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

char editorReadKey(){
    int nread;
    char input;

    while((nread = read(STDIN_FILENO, &input, 1)) != 1) {
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    return input;
}

void editorProcessKeyPress(){
    char input = editorReadKey();

    switch(input){
        case CTRL_KEY('q'):
            
            // Clear screen and exit on quit
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
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
    free(ab->b);
}


/*** output ***/
void editorDrawRows(struct abuf *ab){
    int y;
    for(y = 0; y < CONFIG.screenrows; y++){
        // For each row add ~\r\n to the string
        abAppend(ab,"~", 1);

        if ( y < CONFIG.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen(){
    
    struct abuf ab = ABUF_INIT;

    // Write four bytes to terminal
    // \x1b = escape character (27)
    // [J = erase screeen
    // [J2 = clear entire screen
    abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    
    //[H = reposition cursor to (1,1)
    // (1,1) default args
    abAppend(&ab, "\x1b[H", 3);

    wire(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}
/*** init ***/
void initEditor(){
    if(getWindowSize(&CONFIG.screenrows, &CONFIG.screencols) == -1){
        die("getWindowsize");
    }
}

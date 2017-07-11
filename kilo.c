/*** include ***/
#include <ctype.h>
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

struct editorConfig CONFIG;

/*** terminal ***/
void enableRawMode();
void disableRawMode();
char editorReadKey();
void editorProcessKeyPress();
int getCursorPosition(int *row, int *cols);
int getWindowSize(int * row, int *cols);

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

int getCursorPosition(int *row, int *cols) {
    if(write(STDOUT_FILENO, "\x1b[6b", 4) != 4){
        return -1;
    }

    printf("\r\n");
    char c;

    while(read(STDIN_FILENO, &c, 1) == 1){
        if(iscntrl(c)){
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
    }
    editorReadKey();

    return -1;

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
        return getCursorPosition(row, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        
        return 0;
    }
}

/*** output ***/
void editorRefreshScreen(){
    // Write four bytes to terminal
    // \x1b = escape character (27)
    // [J = erase screeen
    // [J2 = clear entire screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    editorDrawRows();
    
    //[H = reposition cursor to (1,1)
    // (1,1) default args
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void editorDrawRows(){
    int y;
    for(y = 0; y < CONFIG.screenrows; y++){
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

/*** init ***/
void initEditor(){
    if(getWindowSize(&CONFIG.screenrows, &CONFIG.screencols) == -1){
        die("getWindowsize");
    }
}

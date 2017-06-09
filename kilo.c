/*** include ***/
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>


/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f) // Binary & operation


/*** data ***/
struct termios orig_termios;

/*** terminal ***/
void enableRawMode();
void disableRawMode();
char editorReadKey();
void editorProcessKeyPress();

/*** output ***/
void editorRefreshScreen();
// error handling

void die(const char *s);

/*** init ***/
int main(int argc, char *argv[])
{
    enableRawMode();

    // Read 1 byte at a time
    while(1){
        editorRefreshScreen();
        editorProcessKeyPress();
    }
    return 0;
}


/*** terminal ***/
void enableRawMode(){
    
    if(tcgetattr(STDIN_FILENO, &orig_termios) == -1){
        die("tcsetattr");
    }
    atexit(disableRawMode);

    struct termios raw;

    tcgetattr(STDIN_FILENO,&raw);

    // Flags to enable raw mode
    raw.c_iflag &= ~(BRKINT | ICRNL | IXON | ISTRIP | INPCK);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    //
    raw.c_cc[VMIN] = 0; // Value sets minimum number of bytes of input needed bfore read() can return. Set so it returns right away
    raw.c_cc[VTIME] = 1; // Maximum amount of time read waits to return, in tenths of seconds

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1){
        die("tcsetattr");
    }
}

void disableRawMode(){
    if(tcsetattr(STDERR_FILENO,TCSAFLUSH,&orig_termios) == -1){
        die("tcsetattrint");
    }
}

// Error handling
void die(const char *s){
    // Clear screen, and print error
    editorRefreshScreen()
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
            editorRefreshScreen();
            exit(0);
            break;
    }
}

/*** output ***/

void editorRefreshScreen(){
    // Write four bytes to terminal
    // \x1b = escape character (27)
    // [J = erase screeen
    // [J2 = clear entire screen
    write(STDOUT_FILENO, "\x1b[2J", 4);

    //[H = reposition cursor to (1,1)
    // (1,1) default args
    write(STDOUT_FILENO, "\x1b[H", 3);
}

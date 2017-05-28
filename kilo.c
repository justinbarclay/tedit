#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>

struct termios orig_termios;

void enableRawMode();
void disableRawMode();
void die(const char *s);
int main(int argc, char *argv[])
{
    enableRawMode();

    // Read 1 byte at a time
    while(1){
        char input = '\0'; // Input from user
        if(read(STDIN_FILENO, &input, 1) ==-1 && errno != EAGAIN){
            die("read");
        }
        if(iscntrl(input)) {
            printf("%d\r\n", input);
        } else {
            printf("%d ('%c')\r\n", input, input);
        }

        if(input == 'q') break;
    }
    return 0;
}


// Setup
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
    perror(s);
    exit(1);
}

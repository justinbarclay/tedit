#include <unistd.h>

int main(int argc, char *argv[])
{
    char c;

    // Read 1 byte at a time
    while(read(STDIN_FILENO, &c, 1) == 1);
    return 0;
}

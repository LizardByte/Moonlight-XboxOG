#ifdef NXDK

extern "C" {

int close(int fd)
{
    (void) fd;
    return 0;
}

long lseek(int fd, long offset, int whence)
{
    (void) fd;
    (void) offset;
    (void) whence;
    return -1;
}

}

#endif

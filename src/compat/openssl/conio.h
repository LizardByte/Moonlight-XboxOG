#ifndef MOONLIGHT_OPENSSL_CONIO_H
#define MOONLIGHT_OPENSSL_CONIO_H

#ifdef __cplusplus
extern "C" {
#endif

static inline int _kbhit(void)
{
    return 0;
}

static inline int _getch(void)
{
    return -1;
}

static inline int kbhit(void)
{
    return _kbhit();
}

static inline int getch(void)
{
    return _getch();
}

#ifdef __cplusplus
}
#endif

#endif

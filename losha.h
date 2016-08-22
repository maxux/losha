#ifndef __LOSHA
    void diep(char *str);
    void dies(char *str);

    #define debug(...) { if(__debug) { printf(__VA_ARGS__); } }
    extern int __debug;
#endif

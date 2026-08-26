#ifndef ASTRA_ASCII_H
#define ASTRA_ASCII_H

static inline char astra_ascii_upper(char value)
{
    return value >= 'a' && value <= 'z' ?
        (char)(value - ('a' - 'A')) : value;
}

#endif

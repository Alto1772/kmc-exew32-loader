#ifndef EXE32_COFF_H
#define EXE32_COFF_H

#include <stdint.h>
#include "common.h"

struct CoffHdr_s {
    uint16_t f_magic;
    uint16_t f_nscns;
    int32_t f_timdat;
    int32_t f_symptr;
    int32_t f_nsyms;
    uint16_t f_opthdr;
    uint16_t f_flags;
};

struct CoffOptHdr_s {
    uint16_t magic;
    uint16_t vstamp;
    uint32_t tsize;
    uint32_t dsize;
    uint32_t bsize;
    uint32_t entry;
    uint32_t text_start;
    uint32_t data_start;
};

struct CoffSecHdr_s {
    char s_name[8];
    void *s_paddr;
    void *s_vaddr;
    int32_t s_size;
    int32_t s_scnptr;
    int32_t s_relptr;
    int32_t s_lnnoptr;
    uint16_t s_nreloc;
    uint16_t s_nlnno;
    int32_t s_flags;
};

#define STYP_TEXT 0x20
#define STYP_DATA 0x40
#define STYP_BSS  0x80

#endif // EXE32_COFF_H

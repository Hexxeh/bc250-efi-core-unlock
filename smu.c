#include "smu.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

// Freestanding helper routines for compiler intrinsic calls
void *memcpy(void *dest, const void *src, UINTN n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memset(void *s, int c, UINTN n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

// Direct PCI config space access helpers (Bus 0, Device 0, Function 0)
static inline void outpd(unsigned short port, unsigned int val) {
    __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned int inpd(unsigned short port) {
    unsigned int val;
    __asm__ __volatile__("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

unsigned int smn_rd(unsigned int reg) {
    outpd(0xCF8, 0x80000000 | 0xB8);
    outpd(0xCFC, reg);
    outpd(0xCF8, 0x80000000 | 0xBC);
    return inpd(0xCFC);
}

void smu_wr(unsigned int reg, unsigned int val) {
    outpd(0xCF8, 0x80000000 | 0xB8);
    outpd(0xCFC, reg);
    outpd(0xCF8, 0x80000000 | 0xBC);
    outpd(0xCFC, val);
}

unsigned int smu_rd(unsigned int reg) {
    return smn_rd(reg);
}

int is_done(unsigned int status) {
    return (status == 0x01 || status == 0xFF || status == 0xFE || status == 0xFD || status == 0xFC);
}

static int smu_send_msg_generic(EFI_SYSTEM_TABLE *SystemTable, unsigned int cmd_addr, unsigned int rsp_addr, unsigned int arg_addr, unsigned int msg, const unsigned int *args, unsigned int num_args) {
    int timeout = 50000;
    while (!is_done(smu_rd(rsp_addr)) && timeout > 0) {
        SystemTable->BootServices->Stall(5);
        timeout--;
    }
    if (timeout <= 0) {
        return -1;
    }

    smu_wr(rsp_addr, 0);
    for (unsigned int i = 0; i < 6; i++) {
        unsigned int val = (i < num_args && args != NULL) ? args[i] : 0;
        smu_wr(arg_addr + 4 * i, val);
    }
    smu_wr(cmd_addr, msg);

    timeout = 50000;
    while (timeout > 0) {
        unsigned int st = smu_rd(rsp_addr);
        if (is_done(st)) {
            return (int)st;
        }
        SystemTable->BootServices->Stall(5);
        timeout--;
    }
    return -2;
}

int smu_send_msg_q3(EFI_SYSTEM_TABLE *SystemTable, unsigned int msg, const unsigned int *args, unsigned int num_args) {
    return smu_send_msg_generic(SystemTable, Q3_CMD, Q3_RSP, Q3_ARG, msg, args, num_args);
}

int smu_send_msg_q2(EFI_SYSTEM_TABLE *SystemTable, unsigned int msg, const unsigned int *args, unsigned int num_args) {
    return smu_send_msg_generic(SystemTable, Q2_CMD, Q2_RSP, Q2_ARG, msg, args, num_args);
}

int smu_read(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int n, unsigned int *out_words, UINT64 phys_dma_addr) {
    if (n > 18 || out_words == NULL) {
        return -1;
    }
    // sub 0x1F: transfer_engine_sram_load(addr, n)
    unsigned int load_args[6] = {0x1F, 0, addr, n, 0, 0};
    int st = smu_send_msg_q2(SystemTable, 0x0A, load_args, 6);
    if (st != 0x01) {
        return st;
    }

    // sub 0x14: transfer_engine_smu2dram(phys_hi, phys_lo, n)
    unsigned int phys_hi = (unsigned int)(phys_dma_addr >> 32);
    unsigned int phys_lo = (unsigned int)(phys_dma_addr & 0xFFFFFFFF);
    unsigned int dma_args[6] = {0x14, phys_hi, phys_lo, n, 0, 0};
    st = smu_send_msg_q2(SystemTable, 0x0A, dma_args, 6);
    if (st != 0x01) {
        return st;
    }

    volatile unsigned int *src = (volatile unsigned int *)(UINTN)phys_dma_addr;
    for (unsigned int i = 0; i < n; i++) {
        out_words[i] = src[i];
    }
    return 0x01;
}

int smu_write32(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int val) {
    // msg 0x28: sec_set_write_ptr(addr)
    unsigned int arg_addr[1] = {addr};
    int st = smu_send_msg_q3(SystemTable, 0x28, arg_addr, 1);
    if (st != 0x01) {
        return st;
    }
    // msg 0x29: sec_write_through32(val)
    unsigned int arg_val[1] = {val};
    return smu_send_msg_q3(SystemTable, 0x29, arg_val, 1);
}

int smu_memset32(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int val, unsigned int words) {
    for (unsigned int i = 0; i < words; i++) {
        int st = smu_write32(SystemTable, addr + 4 * i, val);
        if (st != 0x01) {
            return st;
        }
    }
    return 0x01;
}

int smu_read_bytes(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int size, unsigned char *out_bytes, UINT64 phys_dma_addr) {
    unsigned int base_start = addr & ~3U;
    unsigned int base_end = (addr + size + 3U) & ~3U;

    unsigned int idx = 0;
    for (unsigned int base = base_start; base < base_end; base += 72) { // 18 dwords = 72 bytes
        unsigned int chunk_words = (base_end - base) / 4;
        if (chunk_words > 18) chunk_words = 18;

        unsigned int buf[18];
        int st = smu_read(SystemTable, base, chunk_words, buf, phys_dma_addr);
        if (st != 0x01) {
            return st;
        }

        unsigned char *buf_bytes = (unsigned char *)buf;
        for (unsigned int w = 0; w < chunk_words; w++) {
            unsigned int cur_base = base + w * 4;
            for (unsigned int byte_off = 0; byte_off < 4; byte_off++) {
                unsigned int cur_addr = cur_base + byte_off;
                if (cur_addr >= addr && cur_addr < addr + size) {
                    out_bytes[idx++] = buf_bytes[w * 4 + byte_off];
                }
            }
        }
    }
    return 0x01;
}

int smu_write_bytes(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int size, const unsigned char *in_bytes, UINT64 phys_dma_addr) {
    unsigned int start_base = addr & ~3U;
    unsigned int end_base = (addr + size + 3U) & ~3U;

    for (unsigned int base = start_base; base < end_base; base += 4) {
        unsigned int dword = 0;
        int st = smu_read(SystemTable, base, 1, &dword, phys_dma_addr);
        if (st != 0x01) {
            return st;
        }

        unsigned int orig_dword = dword;
        for (unsigned int byte_off = 0; byte_off < 4; byte_off++) {
            unsigned int cur_addr = base + byte_off;
            if (cur_addr >= addr && cur_addr < addr + size) {
                unsigned int shift = byte_off * 8U;
                unsigned int val = (unsigned int)in_bytes[cur_addr - addr];
                dword = (dword & ~(0xFFU << shift)) | (val << shift);
            }
        }

        if (dword != orig_dword) {
            st = smu_write32(SystemTable, base, dword);
            if (st != 0x01) {
                return st;
            }
        }
    }
    return 0x01;
}

int smn_write32(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int val) {
    // msg 0x2B: sec_set_smn_write_addr(addr)
    unsigned int arg_addr[1] = {addr};
    int st = smu_send_msg_q3(SystemTable, 0x2B, arg_addr, 1);
    if (st != 0x01) {
        return st;
    }
    // msg 0x2C: sec_smn_write32(val)
    unsigned int arg_val[1] = {val};
    return smu_send_msg_q3(SystemTable, 0x2C, arg_val, 1);
}

int sec_smn_read32(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int *val) {
    // msg 0x2A: sec_smn_read32(addr)
    unsigned int arg_addr[1] = {addr};
    int st = smu_send_msg_q3(SystemTable, 0x2A, arg_addr, 1);
    if (val != NULL) {
        *val = smu_rd(Q3_ARG);
    }
    return st;
}

int transfer_engine_dram2smu(EFI_SYSTEM_TABLE *SystemTable, UINT64 phys_addr, unsigned int words, unsigned int key) {
    unsigned int phys_hi = (unsigned int)(phys_addr >> 32);
    unsigned int phys_lo = (unsigned int)(phys_addr & 0xFFFFFFFF);
    unsigned int dma_args[6] = {0x23, phys_hi, phys_lo, words, 0, key};
    return smu_send_msg_q2(SystemTable, 0x0A, dma_args, 6);
}

void print(EFI_SYSTEM_TABLE *SystemTable, UINT16 *str) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, str);
}

void print_hex(EFI_SYSTEM_TABLE *SystemTable, unsigned int val) {
    UINT16 buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        unsigned int digit = (val >> (i * 4)) & 0xF;
        buf[9 - i] = (digit < 10) ? ('0' + digit) : ('A' + (digit - 10));
    }
    buf[10] = '\0';
    print(SystemTable, buf);
}

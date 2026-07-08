// Astra 68 — CPU self-test ROM. Regression net for the control-FSM loop refactor.
// Uses ALGEBRAIC IDENTITIES that must hold for correct 68k execution (robust: no hand-computed
// magic constants to get wrong). First failing id is reported over UART, plus a coverage checksum.
// Emphasis: addressing modes, register hazards, -(An)/(An)+, MOVEM, PACK/UNPK, 32b MUL/DIV, ADDX,
// bitfields — the exact paths the false-loop fix / FSM refactor can disturb.
#include "vesta.h"

// freestanding: gcc emits calls to these for aggregate init/copy
void *memcpy(void *d,const void *s,unsigned long n){ char*dd=d; const char*ss=s; while(n--)*dd++=*ss++; return d; }
void *memset(void *d,int c,unsigned long n){ char*dd=d; while(n--)*dd++=(char)c; return d; }

static void uart_putc(char c){ while(!(VESTA->UART_STATUS & UART_TX_READY)){} VESTA->UART_DATA=(uint8_t)c; }
static void uart_puts(const char*s){ for(;*s;s++){ if(*s=='\n')uart_putc('\r'); uart_putc(*s);} }
static void uart_hex32(uint32_t v){ int i; for(i=28;i>=0;i-=4){ int d=(v>>i)&0xF; uart_putc(d<10?('0'+d):('A'+d-10)); } }

static volatile int g_fail = 0;
static volatile uint32_t g_sum = 0;
// CHK(id, cond): cond must be true. TRK(id, val): fold val into coverage checksum.
#define CHK(id, cond) do { g_sum=(g_sum<<1|g_sum>>31)^(uint32_t)(id); if(g_fail==0 && !(cond)) g_fail=(id); } while(0)
#define TRK(id, val)  do { g_sum=(g_sum<<1|g_sum>>31)^(uint32_t)(val)^(uint32_t)(id); } while(0)

void kmain(void){
    volatile int32_t a=0x12345678, b=0x0000ABCD, c=-77;
    volatile uint32_t ua=0xF0F0A5A5u, ub=0x0F0F5A5Au;

    // 1x arithmetic identities (ADD/SUB/NEG, all sizes)
    CHK(10, (a+b)-b == a);
    CHK(11, (a-b)+b == a);
    CHK(12, -(-a) == a);
    CHK(13, (int32_t)(a+c)-c == a);
    { volatile int16_t w=0x4321; CHK(14, (int16_t)((w+0x1000)-0x1000) == w); }
    { volatile int8_t  q=0x39;   CHK(15, (int8_t)((q+40)-40) == q); }

    // 2x multiply / divide (32b -> the OP_SIZE_I LONG terms)
    { volatile int32_t m1=1234, m2=-5678; CHK(20, m1*m2 == m2*m1); CHK(21, (m1*m2)/m2 == m1); }
    { volatile uint32_t d=0xDEADBEEFu, k=7u; CHK(22, (d/k)*k + d%k == d); }
    { volatile int32_t d=-1000003, k=97;    CHK(23, (d/k)*k + d%k == d); }
    { volatile int16_t d=30000, k=7;        CHK(24, (int16_t)((d/k)*k + d%k) == d); }
    // 2y signed div/mod SIGN edges: remainder takes the DIVIDEND's sign; incl. both INIT fast paths
    { volatile int32_t d=-1000003, k=97;    CHK(25, d/k==-10309 && d%k==-30);  TRK(25,(uint32_t)(d%k)); } // main path, neg rem
    { volatile int32_t d=-1000003, k=-97;   CHK(26, d/k== 10309 && d%k==-30);  TRK(26,(uint32_t)(d/k)); } // neg/neg: q+, r-
    { volatile int32_t d=-5, k=97;          CHK(27, d/k== 0     && d%k==-5); }  // |dvd|<|dvs| fast path, neg rem
    { volatile int32_t d=-97, k=97;         CHK(28, d/k==-1     && d%k==0);  }  // |dvd|=|dvs| fast path, signs differ
    { volatile int32_t d=97, k=-97;         CHK(29, d/k==-1     && d%k==0);  }  // equal magnitude, other sign

    // 3x logic / shift / bit identities
    CHK(30, (ua & ua) == ua);
    CHK(31, (ua | ua) == ua);
    CHK(32, (ua ^ ua) == 0u);
    CHK(33, ((ua & ub) | (ua & ~ub)) == ua);          // distributive
    CHK(34, (~~ua) == ua);
    { volatile int n=4; CHK(35, ((ua<<n)>>n) == (ua & (0xFFFFFFFFu>>n))); }
    { volatile int32_t s=-4096; CHK(36, (s>>4)<<4 == s); }   // ASR then LSL, aligned
    { uint32_t r; __asm__ volatile("rol.l #5,%0":"=d"(r):"0"(ua)); CHK(37, r == ((ua<<5)|(ua>>27))); }
    { uint32_t r=ua; __asm__ volatile("bchg #7,%0":"+d"(r)); __asm__ volatile("bchg #7,%0":"+d"(r)); CHK(38, r==ua); } // bchg twice = identity

    // 4x addressing modes (loops live in the address/hazard path)
    volatile int32_t arr[8]={11,22,33,44,55,66,77,88};
    CHK(40, arr[0]+arr[7] == 99);
    { int32_t f=0,r=0,i; for(i=0;i<8;i++) f+=arr[i]; for(i=7;i>=0;i--) r+=arr[i]; CHK(41, f==r); TRK(41,f); } // index +/-
    { int32_t *p=(int32_t*)arr,s=0,i; for(i=0;i<8;i++) s+=*p++;  int32_t *q=(int32_t*)&arr[8],t=0; for(i=0;i<8;i++) t+=*--q; CHK(42, s==t); TRK(42,s);} // (An)+ vs -(An)
    { volatile int32_t idx=5; CHK(43, arr[idx] == *(arr+idx)); }   // (d8,An,Xn*4)

    // 5x register hazards (AR_IN_USE / DR_IN_USE back-to-back dependent ops)
    { volatile int32_t t=7; int32_t u=t; u=u*u; u=u+u; u=u^0x1F; CHK(50, u == (((7*7)*2)^0x1F)); TRK(50,u); }
    { int32_t x=a; x+=b; x-=b; x^=a; CHK(51, x==0); }              // dependent chain cancels

    // 6x control flow (Bcc/DBcc/Scc/JSR-RTS/loops)
    { int32_t n=0,i; for(i=0;i<100;i++) if(i&1) n++; CHK(60, n==50); }
    { int32_t f=1,i; for(i=1;i<=8;i++) f*=i; CHK(61, f==40320); }  // 8!

    // 7x MOVEM store (the process I split): load d3-d5 with known values, movem.l to memory, verify
    { int32_t k3=0x33330000,k4=0x44440000,k5=0x55550000; uint32_t buf[3];
      __asm__ volatile(
        "move.l %1,%%d3\n\t move.l %2,%%d4\n\t move.l %3,%%d5\n\t"
        "movem.l %%d3-%%d5,%0"
        : "=m"(buf) : "d"(k3),"d"(k4),"d"(k5) : "d3","d4","d5","memory");
      CHK(70, buf[0]==(uint32_t)k3 && buf[1]==(uint32_t)k4 && buf[2]==(uint32_t)k5); TRK(70,buf[0]^buf[1]^buf[2]); }

    // 8x PACK / UNPK (OP_SIZE_I terms I retimed) + round-trip
    { uint16_t pk; __asm__ volatile("move.w #0x0205,%%d0\n\t pack %%d0,%%d0,#0\n\t move.w %%d0,%0":"=d"(pk)::"d0");
      CHK(80, (uint8_t)pk == 0x25); TRK(80,pk); }
    { uint16_t up; __asm__ volatile("move.b #0x25,%%d0\n\t unpk %%d0,%%d0,#0\n\t move.w %%d0,%0":"=d"(up)::"d0");
      CHK(81, up == 0x0205); TRK(81,up); }                        // unpk(pack) round-trip

    // 9x ADDX 64-bit carry (extended arithmetic)
    { uint32_t rlo,rhi; __asm__ volatile(
        "move.l #-1,%%d0\n\t moveq #0,%%d1\n\t moveq #1,%%d2\n\t moveq #0,%%d3\n\t"
        "add.l %%d2,%%d0\n\t addx.l %%d3,%%d1\n\t move.l %%d0,%0\n\t move.l %%d1,%1"
        :"=&d"(rlo),"=&d"(rhi)::"d0","d1","d2","d3","cc");
      CHK(90, rlo==0u && rhi==1u); TRK(90,rlo^rhi); }

    // Ax bitfield (68020+) extract/insert round-trip
    { uint32_t v=0xDEADBEEFu, f; __asm__ volatile("bfextu %1{#8:#8},%0":"=d"(f):"d"(v));
      CHK(0xA0, f == ((v>>16)&0xFF)); TRK(0xA0,f); }
    { uint32_t v=0, ins=0x5A; __asm__ volatile("bfins %1,%0{#8:#8}":"+d"(v):"d"(ins));
      CHK(0xA1, ((v>>16)&0xFF)==0x5A); TRK(0xA1,v); }

    for(;;){
        if (g_fail==0){ uart_puts("SELFTEST: PASS  sum="); uart_hex32(g_sum); uart_putc('\n'); }
        else          { uart_puts("SELFTEST: FAIL id="); uart_hex32((uint32_t)g_fail); uart_puts(" sum="); uart_hex32(g_sum); uart_putc('\n'); }
        { volatile unsigned i; for(i=0;i<400000u;i++){} }
    }
}

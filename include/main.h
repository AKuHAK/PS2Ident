// #define DEBUG	1


#ifdef EE_UART
#include <SIOCookie.h>
#define DEBUG_INIT_PRINTF() ee_sio_start(38400, 0, 0, 0, 0, 1)
#else
#define DEBUG_INIT_PRINTF()
#endif

#ifdef DEBUG
#include <sio.h>
#include <stdio.h>
#define DEBUG_MSG_BUF_SIZE 512
#define DEBUG_PRINTF(args...) do { \
    char _debug_msg_buf[DEBUG_MSG_BUF_SIZE]; \
    snprintf(_debug_msg_buf, sizeof(_debug_msg_buf), args); \
    printf("%s", _debug_msg_buf); \
    sio_puts(_debug_msg_buf); \
} while (0)
#else
#define DEBUG_PRINTF(args...)
#endif

#ifdef COH_SUPPORT
#define PS2IDENT_VERSION "0.900-COH"
#else
#define PS2IDENT_VERSION "0.900"
#endif

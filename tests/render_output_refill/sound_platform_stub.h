#ifndef REFILL_TEST_PLATFORM_STUB_H
#define REFILL_TEST_PLATFORM_STUB_H
/* Host equivalents for the two string helpers used by sound.c. */
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif

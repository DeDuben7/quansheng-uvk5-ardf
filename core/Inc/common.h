/* Shared utility macros. Nothing module-specific lives here — if a macro
 * only applies to one module, it belongs in that module's header.
 */

#ifndef SCOUT_COMMON_H
#define SCOUT_COMMON_H

#include <stddef.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#endif

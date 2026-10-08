/* Version label, git commit + the marker string the release script looks for. */

#include "scout/version.h"

/* volatile: every read must really happen, so neither LTO nor
 * --gc-sections can fold the label into a constant and drop the marker.
 * Layout: "SCOUT-VERSION:" <version> "+" <commit>. */
static const volatile char s_tag[] = "SCOUT-VERSION:" SCOUT_VERSION "+" SCOUT_GIT_HASH;
#define TAG_PREFIX_LEN 14u   /* strlen("SCOUT-VERSION:") */

const char *scout_version_label(void)
{
    static char label[sizeof(SCOUT_VERSION) + 1];
    label[0] = 'v';
    unsigned i = 0;
    for (; i < sizeof(SCOUT_VERSION) - 1; i++)
        label[i + 1] = s_tag[TAG_PREFIX_LEN + i];
    label[i + 1] = '\0';
    return label;
}

const char *scout_git_hash(void)
{
    static char hash[sizeof(SCOUT_GIT_HASH)];
    const unsigned start = TAG_PREFIX_LEN + (sizeof(SCOUT_VERSION) - 1) + 1;   /* after '+' */
    for (unsigned i = 0; i < sizeof(SCOUT_GIT_HASH); i++)                      /* incl. NUL */
        hash[i] = s_tag[start + i];
    return hash;
}

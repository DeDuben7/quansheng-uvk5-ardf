/* Firmware release version. Bump it for EVERY build given to anyone: users
 * report bugs by the version on their screen, and each release EXE is named
 * after it. tools/release.ps1 checks that "SCOUT-VERSION:<this>+<commit>" is
 * present in both platform binaries. */
#ifndef SCOUT_VERSION_H
#define SCOUT_VERSION_H

#define SCOUT_VERSION "1.1"

/* Short git commit the firmware was built from ("8b8060a", "8b8060a-dirty"),
 * passed by the Makefile as -DSCOUT_GIT_HASH=\"...\". "dev" if unknown. */
#ifndef SCOUT_GIT_HASH
#define SCOUT_GIT_HASH "dev"
#endif

/* "v1.1" for on-screen display. */
const char *scout_version_label(void);

/* The commit the firmware was built from, e.g. "8b8060a" or "dev". */
const char *scout_git_hash(void);

#endif

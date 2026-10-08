/* scout host emulator: runs the real core/ firmware on the PC.
 *
 *   scout-emu.exe <scenes.txt> <outdir> <scene>   run one scene
 *   scout-emu.exe <scenes.txt> <outdir>           run every scene, each in
 *                                                 its own process (the app's
 *                                                 static state must start fresh)
 *
 * A scene boots exactly like V1's Main() and then lives inside
 * scout_app_run(); the script (emu_script.c) ends the process.
 */

#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_df/audio_df.h"
#include "battery/battery.h"
#include "bk4819/bk4819.h"
#include "calib/calib.h"
#include "emu/emu.h"
#include "peripherals/system.h"
#include "radio/radio.h"
#include "scout/app.h"
#include "scout/config.h"
#include "st7565/st7565.h"
#include "ui/welcome.h"

static void boot(void)
{
    ST7565_Init();
    BK4819_Init();
    UI_DisplayWelcome();

    static const radio_config_t radio_cfg = {
        .initial_freq_hz    = SCOUT_BOOT_FREQ_HZ,
        .initial_modulation = SCOUT_BOOT_MODULATION,
        .initial_bandwidth  = SCOUT_BOOT_BANDWIDTH,
        .initial_squelch    = SCOUT_SQL_DEFAULT,
    };
    radio_init(&radio_cfg);

    const battery_config_t bat_cfg = {
        .adc_760  = calib_battery_adc760(),
        .read_adc = emu_read_adc,
    };
    battery_init(&bat_cfg);

    static const audio_df_config_t df_cfg = {
        .rssi_floor = 40,
        .rssi_ceil  = 200,
        .tone_gain  = 96,
    };
    audio_df_init(&df_cfg);

    SYSTEM_DelayMs(1500);
    scout_app_run();
}

/* Next scene line from the file: skips blanks and # comments, strips the
 * line end. Returns 0 at end of file. */
static int next_scene(FILE *f, char *line, size_t size)
{
    while (fgets(line, (int)size, f))
    {
        line[strcspn(line, "\r\n")] = '\0';
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '\0' && *p != '#' && strchr(p, ':'))
            return 1;
    }
    return 0;
}

static void scene_name(const char *line, char *name, size_t size)
{
    while (*line == ' ' || *line == '\t') line++;
    size_t n = strcspn(line, ":");
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) n--;
    if (n >= size) n = size - 1;
    memcpy(name, line, n);
    name[n] = '\0';
}

static int run_scene(const char *file, const char *outdir, const char *want)
{
    FILE *f = fopen(file, "r");
    if (!f) { perror(file); return 2; }
    char line[2048], name[64];
    while (next_scene(f, line, sizeof(line)))
    {
        scene_name(line, name, sizeof(name));
        if (strcmp(name, want) != 0)
            continue;
        fclose(f);
        if (emu_script_load(line, outdir) != 0)
            return 2;
        boot();          /* never returns: the script exits */
        return 2;
    }
    fclose(f);
    fprintf(stderr, "no scene '%s' in %s\n", want, file);
    return 2;
}

static void quote(char *dst, size_t size, const char *s)
{
    snprintf(dst, size, "\"%s\"", s);
}

static int run_all(const char *file, const char *outdir)
{
    FILE *f = fopen(file, "r");
    if (!f) { perror(file); return 2; }

    char exe[1024], qexe[1100], qfile[1100], qout[1100], qname[100];
    snprintf(exe, sizeof(exe), "%s", _pgmptr);
    quote(qexe, sizeof(qexe), exe);
    quote(qfile, sizeof(qfile), file);
    quote(qout, sizeof(qout), outdir);

    int failures = 0, total = 0;
    char line[2048], name[64];
    while (next_scene(f, line, sizeof(line)))
    {
        scene_name(line, name, sizeof(name));
        quote(qname, sizeof(qname), name);
        fflush(stdout);
        const intptr_t rc = _spawnl(_P_WAIT, exe, qexe, qfile, qout, qname, (char *)NULL);
        total++;
        if (rc == 0) printf("ok   %s\n", name);
        else       { printf("FAIL %s\n", name); failures++; }
    }
    fclose(f);
    printf("%d/%d scenes passed\n", total - failures, total);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 4)
        return run_scene(argv[1], argv[2], argv[3]);
    if (argc == 3)
        return run_all(argv[1], argv[2]);
    fprintf(stderr, "usage: %s <scenes.txt> <outdir> [scene]\n", argv[0]);
    return 2;
}

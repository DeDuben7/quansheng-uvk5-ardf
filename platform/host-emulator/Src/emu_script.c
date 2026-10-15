/* Scene scripts: one scene per line, "name: cmd; cmd; ...".
 *
 *   wait <ms>                 do nothing for <ms>
 *   press <KEY>               key down 60 ms, up, 150 ms idle   (short press)
 *   hold <KEY>                key down 700 ms, up, 150 ms idle  (long press)
 *   rssi <n>                  BK4819 RSSI register reads <n>
 *   signal <hz> <rssi>        a carrier on <hz>: reads <rssi> when tuned there
 *   sql open|close            a carrier comes on / goes off the air
 *                             (raises SQUELCH_LOST / SQUELCH_FOUND)
 *   calib blank               squelch tables read as erased (use first)
 *   count reset               zero the RX-restart counter (expect restarts)
 *   snap                      write <outdir>/<scene>.png
 *   expect <what> <value>     audio on|off (speaker path), af mute|fm|am|alam
 *                             (BK4819 AF output, REG_47), freq <hz>,
 *                             mod FM|AM, df on|off, gain <n>, sql <n>,
 *                             restarts <n>, reg <hex> <hex>,
 *                             bar <px> (filled width of the home-screen
 *                             RSSI bar, 0..128, as last rendered),
 *                             light on|off (LCD backlight)
 *
 * Keys: 0-9 MENU UP DOWN EXIT STAR F PTT SIDE1 SIDE2.
 * Actions run on the simulated clock (emu_script_pump is called every
 * simulated millisecond). After the last action the process exits 0; the
 * first failed expect prints to stderr and exits 1.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_df/audio_df.h"
#include "bk4819/bk4819.h"
#include "bk4819/bk4819_regs.h"
#include "emu/emu.h"
#include "radio/radio.h"
#include "st7565/st7565.h"

#define MAX_ACTIONS 256

typedef enum {
    A_WAIT, A_KEYDOWN, A_KEYUP, A_RSSI, A_SIGNAL, A_SQL, A_CALIB_BLANK, A_COUNT_RESET, A_SNAP, A_EXPECT
} action_type_t;

typedef struct {
    action_type_t type;
    uint32_t      duration_ms;
    int           key;            /* KEY_Code_t, or -1 for PTT */
    long          value;          /* rssi, expected number, register */
    long          value2;         /* expected register value */
    char          what[16];       /* expect subject / sql direction */
    char          text[96];       /* original command, for messages */
} action_t;

static action_t   s_actions[MAX_ACTIONS];
static int        s_count;
static int        s_pos;
static uint32_t   s_next_at;
static char       s_scene[64];
static char       s_outdir[512];
static int        s_snaps;
static KEY_Code_t s_key = KEY_INVALID;
static bool       s_ptt;

#define KEY_PTT_PSEUDO (-1)

KEY_Code_t emu_key_state(void) { return s_key; }
bool       emu_ptt_state(void) { return s_ptt; }

static int parse_key(const char *name)
{
    static const struct { const char *name; int key; } KEYS[] = {
        { "MENU", KEY_MENU }, { "UP", KEY_UP }, { "DOWN", KEY_DOWN },
        { "EXIT", KEY_EXIT }, { "STAR", KEY_STAR }, { "F", KEY_F },
        { "SIDE1", KEY_SIDE1 }, { "SIDE2", KEY_SIDE2 }, { "PTT", KEY_PTT_PSEUDO },
    };
    if (name[0] >= '0' && name[0] <= '9' && name[1] == '\0')
        return KEY_0 + (name[0] - '0');
    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++)
        if (strcmp(name, KEYS[i].name) == 0)
            return KEYS[i].key;
    return -2;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static action_t *add(action_type_t type, uint32_t duration_ms, const char *text)
{
    if (s_count >= MAX_ACTIONS)
    {
        fprintf(stderr, "scene %s: too many actions\n", s_scene);
        exit(2);
    }
    action_t *a = &s_actions[s_count++];
    memset(a, 0, sizeof(*a));
    a->type = type;
    a->duration_ms = duration_ms;
    snprintf(a->text, sizeof(a->text), "%s", text);
    return a;
}

static int parse_command(char *cmd)
{
    char text[96];
    snprintf(text, sizeof(text), "%s", cmd);
    char *argv[4] = { 0 };
    int argc = 0;
    for (char *tok = strtok(cmd, " \t"); tok && argc < 4; tok = strtok(NULL, " \t"))
        argv[argc++] = tok;
    if (argc == 0)
        return 0;

    if (strcmp(argv[0], "wait") == 0 && argc == 2)
        add(A_WAIT, (uint32_t)strtoul(argv[1], NULL, 10), text);
    else if ((strcmp(argv[0], "press") == 0 || strcmp(argv[0], "hold") == 0) && argc == 2)
    {
        const int key = parse_key(argv[1]);
        if (key == -2) goto bad;
        add(A_KEYDOWN, argv[0][0] == 'p' ? 60u : 700u, text)->key = key;
        add(A_KEYUP, 150u, text)->key = key;
    }
    else if (strcmp(argv[0], "rssi") == 0 && argc == 2)
        add(A_RSSI, 0, text)->value = strtol(argv[1], NULL, 10);
    else if (strcmp(argv[0], "signal") == 0 && argc == 3)
    {
        action_t *a = add(A_SIGNAL, 0, text);
        a->value  = strtol(argv[1], NULL, 10);
        a->value2 = strtol(argv[2], NULL, 10);
    }
    else if (strcmp(argv[0], "sql") == 0 && argc == 2
             && (strcmp(argv[1], "open") == 0 || strcmp(argv[1], "close") == 0))
        snprintf(add(A_SQL, 0, text)->what, 16, "%s", argv[1]);
    else if (strcmp(argv[0], "calib") == 0 && argc == 2 && strcmp(argv[1], "blank") == 0)
        add(A_CALIB_BLANK, 0, text);
    else if (strcmp(argv[0], "count") == 0 && argc == 2 && strcmp(argv[1], "reset") == 0)
        add(A_COUNT_RESET, 0, text);
    else if (strcmp(argv[0], "snap") == 0 && argc == 1)
        add(A_SNAP, 0, text);
    else if (strcmp(argv[0], "expect") == 0 && argc >= 3)
    {
        action_t *a = add(A_EXPECT, 0, text);
        snprintf(a->what, sizeof(a->what), "%s", argv[1]);
        if (strcmp(argv[1], "reg") == 0)
        {
            if (argc != 4) goto bad;
            a->value  = strtol(argv[2], NULL, 16);
            a->value2 = strtol(argv[3], NULL, 16);
        }
        else if (strcmp(argv[1], "audio") == 0 || strcmp(argv[1], "df") == 0
                 || strcmp(argv[1], "light") == 0)
            a->value = strcmp(argv[2], "on") == 0;
        else if (strcmp(argv[1], "mod") == 0)
            a->value = strcmp(argv[2], "AM") == 0 ? RADIO_MOD_AM : RADIO_MOD_FM;
        else if (strcmp(argv[1], "af") == 0)
        {
            if      (strcmp(argv[2], "mute") == 0) a->value = BK4819_AF_MUTE;
            else if (strcmp(argv[2], "fm") == 0)   a->value = BK4819_AF_FM;
            else if (strcmp(argv[2], "am") == 0)   a->value = BK4819_AF_AM;
            else if (strcmp(argv[2], "alam") == 0) a->value = BK4819_AF_ALAM;
            else goto bad;
        }
        else
            a->value = strtol(argv[2], NULL, 10);
    }
    else
        goto bad;
    return 0;

bad:
    fprintf(stderr, "scene %s: cannot parse '%s'\n", s_scene, text);
    return -1;
}

int emu_script_load(const char *scene_line, const char *outdir)
{
    char line[2048];
    snprintf(line, sizeof(line), "%s", scene_line);
    char *colon = strchr(line, ':');
    if (!colon)
        return -1;
    *colon = '\0';
    snprintf(s_scene, sizeof(s_scene), "%s", trim(line));
    snprintf(s_outdir, sizeof(s_outdir), "%s", outdir);

    char *rest = colon + 1;
    for (char *cmd = rest; cmd; )
    {
        char *semi = strchr(cmd, ';');
        if (semi) *semi = '\0';
        if (parse_command(trim(cmd)) != 0)
            return -1;
        cmd = semi ? semi + 1 : NULL;
    }
    return 0;
}

static long actual_value(const action_t *a, bool *known)
{
    *known = true;
    if (strcmp(a->what, "audio") == 0) return emu_audio_path_on();
    if (strcmp(a->what, "freq") == 0)  return (long)radio_get_frequency();
    if (strcmp(a->what, "mod") == 0)   return radio_get_modulation();
    if (strcmp(a->what, "df") == 0)    return audio_df_is_active();
    if (strcmp(a->what, "light") == 0) return emu_backlight_on();
    if (strcmp(a->what, "gain") == 0)  return radio_get_gain_index();
    if (strcmp(a->what, "sql") == 0)   return radio_get_squelch();
    if (strcmp(a->what, "restarts") == 0) return (long)emu_bk_rx_restarts();
    if (strcmp(a->what, "reg") == 0)   return emu_bk_reg((uint8_t)a->value);
    if (strcmp(a->what, "af") == 0)    return (emu_bk_reg(0x47) >> 8) & 0x0Fu;   /* BK4819_SetAF */
    if (strcmp(a->what, "bar") == 0)
    {
        /* Home screen, frame-buffer page 5: filled columns are 0xFF from
         * the left, the empty tube is 0x81 (scout/home.c draw_bargraph). */
        long px = 0;
        while (px < LCD_WIDTH && gFrameBuffer[5][px] == 0xFF) px++;
        return px;
    }
    *known = false;
    return 0;
}

static void run_expect(const action_t *a)
{
    bool known;
    const long got = actual_value(a, &known);
    const long want = (strcmp(a->what, "reg") == 0) ? a->value2 : a->value;
    if (!known)
    {
        fprintf(stderr, "FAIL %s: unknown expectation '%s'\n", s_scene, a->text);
        exit(1);
    }
    if (got != want)
    {
        if (strcmp(a->what, "reg") == 0)
            fprintf(stderr, "FAIL %s: %s (got %04lX)\n", s_scene, a->text, got);
        else
            fprintf(stderr, "FAIL %s: %s (got %ld)\n", s_scene, a->text, got);
        exit(1);
    }
}

static void run(const action_t *a)
{
    switch (a->type)
    {
        case A_WAIT:
            break;
        case A_KEYDOWN:
            if (a->key == KEY_PTT_PSEUDO) s_ptt = true;
            else                          s_key = (KEY_Code_t)a->key;
            break;
        case A_KEYUP:
            if (a->key == KEY_PTT_PSEUDO) s_ptt = false;
            else                          s_key = KEY_INVALID;
            break;
        case A_RSSI:
            emu_bk_set_rssi((uint16_t)a->value);
            break;
        case A_SIGNAL:
            emu_bk_add_station((uint32_t)a->value, (uint16_t)a->value2);
            break;
        case A_SQL:
            emu_bk_set_carrier(strcmp(a->what, "open") == 0);   /* reald: LOST = opened */
            break;
        case A_CALIB_BLANK:
            g_emu_calib_blank_squelch = true;
            break;
        case A_COUNT_RESET:
            emu_bk_reset_counts();
            break;
        case A_SNAP:
        {
            char path[600];
            if (++s_snaps == 1) snprintf(path, sizeof(path), "%s/%s.png", s_outdir, s_scene);
            else                snprintf(path, sizeof(path), "%s/%s-%d.png", s_outdir, s_scene, s_snaps);
            if (emu_png_write(path) != 0)
            {
                fprintf(stderr, "FAIL %s: cannot write %s\n", s_scene, path);
                exit(1);
            }
            break;
        }
        case A_EXPECT:
            run_expect(a);
            break;
    }
}

void emu_script_pump(void)
{
    while (s_pos < s_count && emu_now_ms() >= s_next_at)
    {
        const action_t *a = &s_actions[s_pos++];
        run(a);
        s_next_at = emu_now_ms() + a->duration_ms;
    }
    if (s_pos >= s_count && emu_now_ms() >= s_next_at)
    {
        fflush(stdout);
        exit(0);
    }
}

/* Settings menu (short MENU on the home screen). Works like the stock
 * UV-K5 menu: UP/DOWN pick an item, MENU edits or opens it, UP/DOWN change
 * the value (applied immediately), MENU confirms, EXIT cancels / leaves.
 *
 * Items: Squelch (0-9), Modulatie (FM/AM), Band (2 m / 70 cm), Scanner,
 * Info. Settings are not stored; power-on defaults are in scout/config.h.
 */
#ifndef SCOUT_MENU_H
#define SCOUT_MENU_H

#include "peripherals/keyboard.h"

typedef enum {
    MENU_STAY = 0,       // still in the menu
    MENU_EXIT_HOME,      // EXIT from the item list
    MENU_OPEN_SCANNER,
    MENU_OPEN_INFO
} menu_result_t;

void          menu_enter(void);                 // list mode; the cursor is kept between visits
menu_result_t menu_handle_key(KEY_Code_t key);  // short presses only
void          menu_render(void);

#endif

/* Info screen (Menu > Info): factory calibration as read from this radio,
 * and a live squelch readout on a second page. */
#ifndef SCOUT_INFO_H
#define SCOUT_INFO_H

void scout_info_render(void);
void scout_info_toggle_page(void);   // UP/DOWN: factory cal <-> squelch

#endif

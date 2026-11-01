#ifndef FILM_RETRO_NATIVE_H
#define FILM_RETRO_NATIVE_H
struct event;
/* Called only from checked patches of the pinned native event chain.
 * 0 consumed by dashboard, 1 unchanged chain, 2 native handoff: only skip
 * the AV menu shortcut and ML menu actions, retaining all safety handlers. */
int film_retro_event_gate(struct event *event);
int film_retro_menu_passthrough(struct event *event);
#endif

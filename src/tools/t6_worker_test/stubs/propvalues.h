int get_halfshutter_pressed(void);
int is_movie_mode(void);
extern int lv, shooting_mode;
extern int __recording, __recording_custom;
#define RECORDING (__recording || __recording_custom)
#define SHOOTMODE_P 0
#define SHOOTMODE_ADEP 1
#define SHOOTMODE_AV 2
#define SHOOTMODE_M 3
#define SHOOTMODE_TV 4

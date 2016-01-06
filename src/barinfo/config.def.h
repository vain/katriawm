char *layout_names[LALast] = {
    [LAFloat] = "><>",
    [LAMonocle] = "[M]",
    [LATile] = "[]=",
};

char *ws_names[WORKSPACE_MAX + 1] = {
    [1] = "www",
    [7] = "irc",
};

#define S_LEMONBAR_BG "#1a1a1a"
#define S_LEMONBAR_FG "#bebebe"
char *s_lemonbar_norm = "%{B"S_LEMONBAR_BG"}%{F"S_LEMONBAR_FG"}";
char *s_lemonbar_sele = "%{B#555555}%{F"S_LEMONBAR_FG"}";
char *s_lemonbar_selmon = "%{B#444444}%{F"S_LEMONBAR_FG"}";
char *s_lemonbar_urg = "%{B"S_LEMONBAR_FG"}%{F"S_LEMONBAR_BG"}";

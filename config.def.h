struct WorkareaInsets wai = {
    .top = 20,
    .left = 0,
    .right = 0,
    .bottom = 20,
};

struct Rule rules[] = {
    /* xprop(1):  WM_CLASS(STRING) = instance, class  */

    /* If both class and instance match a window, the defined actions
     * are taken. If "workspace", "monitor", or "floating" is -1, then
     * the corresponding field is not changed. If "class" or "instance"
     * is NULL, then this field matches everything.
     *
     * Both monitor indexes and workspace indexes start at 0. */

    /* class   instance   workspace   monitor  floating */
    {  NULL,   NULL,      -1,         -1,      -1  },
};

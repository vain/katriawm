#ifndef _WM_IPC_H
#define _WM_IPC_H

#define IPC_ATOM_WINDOW "_"__NAME_UPPERCASE__"_COMMAND_WINDOW"
#define IPC_ATOM_COMMAND "_"__NAME_UPPERCASE__"_CLIENT_COMMAND"

enum IPCCommand
{
    /* These commands shall use the following name scheme:
     *
     *     $subject $predicate $adverb
     *
     * For example:
     *
     *     IPCClientMoveMouse
     *        \____/\__/\___/
     *           |    |   |
     *           |    |   \- $adverb, i.e. how it is done or by which
     *           |    |      means
     *           |    |
     *           |    \- $predicate, i.e. what is to be done
     *           |
     *           \- $subject, i.e. what is affected by this action
     */
    IPCClientMoveMouse = 0,
    IPCClientResizeMouse,
    IPCClientSelectAdjacent,
    IPCLayoutSet,
    IPCMonitorSelectAdjacent,
    IPCWMQuit,
    IPCWMRestart,
    IPCWorkspaceSelect,
    IPCWorkspaceSelectAdjacent,

    IPCLast,
};

enum LayoutAlgorithm
{
    LATile = 0,
    LAMonocle,
    LAFloat,

    LALast,
};

#endif /* _WM_IPC_H */

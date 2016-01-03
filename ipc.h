#ifndef _WM_IPC_H
#define _WM_IPC_H

#define IPC_ATOM_COMMAND "_"__NAME_UPPERCASE__"_CLIENT_COMMAND"
#define IPC_ATOM_STATE "_"__NAME_UPPERCASE__"_STATE"

enum IPCCommand
{
    /* These commands shall use the following name scheme:
     *
     *     $subject $predicate $adverb [$adverb ...]
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
    IPCClientCenterFloating = 0,
    IPCClientClose,
    IPCClientFloatingToggle,
    IPCClientFullscreenToggle,
    IPCClientKill,
    IPCClientMaximizeFloating,
    IPCClientMoveList,
    IPCClientMoveMouse,
    IPCClientResizeMouse,
    IPCClientSelectAdjacent,
    IPCClientSelectRecent,
    IPCClientSwitchMonitorAdjacent,
    IPCClientSwitchWorkspace,
    IPCClientSwitchWorkspaceAdjacent,
    IPCLayoutSet,
    IPCMonitorSelectAdjacent,
    IPCMonitorSelectRecent,
    IPCPlacementStore,
    IPCPlacementUse,
    IPCUrgencyClearVisible,
    IPCWMQuit,
    IPCWMRestart,
    IPCWorkspaceSelect,
    IPCWorkspaceSelectAdjacent,
    IPCWorkspaceSelectRecent,

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

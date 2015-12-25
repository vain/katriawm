#ifndef _WM_IPC_H
#define _WM_IPC_H

#define IPC_ATOM_WINDOW "_"__NAME_UPPERCASE__"_COMMAND_WINDOW"
#define IPC_ATOM_COMMAND "_"__NAME_UPPERCASE__"_CLIENT_COMMAND"

enum IPCCommand
{
    IPCLayout = 0,
    IPCMouseMove,
    IPCMouseResize,
    IPCNavMonitor,
    IPCNavWorkspace,
    IPCNavWorkspaceAdj,
    IPCQuit,
    IPCRestart,

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

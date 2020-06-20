#ifndef INSTRUMENTATION_H
#define INSTRUMENTATION_H

/* This header file ties together C++ and the C logging library-code */

enum type {
    STRING, POINTER, INT32, END, FUNCTIONCALL, PT, GLOBALVAR, POINTEROF, LOCALVAR
};
enum pt_call {
    PT_CREATE, PT_LOCK, PT_UNLOCK, PT_JOIN, PT_INIT
};
enum var_access {
    VA_R, VA_W
};

#if defined(__APPLE__)
#define NAME_PTHREAD_JOIN "\01_pthread_join"
#else
#define NAME_PTHREAD_JOIN "pthread_join"
#endif

#endif

//
//  signal_guard.h
//  objsee
//
//  Created by Ethan Arbuckle on 2/5/25.
//

#ifndef signal_guard_h
#define signal_guard_h

#include <CoreFoundation/CoreFoundation.h>
#include <signal.h>
#include <os/log.h>


/**
 * Block signals while executing a block of code.
 * @param code The block of code to execute.
 * @return The result of the block of code.
 * @note Blocked signals are queued and will eventually be delivered
 */
#define WHILE_BLOCKING_SIGNALS(code) ({ \
    __typeof__(code) __result = 0; \
    sigset_t block_mask, old_mask; \
    if (sigfillset(&block_mask) == 0 && sigprocmask(SIG_BLOCK, &block_mask, &old_mask) == 0) { \
        __result = code; \
        sigprocmask(SIG_SETMASK, &old_mask, NULL); \
    } \
    __result; \
})

/**
 * Execute a block of code while ignoring signals.
 * @param code The block of code to execute.
 * @return The result of the block of code.
 * @note The signals will never hit the original signal handler.
 */
#define WHILE_IGNORING_SIGNALS(code) do { \
    if (!g_sig_ignoring && g_sig_ignore_depth < 2) { \
        signal_handlers_t __old_handlers; \
        g_sig_ignore_depth++; \
        g_sig_ignoring = 1; \
        \
        if (install_signal_handlers(&__old_handlers) == 0) { \
            if (sigsetjmp(g_sig_ignore_jmpbuf, 1) == 0) { \
                code; \
        } \
        else { } \
        restore_signal_handlers(&__old_handlers); \
        } \
        g_sig_ignoring = 0; \
        g_sig_ignore_depth--; \
    } \
} while(0)


static __thread sigjmp_buf g_sig_ignore_jmpbuf;
static __thread volatile sig_atomic_t g_sig_ignoring = 0;
static __thread int g_sig_ignore_depth = 0;

typedef struct {
    struct sigaction sa_segv;
    struct sigaction sa_bus;
    struct sigaction sa_kill;
    struct sigaction sa_ill;
    struct sigaction sa_fpe;
} signal_handlers_t;

static void _sig_ignoring_handler(int signo) {
    if (g_sig_ignoring) {
        os_log(OS_LOG_DEFAULT, "Handler called for signal %d at depth %d\n", signo, g_sig_ignore_depth);
        siglongjmp(g_sig_ignore_jmpbuf, signo);
    }
}

static inline bool install_signal_handlers(signal_handlers_t *old_handlers) {
    // Block signals while installing the new handlers
    return WHILE_BLOCKING_SIGNALS(({
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = _sig_ignoring_handler;
        sa.sa_flags = 0;
        sigemptyset(&sa.sa_mask);
        
        sigaction(SIGSEGV, &sa, &old_handlers->sa_segv) == 0 &&
        sigaction(SIGBUS, &sa, &old_handlers->sa_bus) == 0 &&
        sigaction(SIGKILL, &sa, &old_handlers->sa_kill) == 0 &&
        sigaction(SIGILL, &sa, &old_handlers->sa_ill) == 0 &&
        sigaction(SIGFPE, &sa, &old_handlers->sa_fpe) == 0;
    }));
}

static inline void restore_signal_handlers(const signal_handlers_t *old_handlers) {
    WHILE_BLOCKING_SIGNALS(({
        sigaction(SIGSEGV, &old_handlers->sa_segv, NULL);
        sigaction(SIGBUS, &old_handlers->sa_bus, NULL);
        sigaction(SIGKILL, &old_handlers->sa_kill, NULL);
        sigaction(SIGILL, &old_handlers->sa_ill, NULL);
        sigaction(SIGFPE, &old_handlers->sa_fpe, NULL);
    }));
}

#endif /* signal_guard_h */

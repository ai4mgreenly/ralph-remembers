#ifndef NDEBUG

// Debug only: weak symbol implementations, overridable by strong symbols in tests

#include "wrapper_posix.h"

#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <sys/fanotify.h>
#include <unistd.h>

MOCKABLE int32_t posix_fanotify_init_(uint32_t flags, uint32_t event_f_flags)
{
    return (int32_t)fanotify_init(flags, event_f_flags);
}

MOCKABLE int32_t posix_fanotify_mark_(int32_t fanotify_fd,
                                      uint32_t flags,
                                      uint64_t mask,
                                      int32_t dirfd,
                                      const char *pathname)
{
    return (int32_t)fanotify_mark((int)fanotify_fd, flags, mask, (int)dirfd, pathname);
}

MOCKABLE int32_t posix_open_(const char *pathname, int32_t flags)
{
    return (int32_t)open(pathname, (int)flags);
}

MOCKABLE int32_t posix_close_(int32_t fd)
{
    return (int32_t)close((int)fd);
}

MOCKABLE int32_t posix_poll_(struct pollfd *fds, nfds_t nfds, int32_t timeout)
{
    return (int32_t)poll(fds, nfds, (int)timeout);
}

#endif // NDEBUG

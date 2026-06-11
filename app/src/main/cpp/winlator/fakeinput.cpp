#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <algorithm>
#include <mutex>
#include <set>

#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <poll.h>
#include <linux/input.h>

// Extract direction, type, number from ioctl code
// Direction: bits 30-31 (_IOC_NONE=0, _IOC_WRITE=1, _IOC_READ=2)
// Size: bits 16-29
// Type: bits 8-15
// Number: bits 0-7
#define IOCTL_DIR(op)    (((op) >> 30) & 3)
#define IOCTL_TYPE(op)   (((op) >> 8) & 0xFF)
#define IOCTL_NR(op)     ((op) & 0xFF)
#define IOCTL_SIZE(op)   (((op) >> 16) & 0x3FFF)
#define IOCTL_READ       2
#define IOCTL_WRITE      1
#define IOCTL_EV_TYPE    'E'
#define IOCTL_JS_TYPE    'j'

#define DLSYM_OR_FALLBACK(var, name, fallback_expr) \
    do { \
        if (!(var)) *(void **)&(var) = dlsym(RTLD_NEXT, (name)); \
        if (!(var)) return (fallback_expr); \
    } while(0)

#define EXPORT __attribute__((visibility("default"))) extern "C"

std::unordered_map<int, const char *> controller_map;
static std::mutex controller_map_mutex;
static bool signal_handler_initialized = false;
static bool lazy_init_done = false;
static const char *hook_dir = nullptr;
static bool vibration_enabled = true;
volatile sig_atomic_t stop_flag = 0;
static std::set<int> closed_fds;

// POD flag — zero in BSS at load time, set to 1 after all C++ statics
// (controller_map, mutex, closed_fds, ff_effects) are constructed.
// During __libc_preinit_impl hooks fire before our .init_array runs;
// this guard makes them pass through via syscall without touching C++ objects.
volatile int g_hooks_ready = 0;

static int (*my_open)(const char *, int, ...) = nullptr;
static int (*my_openat)(int, const char *, int, ...) = nullptr;
static int (*my_stat)(const char *, struct stat *) = nullptr;
static int (*my_fstat)(int fd, struct stat *buf) = nullptr;
static int (*my_scandir)(const char *, struct dirent***, int(*)(const struct dirent *), int(*)(const struct dirent**, const struct dirent**));
static int (*my_inotify_add_watch)(int, const char *, uint32_t);
static int (*my_close)(int);
static ssize_t (*my_write)(int, const void *, size_t) = nullptr;
static ssize_t (*real_writev)(int, const struct iovec *, int) = nullptr;

extern char **environ;

static const char* getenv_safe(const char* name) {
    if (!environ) return nullptr;
    size_t len = strlen(name);
    for (char** env = environ; *env; env++) {
        if (strncmp(*env, name, len) == 0 && (*env)[len] == '=') {
            return *env + len + 1;
        }
    }
    return nullptr;
}

namespace Logger {
	int log_enabled;

	void init() {
		const char *log = getenv_safe("FAKE_EVDEV_LOG");
		log_enabled = log && atoi(log);
	}

	void log(const char *message, ...) {
		if (!log_enabled)
			return;

		va_list args;
		va_start(args, message);
		vfprintf(stderr, message, args);
		va_end(args);

		std::cerr.flush();
	}
}

void handle_sigint(int sig)  { 
    stop_flag = 1;
} 

void setup_signal_handler() {
    if (!signal_handler_initialized) {
        signal(SIGINT, handle_sigint);
        signal_handler_initialized = true;
    }
}

static void lazy_init() {
    if (__builtin_expect(lazy_init_done, 1)) return;
    lazy_init_done = true;

    const char *dir = getenv_safe("FAKE_EVDEV_DIR");
    if (dir) hook_dir = strdup(dir);
    if (!hook_dir)
        hook_dir = strdup("/data/data/com.termux/files/home/fake-input");

    const char *vib = getenv_safe("FAKE_EVDEV_VIBRATION");
    vibration_enabled = vib && atoi(vib);

    const char *log = getenv_safe("FAKE_EVDEV_LOG");
    Logger::log_enabled = log && atoi(log);
}

static std::unordered_map<int, struct ff_effect> ff_effects;
static int next_ff_id = 0;

void send_vibration(int strong, int weak, uint16_t duration_ms, uint16_t slot) {
  if (!vibration_enabled)
    return;

  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0)
    return;

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  const char *name = "winlator_vibration";
  memcpy(addr.sun_path + 1, name, strlen(name));
  socklen_t addrlen = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(name);

  if (connect(sock, (struct sockaddr *)&addr, addrlen) < 0) {
    syscall(SYS_close, sock);
    return;
  }

  uint16_t data[4];
  data[0] = (uint16_t)strong;
  data[1] = (uint16_t)weak;
  data[2] = duration_ms;
  data[3] = slot;
  send(sock, data, sizeof(data), 0);
  syscall(SYS_close, sock);
}

__attribute__((constructor))
static void library_init() {
    // C++ statics (controller_map, mutex, closed_fds, ff_effects) are done.
    g_hooks_ready = 1;
	// Deferred to lazy_init() — getenv() in constructors crashes
	// on Android 13+ (MIUI/HyperOS) when loaded via LD_PRELOAD
	// because __system_properties_init hasn't run yet.
}
  
__attribute__((visibility("hidden"))) 
char *from_real_to_fake_path(const char *pathname) {
	const char *event = strrchr(pathname, '/');
	if (!event) return nullptr;
	event++;
	char *fake_path;
	if (asprintf(&fake_path, "%s/%s", hook_dir, event) < 0) return nullptr;
	return fake_path;
}

__attribute__((visibility("hidden")))
const char *get_event(const char *pathname) {
	const char *slash = strrchr(pathname, '/');
	if (!slash) return pathname;
	return slash + 1;
}

__attribute__((visibility("hidden")))
int get_event_number(const char *event) {
    const char *p = event;
    while (*p && !(*p >= '0' && *p <= '9')) p++;
    if (!*p) return 0;
    return atoi(p);
}

struct PathRedirect {
    char* fake_path;
    const char* resolved;
    bool is_from_input;
};

static PathRedirect redirect_path(const char* pathname) {
    PathRedirect r = {nullptr, pathname, false};
    if (!pathname) return r;
    if (strstr(pathname, "/dev/input/event")) {
        r.fake_path = from_real_to_fake_path(pathname);
        if (r.fake_path) {
            r.resolved = r.fake_path;
            r.is_from_input = true;
        }
    } else if (!strcmp(pathname, "/dev/input")) {
        r.resolved = hook_dir;
    }
    return r;
}

EXPORT int open(const char *pathname, int flags, ...) {
    if (!g_hooks_ready) {
        va_list va; va_start(va, flags);
        mode_t m = flags & O_CREAT ? va_arg(va, mode_t) : 0; va_end(va);
        return syscall(SYS_openat, AT_FDCWD, pathname, flags, m);
    }
    lazy_init();
    va_list va;
    mode_t mode;
    int fd;
    bool hasMode;

    va_start(va, flags);

    hasMode = flags & O_CREAT;
    
    if (hasMode) {
        mode = va_arg(va, mode_t);
    }
    
	va_end(va);

	DLSYM_OR_FALLBACK(my_open, "open", syscall(SYS_openat, AT_FDCWD, pathname, flags, hasMode ? mode : 0));

	PathRedirect pr = redirect_path(pathname);
	pathname = pr.resolved;
    
	if (hasMode)
	    fd = my_open(pathname, flags, mode);
	else
	    fd = my_open(pathname, flags);

	if (pr.is_from_input && fd >= 0) {
		Logger::log("Adding controller, fd %d event %s\n", fd, get_event(pathname));
		{
		    std::lock_guard<std::mutex> lock(controller_map_mutex);
		    closed_fds.erase(fd);
		    controller_map[fd] = strdup(get_event(pathname));
		}
    }
	    
	free(pr.fake_path);
	return fd;
}

EXPORT int openat(int dirfd, const char *pathname, int flags, ...) {
    if (!g_hooks_ready) {
        va_list va; va_start(va, flags);
        mode_t m = flags & O_CREAT ? va_arg(va, mode_t) : 0; va_end(va);
        return syscall(SYS_openat, dirfd, pathname, flags, m);
    }
    lazy_init();
    va_list va;
    mode_t mode;
    int fd;
    bool hasMode;
    
    va_start(va, flags);

    hasMode = flags & O_CREAT;
    
    if (hasMode) {
        mode = va_arg(va, mode_t);
    }
    
    va_end(va);

    DLSYM_OR_FALLBACK(my_openat, "openat", syscall(SYS_openat, dirfd, pathname, flags, hasMode ? mode : 0));
    
    PathRedirect pr = redirect_path(pathname);
    pathname = pr.resolved;
    
    if (hasMode)
        fd = my_openat(dirfd, pathname, flags, mode);
    else
        fd = my_openat(dirfd, pathname, flags);

    if (pr.is_from_input && fd >= 0) {
        Logger::log("Adding controller, fd %d event %s\n", fd, get_event(pathname));
        {
            std::lock_guard<std::mutex> lock(controller_map_mutex);
            closed_fds.erase(fd);
            controller_map[fd] = strdup(get_event(pathname));
        }
    }

    free(pr.fake_path);
    return fd;
}

EXPORT int stat(const char *pathname, struct stat *statbuf) {
    if (!g_hooks_ready)
        return syscall(SYS_newfstatat, AT_FDCWD, pathname, statbuf, 0);
	lazy_init();
	DLSYM_OR_FALLBACK(my_stat, "stat", syscall(SYS_newfstatat, AT_FDCWD, pathname, statbuf, 0));

     const char *event = nullptr;
     int event_number = -1;

	PathRedirect pr = redirect_path(pathname);
	pathname = pr.resolved;

	if (pr.is_from_input) {
	    event = get_event(pathname);
	    event_number = get_event_number(event);
	}

	int ret = my_stat(pathname, statbuf);
    
    if (event && event_number >= 0) {
		statbuf->st_rdev = makedev(1, event_number);
	}

	free(pr.fake_path);
	return ret;
}

EXPORT int fstat(int fd, struct stat *buf) {
    if (!g_hooks_ready)
        return syscall(SYS_newfstatat, fd, "", buf, AT_EMPTY_PATH);
	DLSYM_OR_FALLBACK(my_fstat, "fstat", syscall(SYS_newfstatat, fd, "", buf, AT_EMPTY_PATH));

    int ret = my_fstat(fd, buf);

    {
        std::lock_guard<std::mutex> lock(controller_map_mutex);
        auto controller = controller_map.find(fd);
        if (controller != controller_map.end()) {
            buf->st_rdev = makedev(1, get_event_number(controller->second));
        }
    }

    return ret;
  }

EXPORT int scandir(const char *dirp, struct dirent ***namelist, int(*filter)(const struct dirent *), int(*compar)(const struct dirent **, const struct dirent **)) {
    if (!g_hooks_ready) { errno = ENOSYS; return -1; }
	lazy_init();
	DLSYM_OR_FALLBACK(my_scandir, "scandir", -1);
	
	if (dirp) {
	    if (!strcmp(dirp, "/dev/input")) {
	        dirp = hook_dir;
	    }
    }

	return my_scandir(dirp, namelist, filter, compar);
}

EXPORT int inotify_add_watch(int fd, const char *pathname, uint32_t mask) {
    if (!g_hooks_ready)
        return syscall(SYS_inotify_add_watch, fd, pathname, mask);
	lazy_init();
	DLSYM_OR_FALLBACK(my_inotify_add_watch, "inotify_add_watch", syscall(SYS_inotify_add_watch, fd, pathname, mask));

    PathRedirect pr = redirect_path(pathname);
    pathname = pr.resolved;

    int ret = my_inotify_add_watch(fd, pathname, mask);
    free(pr.fake_path);
    return ret;
}

EXPORT int ioctl(int fd, int op, ...) {
	va_list va;
	void *argp;
	
	va_start(va, op);
	argp = va_arg(va, void *);
	va_end(va);

    const char *event;
    int event_number;
    {
        std::lock_guard<std::mutex> lock(controller_map_mutex);
        auto controller = controller_map.find(fd);
        if (controller == controller_map.end()) {
            return syscall(SYS_ioctl, fd, op, argp);
        }
        event = controller->second;
        event_number = get_event_number(event);
    }

    int dir = IOCTL_DIR(op);
    int type = IOCTL_TYPE(op);
    int nr = IOCTL_NR(op);
    int len = IOCTL_SIZE(op);

    // Handle Linux joystick interface ioctls (type 'j')
    if (type == IOCTL_JS_TYPE) {
        if (nr == 0x13 && dir == IOCTL_READ) {
            // JSIOCGNAME(len) — get joystick name
            Logger::log("Hooking ioctl JSIOCGNAME(len=%d) for event %s\n", len, event);
            char *name;
            asprintf(&name, "Generic HID Gamepad %d", event_number);
            int copy_len = len - 1;
            if (copy_len < 0) copy_len = 0;
            int name_len = strlen(name);
            if (copy_len > name_len) copy_len = name_len;
            memcpy((char *)argp, name, copy_len);
            ((char *)argp)[copy_len] = '\0';
            free(name);
            return 0;
        }
        Logger::log("Unhandled joystick ioctl, nr=%d dir=%d\n", nr, dir);
        return syscall(SYS_ioctl, fd, op, argp);
    }

    // Handle evdev ioctls (type 'E')
    if (type != IOCTL_EV_TYPE) {
        int type2 = (op >> 8 & 0xFF);
        int number2 = (op >> 0 & 0xFF);
        Logger::log("Unhandled ioctl, type=%c(0x%x) nr=%d\n", type2, type2, number2);
        return syscall(SYS_ioctl, fd, op, argp);
    }

    // EVIOCGVERSION: _IOR('E', 0x01, int)
    if (nr == 0x01 && dir == IOCTL_READ) {
        Logger::log("Hooking ioctl EVIOCGVERSION for event %s\n", event);
        int version = 65536;
        memcpy(argp, (void *)&version, sizeof(int));
        return 0;
    }
    // EVIOCGID: _IOR('E', 0x02, struct input_id)
    else if (nr == 0x02 && dir == IOCTL_READ) {
        Logger::log("Hooking ioctl EVIOCGID for event %s\n", event);
        struct input_id id;
        memset(&id, 0, sizeof(id));
        id.bustype = 0x03;
        id.vendor = 0x1234 + event_number;
        id.product = 0x5678 + event_number;
        id.version = 0x0110;
        memcpy(argp, (void *)&id, sizeof(id));
        return 0;
    }
    // EVIOCGNAME(len): _IOC(_IOC_READ, 'E', 0x06, len)
    else if (nr == 0x06 && dir == IOCTL_READ) {
        Logger::log("Hooking ioctl EVIOCGNAME(len=%d) for event %s\n", len, event);
        char *name;
        asprintf(&name, "Generic HID Gamepad %d", event_number);
        int copy_len = len - 1;
        if (copy_len < 0) copy_len = 0;
        int name_len = strlen(name);
        if (copy_len > name_len) copy_len = name_len;
        memcpy((char *)argp, name, copy_len);
        ((char *)argp)[copy_len] = '\0';
        free(name);
        return 0;
    }
    // EVIOCGPROP(len): _IOC(_IOC_READ, 'E', 0x09, len)
    else if (nr == 0x09 && dir == IOCTL_READ) {
        Logger::log("Hooking ioctl EVIOCGPROP for event %s\n", event);
        memset(argp, 0, len);
        return 0;
    }
    // EVIOCGKEY(len): _IOC(_IOC_READ, 'E', 0x18, len)
    else if (nr == 0x18 && dir == IOCTL_READ) {
        Logger::log("Hooking ioctl EVIOCGKEY(len=%d) for event %s\n", len, event);
        char bitmask[(KEY_MAX + 7) / 8];
        memset(bitmask, 0, sizeof(bitmask));
        int copy_len = len;
        if (copy_len > (int)sizeof(bitmask)) copy_len = sizeof(bitmask);
        memcpy(argp, (void *)bitmask, copy_len);
        return 0;
    }
    // EVIOCGBIT(ev_type, len): _IOC(_IOC_READ, 'E', 0x20 + ev_type, len)
    // nr == 0x20 means ev_type=0 (EV_SYN), 0x21=EV_KEY, 0x22=EV_REL, 0x23=EV_ABS, 0x35=EV_FF
    else if (nr == 0x20 && dir == IOCTL_READ) {
        // EVIOCGBIT(0, len) — event type bitmask
        Logger::log("Hooking ioctl EVIOCGBIT(EV_SYN, len=%d) for event %s\n", len, event);
        char bitmask[(EV_MAX + 7) / 8];
        memset(bitmask, 0, sizeof(bitmask));
        bitmask[EV_SYN / 8] |= (1 << (EV_SYN % 8));
        bitmask[EV_KEY / 8] |= (1 << (EV_KEY % 8));
        bitmask[EV_ABS / 8] |= (1 << (EV_ABS % 8));
        int copy_len = len;
        if (copy_len > (int)sizeof(bitmask)) copy_len = sizeof(bitmask);
        memcpy(argp, (void *)bitmask, copy_len);
        return 0;
    }
    else if (nr == 0x21 && dir == IOCTL_READ) {
        // EVIOCGBIT(EV_KEY, len) — key bitmask
        Logger::log("Hooking ioctl EVIOCGBIT(EV_KEY, len=%d) for event %s\n", len, event);
        char bitmask[(KEY_MAX + 7) / 8];
        memset(bitmask, 0, sizeof(bitmask));
        bitmask[BTN_A / 8] |= (1 << (BTN_A % 8));
        bitmask[BTN_B / 8] |= (1 << (BTN_B % 8));
        bitmask[BTN_X / 8] |= (1 << (BTN_X % 8));
        bitmask[BTN_Y / 8] |= (1 << (BTN_Y % 8));
        bitmask[BTN_TL / 8] |= (1 << (BTN_TL % 8));
        bitmask[BTN_TR / 8] |= (1 << (BTN_TR % 8));
        bitmask[BTN_SELECT / 8] |= (1 << (BTN_SELECT % 8));
        bitmask[BTN_START / 8] |= (1 << (BTN_START % 8));
        bitmask[BTN_THUMBL / 8] |= (1 << (BTN_THUMBL % 8));
        bitmask[BTN_THUMBR / 8] |= (1 << (BTN_THUMBR % 8));
        int copy_len = len;
        if (copy_len > (int)sizeof(bitmask)) copy_len = sizeof(bitmask);
        memcpy(argp, (void *)bitmask, copy_len);
        return 0;
    }
    else if (nr == 0x22 && dir == IOCTL_READ) {
        // EVIOCGBIT(EV_REL, len) — relative axes (none for gamepad)
        Logger::log("Hooking ioctl EVIOCGBIT(EV_REL, len=%d) for event %s\n", len, event);
        char bitmask[(REL_MAX + 7) / 8];
        memset(bitmask, 0, sizeof(bitmask));
        int copy_len = len;
        if (copy_len > (int)sizeof(bitmask)) copy_len = sizeof(bitmask);
        memcpy(argp, (void *)bitmask, copy_len);
        return 0;
    }
    else if (nr == 0x23 && dir == IOCTL_READ) {
        // EVIOCGBIT(EV_ABS, len) — absolute axes bitmask
        Logger::log("Hooking ioctl EVIOCGBIT(EV_ABS, len=%d) for event %s\n", len, event);
        char bitmask[(ABS_MAX + 7) / 8];
        memset(bitmask, 0, sizeof(bitmask));
        bitmask[ABS_X / 8] |= (1 << (ABS_X % 8));
        bitmask[ABS_Y / 8] |= (1 << (ABS_Y % 8));
        bitmask[ABS_RX / 8] |= (1 << (ABS_RX % 8));
        bitmask[ABS_RY / 8] |= (1 << (ABS_RY % 8));
        bitmask[ABS_GAS / 8] |= (1 << (ABS_GAS % 8));
        bitmask[ABS_BRAKE / 8] |= (1 << (ABS_BRAKE % 8));
        bitmask[ABS_HAT0X / 8] |= (1 << (ABS_HAT0X % 8));
        bitmask[ABS_HAT0Y / 8] |= (1 << (ABS_HAT0Y % 8));
        int copy_len = len;
        if (copy_len > (int)sizeof(bitmask)) copy_len = sizeof(bitmask);
        memcpy(argp, (void *)bitmask, copy_len);
        return 0;
    }
    else if (nr == 0x35 && dir == IOCTL_READ) {
        // EVIOCGBIT(EV_FF, len) — force feedback bitmask
        Logger::log("Hooking ioctl EVIOCGBIT(EV_FF, len=%d) for event %s\n", len, event);
        char bitmask[(FF_MAX + 7) / 8];
        memset(bitmask, 0, sizeof(bitmask));
        bitmask[FF_RUMBLE / 8] |= (1 << (FF_RUMBLE % 8));
        bitmask[FF_PERIODIC / 8] |= (1 << (FF_PERIODIC % 8));
        int copy_len = len;
        if (copy_len > (int)sizeof(bitmask)) copy_len = sizeof(bitmask);
        memcpy(argp, (void *)bitmask, copy_len);
        return 0;
    }
    // EVIOCSFF: _IOW('E', 0x80, struct ff_effect)
    else if (nr == 0x80 && dir == IOCTL_WRITE) {
        struct ff_effect *effect = (struct ff_effect *)argp;
        if (effect->id == -1) {
            effect->id = next_ff_id++;
        }
        ff_effects[effect->id] = *effect;
        return 0;
    }
    // EVIOCRMFF: _IOW('E', 0x81, int)
    else if (nr == 0x81 && dir == IOCTL_WRITE) {
        int id = (intptr_t)argp;
        ff_effects.erase(id);
        return 0;
    }
    // EVIOCGEFFECTS: _IOR('E', 0x84, int)
    else if (nr == 0x84 && dir == IOCTL_READ) {
        int max_effects = 16;
        memcpy(argp, &max_effects, sizeof(int));
        return 0;
    }
    // EVIOCGABS(abs_code): _IOC(_IOC_READ, 'E', 0x40 + abs_code, sizeof(struct input_absinfo))
    else if (nr >= 0x40 && nr <= 0x7F && dir == IOCTL_READ) {
        int abs_code = nr - 0x40;
        Logger::log("Hooking ioctl EVIOCGABS(0x%02x) for event %s\n", abs_code, event);
        struct input_absinfo abs_info;
        memset(&abs_info, 0, sizeof(abs_info));
        switch (abs_code) {
            case ABS_X:
            case ABS_Y:
            case ABS_RX:
            case ABS_RY:
                abs_info.minimum = -32768;
                abs_info.maximum = 32767;
                break;
            case ABS_Z:
                abs_info.minimum = 0;
                abs_info.maximum = 255;
                break;
            case ABS_GAS:
            case ABS_BRAKE:
                abs_info.minimum = 0;
                abs_info.maximum = 255;
                break;
            case ABS_HAT0X:
            case ABS_HAT0Y:
                abs_info.minimum = -1;
                abs_info.maximum = 1;
                break;
            default:
                // Unknown axis — return zeroed info
                break;
        }
        memcpy(argp, (void *)&abs_info, sizeof(abs_info));
        return 0;
    }
    // EVIOCGRAB: _IOW('E', 0x90, int)
    else if (nr == 0x90 && dir == IOCTL_WRITE) {
        Logger::log("Hooking ioctl EVIOCGRAB for event %s\n", event);
        /* Always pretend this succeeds */
        return 0;
    }
    else {
        Logger::log("Unhandled evdev ioctl, nr=0x%02x dir=%d len=%d\n", nr, dir, len);
        return syscall(SYS_ioctl, fd, op, argp);
    }
}

EXPORT int close(int fd) {
    if (!g_hooks_ready) return syscall(SYS_close, fd);
	DLSYM_OR_FALLBACK(my_close, "close", syscall(SYS_close, fd));

	{
	    std::lock_guard<std::mutex> lock(controller_map_mutex);
	    if (closed_fds.count(fd)) {
	        return 0;
	    }
	    // closed_fds is bounded by process lifetime: the OS recycles fd numbers,
	    // and each fd is inserted at most once. The set cannot grow beyond the
	    // maximum fd value the kernel assigns to this process.
	    closed_fds.insert(fd);
	    auto controller = controller_map.find(fd);
	    if (controller != controller_map.end()) {
	        Logger::log("Removing controller, fd %d event %s\n", controller->first, controller->second);
	        free((void *)controller->second);
		    controller_map.erase(fd);
	    }
	}

	return my_close(fd);
}

EXPORT ssize_t read(int fd, void *buf, size_t count) {
    bool is_controller = false;
    {
        std::lock_guard<std::mutex> lock(controller_map_mutex);
        is_controller = controller_map.find(fd) != controller_map.end();
    }
    
    if (is_controller) {
        ssize_t bytes_read = 0;
        int flags = fcntl(fd, F_GETFL);
        bool isNonBlock = flags & O_NONBLOCK;
        bytes_read = syscall(SYS_read, fd, buf, count);
        int empty_retries = 0;
        setup_signal_handler();
        while(bytes_read == 0) {
            struct stat statbuf;
            if (!my_fstat) *(void **)&my_fstat = dlsym(RTLD_NEXT, "fstat");
            if (my_fstat && my_fstat(fd, &statbuf) == 0 && statbuf.st_nlink == 0) {
                errno = ENODEV;
                return -1;
            }
            if (isNonBlock) {
                break;
            }
            if (++empty_retries > 50) {
                break;
            }
            if (stop_flag) {
            	errno = EINTR;
            	return -1;
            }
            struct pollfd pfd = {fd, POLLIN, 0};
            int pret = poll(&pfd, 1, 100);
            if (pret < 0) {
                if (errno == EINTR) { errno = EINTR; return -1; }
                return -1;
            }
            if (pret == 0) continue;
            bytes_read = syscall(SYS_read, fd, buf, count);
        }
        
    	return bytes_read;
    }
    return syscall(SYS_read, fd, buf, count);
}

static void check_ff_event(const struct input_event *ev, uint16_t slot) {
  if (ev->type == EV_FF) {
    int id = ev->code;
    int value = ev->value;
    if (value > 0) {
      auto it = ff_effects.find(id);
      if (it != ff_effects.end()) {
        uint16_t duration = it->second.replay.length;
        if (it->second.type == FF_RUMBLE) {
          send_vibration(it->second.u.rumble.strong_magnitude,
                         it->second.u.rumble.weak_magnitude, duration, slot);
        } else if (it->second.type == FF_PERIODIC) {
          send_vibration(it->second.u.periodic.magnitude,
                         it->second.u.periodic.magnitude, duration, slot);
        }
      }
    } else {
      send_vibration(0, 0, 0, slot);
    }
  }
}

EXPORT ssize_t write(int fd, const void *buf, size_t count) {
  if (!g_hooks_ready) return syscall(SYS_write, fd, buf, count);
  DLSYM_OR_FALLBACK(my_write, "write", syscall(SYS_write, fd, buf, count));

  const char *event = nullptr;
  {
      std::lock_guard<std::mutex> lock(controller_map_mutex);
      auto controller = controller_map.find(fd);
      if (controller != controller_map.end())
          event = controller->second;
  }
    if (event != nullptr) {
    	if (count % sizeof(struct input_event) == 0) {
    	  size_t num_events = count / sizeof(struct input_event);
    	  const struct input_event *ev = (const struct input_event *)buf;
    	  uint16_t slot = (uint16_t)get_event_number(event);
    	  bool has_ff = false;
    	  for (size_t i = 0; i < num_events; i++) {
    	      check_ff_event(&ev[i], slot);
    	      if (ev[i].type == EV_FF) {
    	          has_ff = true;
    	      }
    	  }
    	  if (has_ff) {
    	      if (num_events == 1) {
    	          // Single FF event: consume without writing
    	          return (ssize_t)count;
    	      }
    	      // Multi-event: filter out FF events by writing only non-FF events
    	      struct input_event stack_filtered[32];
    	      struct input_event* filtered = (num_events <= 32) ? stack_filtered :
    	          (struct input_event*)malloc(num_events * sizeof(struct input_event));
    	      if (!filtered) return (ssize_t)count;
    	      size_t filtered_count = 0;
    	      for (size_t i = 0; i < num_events; i++) {
    	          if (ev[i].type != EV_FF) {
    	              filtered[filtered_count++] = ev[i];
    	          }
    	      }
    	      if (filtered_count == 0) {
    	          if (filtered != stack_filtered) free(filtered);
    	          return (ssize_t)count;
    	      }
    	      ssize_t written = my_write(fd, filtered, filtered_count * sizeof(struct input_event));
    	      if (filtered != stack_filtered) free(filtered);
    	      if (written >= 0)
    	          return (ssize_t)count;
    	      return written;
    	  }
    	}
    }
  return my_write(fd, buf, count);
}

EXPORT ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
  if (!g_hooks_ready) return syscall(SYS_writev, fd, iov, iovcnt);
  DLSYM_OR_FALLBACK(real_writev, "writev", syscall(SYS_writev, fd, iov, iovcnt));
  const char *event = nullptr;
  {
      std::lock_guard<std::mutex> lock(controller_map_mutex);
      auto controller = controller_map.find(fd);
      if (controller != controller_map.end())
          event = controller->second;
  }
  if (event != nullptr) {
    uint16_t slot = (uint16_t)get_event_number(event);
    // Separate FF control events from regular input events.
    // FF events must not be written to the fake evdev file (see write() above).
    struct iovec filtered[iovcnt];
    int filtered_count = 0;
    for (int i = 0; i < iovcnt; i++) {
      if (iov[i].iov_len == sizeof(struct input_event)) {
        const struct input_event *ev = (const struct input_event *)iov[i].iov_base;
        check_ff_event(ev, slot);
        if (ev->type == EV_FF)
          continue;
      }
      filtered[filtered_count++] = iov[i];
    }
    if (filtered_count == 0)
      return 0;
    ssize_t written = real_writev(fd, filtered, filtered_count);
    if (written >= 0) {
        size_t total = 0;
        for (int i = 0; i < iovcnt; i++) {
            total += iov[i].iov_len;
        }
        return total > (size_t)written ? (ssize_t)total : written;
    }
    return written;
  }
  return real_writev(fd, iov, iovcnt);
}
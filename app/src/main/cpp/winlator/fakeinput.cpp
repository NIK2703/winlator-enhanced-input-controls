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

#define EXPORT __attribute__((visibility("default"))) extern "C"

std::unordered_map<int, const char *> controller_map;
static std::mutex controller_map_mutex;
static bool signal_handler_initialized = false;
static bool lazy_init_done = false;
static const char *hook_dir = nullptr;
static bool vibration_enabled = true;
volatile sig_atomic_t stop_flag = 0;

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
    bool isFromInput;
    char *fake_path = nullptr;

    va_start(va, flags);

    hasMode = flags & O_CREAT;
    isFromInput = false;
    
    if (hasMode) {
        mode = va_arg(va, mode_t);
    }
    
    va_end(va);

	if (!my_open)
    	*(void **)&my_open = dlsym(RTLD_NEXT, "open");
	if (!my_open) {
        va_list va2;
        va_start(va2, flags);
        mode_t m = flags & O_CREAT ? va_arg(va2, mode_t) : 0;
        va_end(va2);
        return syscall(SYS_openat, AT_FDCWD, pathname, flags, m);
    }

	if (pathname) {
		if (strstr(pathname, "/dev/input/event")) {
		    fake_path = from_real_to_fake_path(pathname);
		    if (fake_path) {
		        pathname = fake_path;
		        isFromInput = true;
		    }
		}
		else if (!strcmp(pathname, "/dev/input")) {
			pathname = hook_dir;
		}
	}
	    
	if (hasMode)
	    fd = my_open(pathname, flags, mode);
	else
	    fd = my_open(pathname, flags);

	if (isFromInput) {
		Logger::log("Adding controller, fd %d event %s\n", fd, get_event(pathname));
		{
		    std::lock_guard<std::mutex> lock(controller_map_mutex);
		    controller_map[fd] = strdup(get_event(pathname));
		}
    }
	    
	free(fake_path);
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
    bool isFromInput;
    char *fake_path = nullptr;
    
    va_start(va, flags);

    isFromInput = false;
    hasMode = flags & O_CREAT;
    
    if (hasMode) {
        mode = va_arg(va, mode_t);
    }
    
    va_end(va);

    if (!my_openat)
    	*(void **)&my_openat = dlsym(RTLD_NEXT, "openat");
    if (!my_openat) {
        va_list va2;
        va_start(va2, flags);
        mode_t m = flags & O_CREAT ? va_arg(va2, mode_t) : 0;
        va_end(va2);
        return syscall(SYS_openat, dirfd, pathname, flags, m);
    }
    
    if (pathname) {
        if (strstr(pathname, "/dev/input/event")) {
            fake_path = from_real_to_fake_path(pathname);
            if (fake_path) {
                pathname = fake_path;
                isFromInput = true;
            }
        }
        else if (!strcmp(pathname, "/dev/input")) {                                    
            pathname = hook_dir;             
        }
    }
    
    if (hasMode)
        fd = my_openat(dirfd, pathname, flags, mode);
    else
        fd = my_openat(dirfd, pathname, flags);

    if (isFromInput) {
        Logger::log("Adding controller, fd %d event %s\n", fd, get_event(pathname));
        {
            std::lock_guard<std::mutex> lock(controller_map_mutex);
            controller_map[fd] = strdup(get_event(pathname));
        }
    }

    free(fake_path);
    return fd;
}

EXPORT int stat(const char *pathname, struct stat *statbuf) {
    if (!g_hooks_ready)
        return syscall(SYS_newfstatat, AT_FDCWD, pathname, statbuf, 0);
	lazy_init();
	if (!my_stat)
		*(void **)&my_stat = dlsym(RTLD_NEXT, "stat");
	if (!my_stat)
        return syscall(SYS_newfstatat, AT_FDCWD, pathname, statbuf, 0);

     char *fake_path = nullptr;
     const char *event = nullptr;
     int event_number = -1;

	if (pathname) {
		if (strstr(pathname, "/dev/input/event")) {
		    fake_path = from_real_to_fake_path(pathname);
		    if (fake_path) {
		        pathname = fake_path;
		        event = get_event(pathname);
		        event_number = get_event_number(event);
		    }
		}
		else if (!strcmp(pathname, "/dev/input")) {                                    
		    pathname = hook_dir;             
		}
	}

	int ret = my_stat(pathname, statbuf);
    
    if (event && event_number >= 0) {
		statbuf->st_rdev = makedev(1, event_number);
	}

	free(fake_path);
	return ret;
}

EXPORT int fstat(int fd, struct stat *buf) {
    if (!g_hooks_ready)
        return syscall(SYS_newfstatat, fd, "", buf, AT_EMPTY_PATH);
	if (!my_fstat)
    	*(void **)&my_fstat = dlsym(RTLD_NEXT, "fstat");
    if (!my_fstat)
        return syscall(SYS_newfstatat, fd, "", buf, AT_EMPTY_PATH);

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
	if (!my_scandir)
		*(void **)&my_scandir = dlsym(RTLD_NEXT, "scandir");
	if (!my_scandir)
        return -1;
	
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
	if (!my_inotify_add_watch)
		*(void **)&my_inotify_add_watch = dlsym(RTLD_NEXT, "inotify_add_watch");
    if (!my_inotify_add_watch)
        return syscall(SYS_inotify_add_watch, fd, pathname, mask);

    char *fake_path = nullptr;
    if (pathname) {
        if (strstr(pathname, "/dev/input/event")) {
            fake_path = from_real_to_fake_path(pathname);
            if (fake_path) {
                pathname = fake_path;
            }
        }
        else if (!strcmp(pathname, "/dev/input")) {
            pathname = hook_dir;
    	}
    }

    int ret = my_inotify_add_watch(fd, pathname, mask);
    free(fake_path);
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

	int type = (op >> 8 & 0xFF);
	int number = (op >> 0 & 0xFF);

    if (type == 0x45 && number == 0x1) {
        Logger::log("Hooking ioctl EVIOCGVERSION for event %s\n", event);
        int version = 65536;
        memcpy(argp, (void *)&version, sizeof(int));
        return 0;
    }
    else if (type == 0x45 && number == 0x2) {
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
    else if (type == 0x45 && number == 0x6) {
    	Logger::log("Hooking ioctl EVIOCGNAME for event %s\n", event);
    	char *name;
    	
    	asprintf(&name, "Generic HID Gamepad %d", event_number);
    	
    	strcpy((char *)argp, name);
    	free(name);
    	return 0;
    }
    else if (type == 0x45 && number == 0x9) {
        Logger::log("Hooking ioctl EVIOCGPROP for event %s\n", event);
        memset(argp, 0, sizeof(int));
        return 0;
    }
    else if (type == 0x45 && number == 0x18) {
    	Logger::log("Hooking ioctl EVIOCGKEY(len) for event %s\n", event);
    	char bitmask[KEY_MAX / 8] = {0};
        memcpy(argp, (void *)&bitmask, sizeof(bitmask));
        return 0;
    }
    else if (type == 0x45 && number == 0x20) {
    	Logger::log("Hooking ioctl EVIOCGBIT(0, len) for event %s\n", event);
        char bitmask[EV_MAX / 8] = {0};
        bitmask[EV_SYN / 8] |= (1 << (EV_SYN % 8));
        bitmask[EV_KEY / 8] |= (1 << (EV_KEY % 8));
        bitmask[EV_ABS / 8] |= (1 << (EV_ABS % 8));
    	memcpy(argp, (void *)&bitmask, sizeof(bitmask));
    	return 0;	
    }
    else if (type == 0x45 && number == 0x21) {
        Logger::log("Hooking ioctl EVIOCGBIT(EV_KEY, len) for event %s\n", event);
        char bitmask[KEY_MAX / 8] = {0};
        for (int i = 0x130; i <= 0x13e; i++) {
            if (i == 0x130)
                bitmask[BTN_A / 8] |= (1 << (BTN_A % 8));
            else if (i == 0x131)
                bitmask[BTN_B / 8] |= (1 << (BTN_B % 8));
            else if (i == 0x132)
                continue;
            else if (i == 0x133)
                bitmask[BTN_X / 8] |= (1 << (BTN_X % 8));
            else if (i == 0x134)
                bitmask[BTN_Y / 8] |= (1 << (BTN_Y % 8));
            else if (i == 0x135)
                continue;
            else
                bitmask[i / 8] |= (1 << (i % 8));
        }
        memcpy(argp, (void *)&bitmask, sizeof(bitmask));
        return 0;
    }
    else if (type == 0x45 && number == 0x22) {
    	Logger::log("Hooking ioctl EVIOCGBIT(EV_REL, len) for event %s\n", event);
    	char bitmask[REL_MAX / 8] = {0};
    	memcpy(argp, (void *)&bitmask, sizeof(bitmask));
    	return 0;
    }
    else if (type == 0x45 && number == 0x23) {
    	Logger::log("Hooking ioctl EVIOCGBIT(EV_ABS, len) for event %s\n", event);
    	char bitmask[ABS_MAX / 8] = {0};
    	bitmask[ABS_X / 8] |= (1 << (ABS_X % 8));
    	bitmask[ABS_Y / 8] |= (1 << (ABS_Y % 8));
    	bitmask[ABS_RX / 8] |= (1 << (ABS_RX % 8));
    	bitmask[ABS_RY / 8] |= (1 << (ABS_RY % 8));
    	bitmask[ABS_GAS / 8] |= (1 << (ABS_GAS % 8));
    	bitmask[ABS_BRAKE / 8] |= (1 << (ABS_BRAKE % 8));
    	bitmask[ABS_HAT0X / 8] |= (1 << (ABS_HAT0X % 8));
    	bitmask[ABS_HAT0Y / 8] |= (1 << (ABS_HAT0Y % 8));
    	memcpy(argp, (void *)&bitmask, sizeof(bitmask));
    	return 0;
    }
    else if (type == 0x45 && number == 0x35) {
        Logger::log("Hooking ioctl EVIOCGBIT(EV_FF, len) for event %s\n", event);
        char bitmask[FF_MAX / 8] = {0};
        bitmask[FF_RUMBLE / 8] |= (1 << (FF_RUMBLE % 8));
        bitmask[FF_PERIODIC / 8] |= (1 << (FF_PERIODIC % 8));
        memcpy(argp, (void *)&bitmask, sizeof(bitmask));
        return 0;
    }
    else if (type == 0x45 && number == 0x80) {
        struct ff_effect *effect = (struct ff_effect *)argp;
        if (effect->id == -1) {
            effect->id = next_ff_id++;
        }
        ff_effects[effect->id] = *effect;
        return 0;
    }
    else if (type == 0x45 && number == 0x81) {
        int id = (intptr_t)argp;
        ff_effects.erase(id);
        return 0;
    }
    else if (type == 0x45 && number == 0x84) {
        int max_effects = 16;
        memcpy(argp, &max_effects, sizeof(int));
        return 0;
    }
    else if (type == 0x45 && number >= 0x40 && number <= 0x51) {
    	Logger::log("Hooking ioctl EVIOCGABS(ABS) for event %s\n", event);
    	struct input_absinfo abs_info;
    	memset(&abs_info, 0, sizeof(abs_info));
    	if (number >= 0x40 && number <= 0x41) {
    		abs_info.value = 0;
    		abs_info.minimum = -32768;
    		abs_info.maximum = 32767;
    	}
    	else if (number >= 0x43 && number <= 0x44) {
    		abs_info.value = 0;
    		abs_info.minimum = -32768;
    		abs_info.maximum = 32767;
    	}
    	else if (number == 0x42) {
    		abs_info.value = 0;
    		abs_info.minimum = 0;
    		abs_info.maximum = 255;
    		// ABS_Z / ABS_THROTTLE for trigger axes
    	}
    	else if (number >= 0x49 && number <= 0x4A) {
    		abs_info.value = 0;
    		abs_info.minimum = 0;
    		abs_info.maximum = 255;
    	}
    	else if (number >= 0x50 && number <= 0x51) {
    		abs_info.value = 0;
    		abs_info.minimum = -1;
    		abs_info.maximum = 1;
    	}
    	memcpy(argp, (void *)&abs_info, sizeof(abs_info));
    	return 0;
    }
    else if (type == 0x45 && number == 0x90) {
    	Logger::log("Hooking ioctl EVIOCGRAB for event %s\n", event);
    	/* Always pretend this succeeds */
    	return 0;
    }
    else if (type == 0x6A && number == 0x13) {
    	Logger::log("Hooking ioctl JSIOCGNAME(len) for event %s\n", event);
    	char *name;
        asprintf(&name, "Generic HID Gamepad %d", event_number);
    	strcpy((char *)argp, name);
    	free(name);
    	return 0;
    }
    else {
    	Logger::log("Unhandled evdev ioctl, type %d number %d\n", type, number);
    	return syscall(SYS_ioctl, fd, op, argp);
    }
}

static std::set<int> closed_fds;

EXPORT int close(int fd) {
    if (!g_hooks_ready) return syscall(SYS_close, fd);
	if (!my_close)
		*(void **)&my_close = dlsym(RTLD_NEXT, "close");
	if (!my_close)
        return syscall(SYS_close, fd);

	{
	    std::lock_guard<std::mutex> lock(controller_map_mutex);
	    if (closed_fds.count(fd)) {
	        return 0;
	    }
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
  if (!my_write)
    *(void **)&my_write = dlsym(RTLD_NEXT, "write");
  if (!my_write)
    return syscall(SYS_write, fd, buf, count);

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
    	      struct input_event filtered[num_events];
    	      size_t filtered_count = 0;
    	      for (size_t i = 0; i < num_events; i++) {
    	          if (ev[i].type != EV_FF) {
    	              filtered[filtered_count++] = ev[i];
    	          }
    	      }
    	      if (filtered_count == 0)
    	          return (ssize_t)count;
    	      ssize_t written = my_write(fd, filtered, filtered_count * sizeof(struct input_event));
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
  if (!real_writev)
    *(void **)&real_writev = dlsym(RTLD_NEXT, "writev");
  if (!real_writev)
    return syscall(SYS_writev, fd, iov, iovcnt);
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
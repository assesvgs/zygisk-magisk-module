#include <sys/mman.h>
#include <sys/socket.h>
#include <android/dlext.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <lsplt.hpp>

#include "zygisk.hpp"
#include "module.hpp"
#include "jni_hooks.hpp"
#include "ipc_client.hpp"
#include "logging.hpp"

using namespace std;

// ---------------------------------------------------------------------
// RAII helpers (replacing Magisk base.hpp without rust cxx dependency)
// ---------------------------------------------------------------------

struct owned_fd {
    int fd;
    owned_fd() : fd(-1) {}
    owned_fd(int f) : fd(f) {}
    owned_fd(owned_fd &&o) : fd(o.fd) { o.fd = -1; }
    owned_fd &operator=(owned_fd &&o) {
        if (this != &o) { close(fd); fd = o.fd; o.fd = -1; }
        return *this;
    }
    ~owned_fd() { if (fd >= 0) close(fd); }
    operator int() const { return fd; }
    int release() { int f = fd; fd = -1; return f; }
};

struct mutex_guard {
    pthread_mutex_t *mutex;
    explicit mutex_guard(pthread_mutex_t &m) : mutex(&m) {
        pthread_mutex_lock(mutex);
    }
    void unlock() { if (mutex) { pthread_mutex_unlock(mutex); mutex = nullptr; } }
    ~mutex_guard() { if (mutex) pthread_mutex_unlock(mutex); }
};

using sDIR = unique_ptr<DIR, decltype(&closedir)>;
static sDIR make_dir(DIR *dp) { return sDIR(dp, closedir); }
static sDIR xopen_dir(const char *path) { return make_dir(opendir(path)); }
static dirent *xreaddir(DIR *dirp) { return readdir(dirp); }
static int parse_int(const char *s) {
    int v = 0;
    for (; *s >= '0' && *s <= '9'; ++s) v = v * 10 + (*s - '0');
    return v;
}

// ---------------------------------------------------------------------
// IPC helpers (raw syscall wrappers for socket to daemon)
// ---------------------------------------------------------------------

template<typename T>
static void write_any(int fd, T val) {
    if (fd < 0) return;
    write(fd, &val, sizeof(val));
}

static void write_int(int fd, int val) { write_any(fd, val); }

static ssize_t xxread(int fd, void *buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t n = read(fd, static_cast<uint8_t *>(buf) + total, count - total);
        if (n <= 0) {
            if (n == 0) break;
            if (errno == EINTR) continue;
            return -1;
        }
        total += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(total);
}

static void write_string(int fd, const char *str) {
    size_t len = str ? strlen(str) : 0;
    write_int(fd, static_cast<int>(len));
    if (len > 0) write(fd, str, len);
}

template <typename T>
void write_vector(int fd, const std::vector<T> &vec) {
    write_int(fd, static_cast<int>(vec.size()));
    if (!vec.empty()) write(fd, vec.data(), vec.size() * sizeof(T));
}

// ---------------------------------------------------------------------
// Connect to local zygiskd via ipc_client
// ---------------------------------------------------------------------

// Returns raw fd to zygiskd for sending additional data after initial request
static int zygisk_request_fd(int req) {
    int fd = zygisk_connect();
    if (fd < 0) return fd;
    // Send [opcode, pid] header (zygiskd protocol)
    zygisk_request_header_t header;
    header.opcode = req;
    header.pid = getpid();
    write(fd, &header, sizeof(header));
    return fd;
}

// g_ctx and old_fork are defined in hook.cpp
// ------------------------------------------------------------------------
// ZygiskModule
// ------------------------------------------------------------------------

ZygiskModule::ZygiskModule(int id, void *handle, void *entry)
    : id(id), handle(handle), entry{entry}, api{}, mod{nullptr} {
    memset(&api, 0, sizeof(api));
    api.base.impl = this;
    api.base.registerModule = &ZygiskModule::RegisterModuleImpl;
}

bool ZygiskModule::RegisterModuleImpl(ApiTable *api, long *module) {
    if (api == nullptr || module == nullptr)
        return false;

    long api_version = *module;
    if (api_version > ZYGISK_API_VERSION)
        return false;

    api->base.impl->mod = { module };

    if (api_version >= 1) {
        api->v1.hookJniNativeMethods = hookJniNativeMethods;
        api->v1.pltHookRegister = [](auto a, auto b, auto c, auto d) {
            if (g_ctx) g_ctx->plt_hook_register(a, b, c, d);
        };
        api->v1.pltHookExclude = [](auto a, auto b) {
            if (g_ctx) g_ctx->plt_hook_exclude(a, b);
        };
        api->v1.pltHookCommit = []() { return g_ctx && g_ctx->plt_hook_commit(); };
        api->v1.connectCompanion = [](ZygiskModule *m) { return m->connectCompanion(); };
        api->v1.setOption = [](ZygiskModule *m, auto opt) { m->setOption(opt); };
    }
    if (api_version >= 2) {
        api->v2.getModuleDir = [](ZygiskModule *m) { return m->getModuleDir(); };
        api->v2.getFlags = [](auto) { return ZygiskModule::getFlags(); };
    }
    if (api_version >= 4) {
        api->v4.pltHookCommit = lsplt::CommitHook;
        api->v4.pltHookRegister = [](dev_t dev, ino_t inode, const char *symbol, void *fn, void **backup) {
            if (dev == 0 || inode == 0 || symbol == nullptr || fn == nullptr)
                return;
            lsplt::RegisterHook(dev, inode, symbol, fn, backup);
        };
        api->v4.exemptFd = [](int fd) { return g_ctx && g_ctx->exempt_fd(fd); };
    }

    return true;
}

bool ZygiskModule::valid() const {
    if (mod.api_version == nullptr)
        return false;
    switch (*mod.api_version) {
        case 5:
        case 4:
        case 3:
        case 2:
        case 1:
            return mod.v1->impl && mod.v1->preAppSpecialize && mod.v1->postAppSpecialize &&
                   mod.v1->preServerSpecialize && mod.v1->postServerSpecialize;
        default:
            return false;
    }
}

int ZygiskModule::connectCompanion() const {
    if (int fd = zygisk_request_fd(+ZygiskRequest::ConnectCompanion); fd >= 0) {
#ifdef __LP64__
        write_any(fd, true);
#else
        write_any(fd, false);
#endif
        write_int(fd, id);
        return fd;
    }
    return -1;
}

int ZygiskModule::getModuleDir() const {
    if (owned_fd fd = zygisk_request_fd(+ZygiskRequest::GetModuleDir); fd >= 0) {
        write_int(fd, id);
        char cmsgbuf[CMSG_SPACE(sizeof(int))];
        struct iovec iov = {const_cast<char *>(""), 1};
        struct msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = cmsgbuf;
        msg.msg_controllen = sizeof(cmsgbuf);
        if (recvmsg(fd, &msg, 0) < 0) return -1;
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS) return -1;
        int rfd;
        memcpy(&rfd, CMSG_DATA(cmsg), sizeof(rfd));
        return rfd;
    }
    return -1;
}

void ZygiskModule::setOption(zygisk::Option opt) {
    if (g_ctx == nullptr)
        return;
    switch (opt) {
        case zygisk::FORCE_DENYLIST_UNMOUNT:
            g_ctx->flags |= DO_REVERT_UNMOUNT;
            break;
        case zygisk::DLCLOSE_MODULE_LIBRARY:
            unload = true;
            break;
    }
}

uint32_t ZygiskModule::getFlags() {
    return g_ctx ? (g_ctx->info_flags & ~PRIVATE_MASK) : 0;
}

void ZygiskModule::tryUnload() const {
    if (unload) dlclose(handle);
}

// -----------------------------------------------------------------
// Version dispatch macro
// -----------------------------------------------------------------

#define call_app(method)               \
switch (*mod.api_version) {            \
case 1:                                \
case 2: {                              \
    AppSpecializeArgs_v1 a(args);      \
    mod.v1->method(mod.v1->impl, &a);  \
    break;                             \
}                                      \
case 3:                                \
case 4:                                \
case 5:                                \
    mod.v1->method(mod.v1->impl, args);\
    break;                             \
}

void ZygiskModule::preAppSpecialize(AppSpecializeArgs_v5 *args) const {
    call_app(preAppSpecialize)
}

void ZygiskModule::postAppSpecialize(const AppSpecializeArgs_v5 *args) const {
    call_app(postAppSpecialize)
}

void ZygiskModule::preServerSpecialize(ServerSpecializeArgs_v1 *args) const {
    mod.v1->preServerSpecialize(mod.v1->impl, args);
}

void ZygiskModule::postServerSpecialize(const ServerSpecializeArgs_v1 *args) const {
    mod.v1->postServerSpecialize(mod.v1->impl, args);
}

// -----------------------------------------------------------------
// ZygiskContext (constructor/destructor in hook.cpp)
// -----------------------------------------------------------------

void ZygiskContext::plt_hook_register(const char *regex, const char *symbol, void *fn, void **backup) {
    if (regex == nullptr || symbol == nullptr || fn == nullptr)
        return;
    regex_t re;
    if (regcomp(&re, regex, REG_NOSUB) != 0)
        return;
    mutex_guard lock(hook_info_lock);
    register_info.emplace_back(RegisterInfo{re, symbol, fn, backup});
}

void ZygiskContext::plt_hook_exclude(const char *regex, const char *symbol) {
    if (!regex) return;
    regex_t re;
    if (regcomp(&re, regex, REG_NOSUB) != 0)
        return;
    mutex_guard lock(hook_info_lock);
    ignore_info.emplace_back(IgnoreInfo{re, symbol ?: ""});
}

void ZygiskContext::plt_hook_process_regex() {
    if (register_info.empty())
        return;
    for (auto &map : lsplt::MapInfo::Scan()) {
        if (map.offset != 0 || !map.is_private || !(map.perms & PROT_READ)) continue;
        for (auto &reg: register_info) {
            if (regexec(&reg.regex, map.path.data(), 0, nullptr, 0) != 0)
                continue;
            bool ignored = false;
            for (auto &ign: ignore_info) {
                if (regexec(&ign.regex, map.path.data(), 0, nullptr, 0) != 0)
                    continue;
                if (ign.symbol.empty() || ign.symbol == reg.symbol) {
                    ignored = true;
                    break;
                }
            }
            if (!ignored) {
                lsplt::RegisterHook(map.dev, map.inode, reg.symbol, reg.callback, reg.backup);
            }
        }
    }
}

bool ZygiskContext::plt_hook_commit() {
    {
        mutex_guard lock(hook_info_lock);
        plt_hook_process_regex();
        for (auto& reg: register_info) {
            regfree(&reg.regex);
        }
        for (auto& ign: ignore_info) {
            regfree(&ign.regex);
        }
        register_info.clear();
        ignore_info.clear();
    }
    return lsplt::CommitHook();
}

// -----------------------------------------------------------------
// Module loading: connect to zygiskd + scan filesystem for modules
// -----------------------------------------------------------------

int ZygiskContext::get_module_info(int uid, vector<int> &fds) {
    if (int fd = zygisk_request_fd(+ZygiskRequest::GetInfo); fd >= 0) {
        zygisk_response_header_t resp;
        xxread(fd, &resp, sizeof(resp));
        info_flags = resp.flags;

        if ((info_flags & UNMOUNT_MASK) != UNMOUNT_MASK) {
            // Scan /data/adb/modules/*/zygisk/*.so
            auto dir = xopen_dir("/data/adb/modules");
            if (dir) {
                dirent *entry;
                while ((entry = xreaddir(dir.get()))) {
                    if (entry->d_type != DT_DIR) continue;
                    if (entry->d_name[0] == '.') continue;

                    string path = string("/data/adb/modules/") + entry->d_name + "/zygisk";
                    auto zdir = xopen_dir(path.c_str());
                    if (!zdir) continue;

                    dirent *zent;
                    while ((zent = xreaddir(zdir.get()))) {
                        string name = zent->d_name;
                        if (name.size() < 3) continue;
                        if (name.compare(name.size() - 3, 3, ".so") != 0) continue;

                        string full = path + "/" + name;
                        int mfd = open(full.c_str(), O_RDONLY | O_CLOEXEC);
                        if (mfd >= 0) fds.push_back(mfd);
                    }
                }
            }
        }
        return fd;
    }
    return -1;
}

// -----------------------------------------------------------------
// FD sanitization
// -----------------------------------------------------------------

void ZygiskContext::sanitize_fds() {
    if (!is_child()) return;

    if (can_exempt_fd() && !exempted_fds.empty()) {
        auto update = [&](int old_len) -> jintArray {
            jintArray arr = env->NewIntArray(static_cast<int>(old_len + exempted_fds.size()));
            if (arr == nullptr) return nullptr;
            env->SetIntArrayRegion(arr, old_len, static_cast<int>(exempted_fds.size()), exempted_fds.data());
            for (int fd : exempted_fds) {
                if (fd >= 0 && fd < (int)allowed_fds.size()) allowed_fds[fd] = true;
            }
            *args.app->fds_to_ignore = arr;
            return arr;
        };

        if (jintArray fdsToIgnore = *args.app->fds_to_ignore) {
            int *arr = env->GetIntArrayElements(fdsToIgnore, nullptr);
            int len = env->GetArrayLength(fdsToIgnore);
            for (int i = 0; i < len; ++i) {
                int fd = arr[i];
                if (fd >= 0 && fd < (int)allowed_fds.size()) allowed_fds[fd] = true;
            }
            if (jintArray newFdList = update(len)) {
                env->SetIntArrayRegion(newFdList, 0, len, arr);
            }
            env->ReleaseIntArrayElements(fdsToIgnore, arr, JNI_ABORT);
        } else {
            update(0);
        }
    }

    auto dir = xopen_dir("/proc/self/fd");
    int dfd = dirfd(dir.get());
    for (dirent *entry; (entry = xreaddir(dir.get()));) {
        int fd = parse_int(entry->d_name);
        if ((fd < 0 || fd >= (int)allowed_fds.size() || !allowed_fds[fd]) && fd != dfd) {
            close(fd);
        }
    }
}

bool ZygiskContext::exempt_fd(int fd) {
    if ((flags & POST_SPECIALIZE) || (flags & SKIP_CLOSE_LOG_PIPE)) return true;
    if (!can_exempt_fd()) return false;
    exempted_fds.push_back(fd);
    return true;
}

bool ZygiskContext::can_exempt_fd() const {
    return (flags & APP_FORK_AND_SPECIALIZE) && args.app->fds_to_ignore;
}

// -----------------------------------------------------------------
// Fork lifecycle
// -----------------------------------------------------------------

static int sigmask(int how, int signum) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, signum);
    return sigprocmask(how, &set, nullptr);
}

void ZygiskContext::fork_pre() {
    sigmask(SIG_BLOCK, SIGCHLD);
    pid = old_fork();

    if (!is_child()) return;

    auto dir = xopen_dir("/proc/self/fd");
    for (dirent *entry; (entry = xreaddir(dir.get()));) {
        int fd = parse_int(entry->d_name);
        if (fd < 0 || fd >= (int)allowed_fds.size()) {
            close(fd);
            continue;
        }
        allowed_fds[fd] = true;
    }
    allowed_fds[dirfd(dir.get())] = false;
}

void ZygiskContext::fork_post() {
    sigmask(SIG_UNBLOCK, SIGCHLD);
}

void ZygiskContext::run_modules_pre(vector<int> &fds) {
    for (int i = 0; i < (int)fds.size(); ++i) {
        owned_fd fd = fds[i];
        struct stat s{};
        if (fstat(fd, &s) != 0 || !S_ISREG(s.st_mode)) {
            fds[i] = -1;
            continue;
        }
        // fd remains owned by caller (owned_fd). android_dlopen_ext with
        // ANDROID_DLEXT_USE_LIBRARY_FD does NOT take ownership of the fd.
        // The fd will be closed when owned_fd goes out of scope.
        android_dlextinfo info {
            .flags = ANDROID_DLEXT_USE_LIBRARY_FD,
            .library_fd = fd,
        };
        if (void *h = android_dlopen_ext("/jit-cache", RTLD_LAZY, &info)) {
            if (void *e = dlsym(h, "zygisk_module_entry")) {
                modules.emplace_back(i, h, e);
            }
        } else if (flags & SERVER_FORK_AND_SPECIALIZE) {
            ZLOGW("Failed to dlopen zygisk module: %s\n", dlerror());
            fds[i] = -1;
        }
    }

    for (auto it = modules.begin(); it != modules.end();) {
        it->onLoad(env);
        if (it->valid()) ++it;
        else it = modules.erase(it);
    }

    for (auto &m : modules) {
        if (flags & APP_SPECIALIZE) m.preAppSpecialize(args.app);
        else if (flags & SERVER_FORK_AND_SPECIALIZE) m.preServerSpecialize(args.server);
    }
}

void ZygiskContext::run_modules_post() {
    flags |= POST_SPECIALIZE;
    for (const auto &m : modules) {
        if (flags & APP_SPECIALIZE) m.postAppSpecialize(args.app);
        else if (flags & SERVER_FORK_AND_SPECIALIZE) m.postServerSpecialize(args.server);
        m.tryUnload();
    }
}

// -----------------------------------------------------------------
// Specialization lifecycle
// -----------------------------------------------------------------

void ZygiskContext::app_specialize_pre() {
    flags |= APP_SPECIALIZE;

    vector<int> module_fds;
    owned_fd fd = get_module_info(args.app->uid, module_fds);
    if ((info_flags & UNMOUNT_MASK) == UNMOUNT_MASK) {
        ZLOGI("[%s] is on the denylist\n", process);
        flags |= DO_REVERT_UNMOUNT;
    } else if (fd >= 0) {
        run_modules_pre(module_fds);
    }
}

void ZygiskContext::app_specialize_post() {
    run_modules_post();
    if (info_flags & +ZygiskStateFlags::ProcessIsMagiskApp) {
        setenv("ZYGISK_ENABLED", "1", 1);
    }
    env->ReleaseStringUTFChars(args.app->nice_name, process);
}

void ZygiskContext::server_specialize_pre() {
    vector<int> module_fds;
    if (owned_fd fd = get_module_info(1000, module_fds); fd >= 0) {
        if (!module_fds.empty()) {
            run_modules_pre(module_fds);
        }
    }
}

void ZygiskContext::server_specialize_post() {
    run_modules_post();
}

// -----------------------------------------------------------------
// JNI hook entry points (called from jni_hooks.hpp)
// -----------------------------------------------------------------

void ZygiskContext::nativeSpecializeAppProcess_pre() {
    process = env->GetStringUTFChars(args.app->nice_name, nullptr);
    ZLOGV("pre  specialize [%s]\n", process);
    flags |= SKIP_CLOSE_LOG_PIPE;
    app_specialize_pre();
}

void ZygiskContext::nativeSpecializeAppProcess_post() {
    ZLOGV("post specialize [%s]\n", process);
    app_specialize_post();
}

void ZygiskContext::nativeForkSystemServer_pre() {
    ZLOGV("pre  forkSystemServer\n");
    flags |= SERVER_FORK_AND_SPECIALIZE;
    process = "system_server";

    fork_pre();
    if (is_child()) {
        server_specialize_pre();
    }
    sanitize_fds();
}

void ZygiskContext::nativeForkSystemServer_post() {
    if (is_child()) {
        ZLOGV("post forkSystemServer\n");
        server_specialize_post();
    }
    fork_post();
}

void ZygiskContext::nativeForkAndSpecialize_pre() {
    process = env->GetStringUTFChars(args.app->nice_name, nullptr);
    ZLOGV("pre  forkAndSpecialize [%s]\n", process);
    flags |= APP_FORK_AND_SPECIALIZE;

    fork_pre();
    if (is_child()) {
        app_specialize_pre();
    }
    sanitize_fds();
}

void ZygiskContext::nativeForkAndSpecialize_post() {
    if (is_child()) {
        ZLOGV("post forkAndSpecialize [%s]\n", process);
        app_specialize_post();
    }
    fork_post();
}

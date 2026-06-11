#include <jni.h>
#include <stddef.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/eventfd.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <android/log.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_EVENTS 10
#define MAX_FDS 32
#define BACKLOG 128

static jmethodID s_handle_new_method = NULL;
static jmethodID s_handle_existing_method = NULL;
static jmethodID s_add_ancillary_fd_method = NULL;

static void ensure_jni_methods(JNIEnv* env) {
    if (s_handle_new_method) return;
    jclass epoll_cls = (*env)->FindClass(env, "com/winlator/cmod/xconnector/XConnectorEpoll");
    if (!epoll_cls) return;
    s_handle_new_method = (*env)->GetMethodID(env, epoll_cls, "handleNewConnection", "(I)V");
    if (!s_handle_new_method) return;
    s_handle_existing_method = (*env)->GetMethodID(env, epoll_cls, "handleExistingConnection", "(I)V");
    if (!s_handle_existing_method) return;
    jclass client_cls = (*env)->FindClass(env, "com/winlator/cmod/xconnector/ClientSocket");
    if (!client_cls) return;
    s_add_ancillary_fd_method = (*env)->GetMethodID(env, client_cls, "addAncillaryFd", "(I)V");
    if (!s_add_ancillary_fd_method) return;
}

JNIEXPORT jint JNICALL
Java_com_winlator_cmod_xconnector_XConnectorEpoll_createAFUnixSocket(JNIEnv *env, jobject obj,
                                                                jstring path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sun_family = AF_LOCAL;

    const char *pathPtr = (*env)->GetStringUTFChars(env, path, 0);

    int addrLength = offsetof(struct sockaddr_un, sun_path) + strlen(pathPtr);
    strncpy(serverAddr.sun_path, pathPtr, sizeof(serverAddr.sun_path) - 1);

    (*env)->ReleaseStringUTFChars(env, path, pathPtr);

    unlink(serverAddr.sun_path);
    if (bind(fd, (struct sockaddr*) &serverAddr, addrLength) < 0) { close(fd); return -1; }
    if (listen(fd, BACKLOG) < 0) { close(fd); return -1; }

    return fd;
}

JNIEXPORT jint JNICALL
Java_com_winlator_cmod_xconnector_XConnectorEpoll_createEpollFd(JNIEnv *env, jobject obj) {
    return epoll_create(MAX_EVENTS);
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_xconnector_XConnectorEpoll_closeFd(JNIEnv *env, jobject obj, jint fd) {
    close(fd);
}

JNIEXPORT jboolean JNICALL
Java_com_winlator_cmod_xconnector_XConnectorEpoll_doEpollIndefinitely(JNIEnv *env, jobject obj,
                                                                 jint epollFd, jint serverFd,
                                                                 jboolean addClientToEpoll) {
    ensure_jni_methods(env);

    struct epoll_event events[MAX_EVENTS];
    int numFds = epoll_wait(epollFd, events, MAX_EVENTS, -1);
    for (int i = 0; i < numFds; i++) {
        if (events[i].data.fd == serverFd) {
            int clientFd = accept(serverFd, NULL, NULL);
            if (clientFd >= 0) {
                if (addClientToEpoll) {
                    struct epoll_event event;
                    event.data.fd = clientFd;
                    event.events = EPOLLIN;

                    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &event) >= 0) {
                        (*env)->CallVoidMethod(env, obj, s_handle_new_method, clientFd);
                    }
                }
                else (*env)->CallVoidMethod(env, obj, s_handle_new_method, clientFd);
            }
            continue;
        }
        if (events[i].events & (EPOLLHUP | EPOLLERR)) {
            close(events[i].data.fd);
            continue;
        }
        if (events[i].events & EPOLLIN) {
            (*env)->CallVoidMethod(env, obj, s_handle_existing_method, events[i].data.fd);
        }
    }

    return numFds >= 0;
}

JNIEXPORT jboolean JNICALL
Java_com_winlator_cmod_xconnector_XConnectorEpoll_addFdToEpoll(JNIEnv *env, jobject obj,
                                                          jint epollFd,
                                                          jint fd) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event) < 0) return JNI_FALSE;
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_xconnector_XConnectorEpoll_removeFdFromEpoll(JNIEnv *env, jobject obj,
                                                               jint epollFd, jint fd) {
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, NULL);
}

JNIEXPORT jint JNICALL
Java_com_winlator_cmod_xconnector_ClientSocket_read(JNIEnv *env, jobject obj, jint fd, jobject data,
                                                jint offset, jint length) {
    char *dataAddr = (*env)->GetDirectBufferAddress(env, data);
    if (dataAddr == NULL) return -1;
    jlong capacity = (*env)->GetDirectBufferCapacity(env, data);
    if (offset < 0 || length < 0 || offset > capacity || length > capacity - offset) return -1;
    return read(fd, dataAddr + offset, length);
}

JNIEXPORT jint JNICALL
Java_com_winlator_cmod_xconnector_ClientSocket_setNonBlocking(JNIEnv *env, jclass clazz, jint fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

JNIEXPORT jint JNICALL
Java_com_winlator_cmod_xconnector_ClientSocket_write(JNIEnv *env, jclass clazz, jint fd, jobject data,
                                                 jint offset, jint length) {
    char *dataAddr = (*env)->GetDirectBufferAddress(env, data);
    if (dataAddr == NULL) return -1;
    jlong capacity = (*env)->GetDirectBufferCapacity(env, data);
    if (offset < 0 || length < 0 || offset > capacity || length > capacity - offset) return -1;
    ssize_t result = write(fd, dataAddr + offset, length);
    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return (jint)result;
}

JNIEXPORT jint JNICALL
Java_com_winlator_cmod_xconnector_XConnectorEpoll_createEventFd(JNIEnv *env, jobject obj) {
    return eventfd(0, EFD_NONBLOCK);
}

JNIEXPORT jint JNICALL
Java_com_winlator_cmod_xconnector_ClientSocket_recvAncillaryMsg(JNIEnv *env, jobject obj, jint clientFd, jobject data,
                                                            jint offset, jint length) {
    char *dataAddr = (*env)->GetDirectBufferAddress(env, data);
    if (dataAddr == NULL) return -1;
    jlong capacity = (*env)->GetDirectBufferCapacity(env, data);
    if (offset < 0 || length < 0 || offset > capacity || length > capacity - offset) return -1;

    struct iovec iovmsg = {.iov_base = dataAddr + offset, .iov_len = length};
    struct {
        struct cmsghdr align;
        int fds[MAX_FDS];
    } ctrlmsg;

    struct msghdr msg = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iovmsg,
        .msg_iovlen = 1,
        .msg_control = &ctrlmsg,
        .msg_controllen = CMSG_LEN(MAX_FDS * sizeof(int))
    };

    int size = recvmsg(clientFd, &msg, 0);

    if (size >= 0) {
        ensure_jni_methods(env);
        struct cmsghdr *cmsg;
        for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
                if (cmsg->cmsg_len < CMSG_LEN(0)) continue;
                size_t data_len = cmsg->cmsg_len - CMSG_LEN(0);
                int numFds = (int)(data_len / sizeof(int));
                if (numFds > 0) {
                    for (int i = 0; i < numFds; i++) {
                        int ancillaryFd = ((int*)CMSG_DATA(cmsg))[i];
                        (*env)->CallVoidMethod(env, obj, s_add_ancillary_fd_method, ancillaryFd);
                    }
                }
            }
        }
    }
    return size;
}

JNIEXPORT jint JNICALL
Java_com_winlator_cmod_xconnector_ClientSocket_sendAncillaryMsg(JNIEnv *env, jobject obj, jint clientFd,
                                                           jobject data, jint length, jint ancillaryFd) {
    char *dataAddr = (*env)->GetDirectBufferAddress(env, data);
    if (dataAddr == NULL) return -1;

    struct iovec iovmsg = {.iov_base = dataAddr, .iov_len = length};
    struct {
        struct cmsghdr align;
        int fds[1];
    } ctrlmsg;

    struct msghdr msg = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iovmsg,
        .msg_iovlen = 1,
        .msg_flags = 0,
        .msg_control = &ctrlmsg,
        .msg_controllen = CMSG_LEN(sizeof(int))
    };

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = msg.msg_controllen;
    ((int*)CMSG_DATA(cmsg))[0] = ancillaryFd;

    return sendmsg(clientFd, &msg, MSG_NOSIGNAL);
}

JNIEXPORT jboolean JNICALL
Java_com_winlator_cmod_xconnector_XConnectorEpoll_waitForSocketRead(JNIEnv *env, jobject obj, jint clientFd, jint shutdownFd) {
    struct pollfd pfds[2];
    pfds[0].fd = clientFd;
    pfds[0].events = POLLIN;

    pfds[1].fd = shutdownFd;
    pfds[1].events = POLLIN;

    int res = poll(pfds, 2, -1);
    if (res < 0 || (pfds[1].revents & POLLIN)) return JNI_FALSE;

    if (pfds[0].revents & POLLIN) {
        ensure_jni_methods(env);
        (*env)->CallVoidMethod(env, obj, s_handle_existing_method, clientFd);
    }
    return JNI_TRUE;
}

JNIEXPORT jintArray JNICALL
Java_com_winlator_cmod_xconnector_XConnectorEpoll_pollEpollEvents(JNIEnv *env, jobject obj,
                                                             jint epollFd, jint maxEvents) {
    struct epoll_event events[maxEvents];
    int numFds = epoll_wait(epollFd, events, maxEvents, -1);

    if (numFds < 0) return NULL;

    jintArray result = (*env)->NewIntArray(env, numFds);
    if (result == NULL) return NULL;

    jint *r = (*env)->GetIntArrayElements(env, result, 0);

    for (int i = 0; i < numFds; i++) {
        r[i] = events[i].data.fd;
    }

    (*env)->ReleaseIntArrayElements(env, result, r, 0);
    return result;
}

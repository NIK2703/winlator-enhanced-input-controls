#include <vulkan/vulkan.h>

#include <jni.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <android/api-level.h>
#include "../adrenotools/include/adrenotools/driver.h"

#define API_LEVEL_ANDROID_T 33

#define vlog_debug(...) do {} while(0)

static VkInstance instance;
static VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
static PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties;
static PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensionProperties;
static PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices;
static PFN_vkDestroyInstance destroyInstance;

static void *vulkan_handle = NULL;

static void teardown_vulkan(void);

static char *get_native_library_dir(JNIEnv *env, jobject context) {
    char *native_libdir = NULL;

    if (context != NULL) {
        jclass class_ = (*env)->FindClass(env, "com/winlator/cmod/core/AppUtils");
        if (!class_) return NULL;
        jmethodID getNativeLibraryDir = (*env)->GetStaticMethodID(env, class_, "getNativeLibDir",
                                                               "(Landroid/content/Context;)Ljava/lang/String;");
        if (!getNativeLibraryDir) { (*env)->DeleteLocalRef(env, class_); return NULL; }
        jstring nativeLibDir = (jstring)(*env)->CallStaticObjectMethod(env, class_,
                                                                     getNativeLibraryDir,
                                                                     context);
        if (nativeLibDir) {
            const char *tmp = (*env)->GetStringUTFChars(env, nativeLibDir, NULL);
            native_libdir = strdup(tmp);
            (*env)->ReleaseStringUTFChars(env, nativeLibDir, tmp);
        }
        (*env)->DeleteLocalRef(env, nativeLibDir);
        (*env)->DeleteLocalRef(env, class_);
    }

    return native_libdir;
}

static char *get_driver_path(JNIEnv *env, jobject context, const char *driver_name) {
    char *driver_path = NULL;
    char *absolute_path;

    jclass contextWrapperClass = (*env)->FindClass(env, "android/content/ContextWrapper");
    if (!contextWrapperClass) return NULL;
    jmethodID  getFilesDir = (*env)->GetMethodID(env, contextWrapperClass, "getFilesDir", "()Ljava/io/File;");
    if (!getFilesDir) { (*env)->DeleteLocalRef(env, contextWrapperClass); return NULL; }
    jobject  filesDirObj = (*env)->CallObjectMethod(env, context, getFilesDir);
    if (!filesDirObj) { (*env)->DeleteLocalRef(env, contextWrapperClass); return NULL; }
    jclass fileClass = (*env)->GetObjectClass(env, filesDirObj);
    if (!fileClass) { (*env)->DeleteLocalRef(env, filesDirObj); (*env)->DeleteLocalRef(env, contextWrapperClass); return NULL; }
    jmethodID getAbsolutePath = (*env)->GetMethodID(env, fileClass, "getAbsolutePath", "()Ljava/lang/String;");
    if (!getAbsolutePath) { (*env)->DeleteLocalRef(env, fileClass); (*env)->DeleteLocalRef(env, filesDirObj); (*env)->DeleteLocalRef(env, contextWrapperClass); return NULL; }
    jstring absolutePath = (jstring)(*env)->CallObjectMethod(env,filesDirObj,
                                                             getAbsolutePath);

    if (absolutePath) {
        absolute_path = (char *)(*env)->GetStringUTFChars(env,absolutePath, NULL);
        asprintf(&driver_path, "%s/contents/adrenotools/%s/", absolute_path, driver_name);
        (*env)->ReleaseStringUTFChars(env,absolutePath, absolute_path);
    }

    (*env)->DeleteLocalRef(env, absolutePath);
    (*env)->DeleteLocalRef(env, fileClass);
    (*env)->DeleteLocalRef(env, filesDirObj);
    (*env)->DeleteLocalRef(env, contextWrapperClass);

    return driver_path;
}

static char *get_library_name(JNIEnv *env, jobject context, const char *driver_name) {
    char *library_name = NULL;

    jclass adrenotoolsManager = (*env)->FindClass(env, "com/winlator/cmod/contents/AdrenotoolsManager");
    if (!adrenotoolsManager) return NULL;
    jmethodID constructor = (*env)->GetMethodID(env, adrenotoolsManager, "<init>", "(Landroid/content/Context;)V");
    if (!constructor) { (*env)->DeleteLocalRef(env, adrenotoolsManager); return NULL; }
    jobject  adrenotoolsManagerObj = (*env)->NewObject(env, adrenotoolsManager, constructor, context);
    if (!adrenotoolsManagerObj) { (*env)->DeleteLocalRef(env, adrenotoolsManager); return NULL; }
    jmethodID getLibraryName = (*env)->GetMethodID(env, adrenotoolsManager, "getLibraryName","(Ljava/lang/String;)Ljava/lang/String;");
    if (!getLibraryName) { (*env)->DeleteLocalRef(env, adrenotoolsManagerObj); (*env)->DeleteLocalRef(env, adrenotoolsManager); return NULL; }
    jstring driverName = (*env)->NewStringUTF(env, driver_name);
    jstring libraryName = (jstring)(*env)->CallObjectMethod(env, adrenotoolsManagerObj,getLibraryName, driverName);

    if (libraryName) {
        const char *tmp = (*env)->GetStringUTFChars(env, libraryName, NULL);
        library_name = strdup(tmp);
        (*env)->ReleaseStringUTFChars(env, libraryName, tmp);
    }

    (*env)->DeleteLocalRef(env, driverName);
    (*env)->DeleteLocalRef(env, libraryName);
    (*env)->DeleteLocalRef(env, adrenotoolsManagerObj);
    (*env)->DeleteLocalRef(env, adrenotoolsManager);

    return library_name;
}

static void init_original_vulkan() {
    vulkan_handle = dlopen("/system/lib64/libvulkan.so", RTLD_LOCAL | RTLD_NOW);
}

static void init_vulkan(JNIEnv  *env, jobject context, const char *driver_name) {
    char *tmpdir = NULL;
    char *library_name = NULL;
    char *native_library_dir = NULL;

    char *driver_path = get_driver_path(env, context, driver_name);

    if (driver_path && (access(driver_path, F_OK) == 0)) {
        library_name = get_library_name(env, context, driver_name);
        native_library_dir = get_native_library_dir(env, context);
        asprintf(&tmpdir, "%s%s", driver_path, "temp");
        mkdir(tmpdir, S_IRWXU | S_IRWXG);
    }

    vulkan_handle = adrenotools_open_libvulkan(RTLD_LOCAL | RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, tmpdir, native_library_dir, driver_path, library_name, NULL, NULL);

    free(tmpdir);
    free(driver_path);
    free(native_library_dir);
    free(library_name);
}

static VkResult configure_app_info(JNIEnv *env, jstring driverName, VkApplicationInfo *appInfo) {
    VkResult result;
    PFN_vkEnumerateInstanceVersion enumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)dlsym(vulkan_handle, "vkEnumerateInstanceVersion");
    if (!enumerateInstanceVersion)
        return VK_ERROR_INITIALIZATION_FAILED;

    int apiLevel = android_get_device_api_level();

    appInfo->sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo->pApplicationName = "Winlator";
    appInfo->applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo->pEngineName = "Winlator";
    appInfo->engineVersion = VK_MAKE_VERSION(1, 0, 0);
    if (apiLevel > API_LEVEL_ANDROID_T)
        appInfo->apiVersion = VK_API_VERSION_1_0;
    else
        enumerateInstanceVersion(&appInfo->apiVersion);

    return VK_SUCCESS;
}

static VkResult resolve_instance_procs(VkInstance inst) {
    PFN_vkGetInstanceProcAddr gip = (PFN_vkGetInstanceProcAddr)dlsym(vulkan_handle, "vkGetInstanceProcAddr");
    if (!gip)
        return VK_ERROR_INITIALIZATION_FAILED;

    getPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)gip(inst, "vkGetPhysicalDeviceProperties");
    destroyInstance = (PFN_vkDestroyInstance)gip(inst, "vkDestroyInstance");
    enumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)gip(inst, "vkEnumerateDeviceExtensionProperties");
    enumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)gip(inst, "vkEnumeratePhysicalDevices");

    if (!getPhysicalDeviceProperties || !destroyInstance || !enumerateDeviceExtensionProperties || !enumeratePhysicalDevices)
        return VK_ERROR_INITIALIZATION_FAILED;

    return VK_SUCCESS;
}

static VkResult create_instance(jstring driverName, JNIEnv *env, jobject context) {
    VkInstanceCreateInfo create_info = {};
    char *driver_name = NULL;

    if (driverName != NULL)
        driver_name = (char *)(*env)->GetStringUTFChars(env, driverName, NULL);

    if (driver_name && strcmp(driver_name, "System"))
        init_vulkan(env, context, driver_name);
    else
        init_original_vulkan();

    if (driver_name)
        (*env)->ReleaseStringUTFChars(env, driverName, driver_name);

    if (!vulkan_handle)
        return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkCreateInstance createInstance = (PFN_vkCreateInstance)dlsym(vulkan_handle, "vkCreateInstance");
    if (!createInstance)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkApplicationInfo app_info = {};
    VkResult result = configure_app_info(env, driverName, &app_info);
    if (result != VK_SUCCESS)
        return result;

    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pNext = NULL;
    create_info.flags = 0;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledLayerCount = 0;
    create_info.enabledExtensionCount = 0;

    result = createInstance(&create_info, NULL, &instance);

    if (result != VK_SUCCESS)
        return result;

    return resolve_instance_procs(instance);
}

static VkResult enumerate_physical_devices() {
    VkResult result;
    uint32_t deviceCount;

    result = enumeratePhysicalDevices(instance, &deviceCount, NULL);

    if (result != VK_SUCCESS)
        return result;

    if (deviceCount < 1)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkPhysicalDevice *pdevices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
    if (!pdevices)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    result = enumeratePhysicalDevices(instance, &deviceCount, pdevices);

    if (result != VK_SUCCESS) {
        free(pdevices);
        return result;
    }

    physicalDevice = pdevices[0];

    free(pdevices);

    if (physicalDevice == VK_NULL_HANDLE)
        return VK_ERROR_INITIALIZATION_FAILED;

    return VK_SUCCESS;
}

static VkResult setup_vulkan(JNIEnv *env, jstring driverName, jobject context) {
    VkResult result = create_instance(driverName, env, context);
    if (result != VK_SUCCESS) {
        return result;
    }

    result = enumerate_physical_devices();
    if (result != VK_SUCCESS) {
        teardown_vulkan();
    }
    return result;
}

static void teardown_vulkan(void) {
    destroyInstance(instance, NULL);
    if (vulkan_handle)
        dlclose(vulkan_handle);
    vulkan_handle = NULL;
}

static bool setup_and_get_props(JNIEnv *env, jclass obj, jstring driverName, jobject context, VkPhysicalDeviceProperties *props) {
    if (setup_vulkan(env, driverName, context) != VK_SUCCESS) return false;
    getPhysicalDeviceProperties(physicalDevice, props);
    return true;
}

JNIEXPORT jstring JNICALL
Java_com_winlator_cmod_core_GPUInformation_getVulkanVersion(JNIEnv *env, jclass obj, jstring driverName, jobject context) {
    VkPhysicalDeviceProperties props = {};

    if (!setup_and_get_props(env, obj, driverName, context, &props))
        return (*env)->NewStringUTF(env, "Unknown");

    uint32_t api_version_major = VK_VERSION_MAJOR(props.apiVersion);
    uint32_t api_version_minor = VK_VERSION_MINOR(props.apiVersion);
    uint32_t api_version_patch = VK_VERSION_PATCH(props.apiVersion);
    char *driverVersion;
    asprintf(&driverVersion, "%d.%d.%d", api_version_major, api_version_minor, api_version_patch);

    teardown_vulkan();

    jstring result = (*env)->NewStringUTF(env, driverVersion);
    free(driverVersion);
    return result;
}

JNIEXPORT jint JNICALL
Java_com_winlator_cmod_core_GPUInformation_getVendorID(JNIEnv *env, jclass obj, jstring driverName, jobject context) {
    VkPhysicalDeviceProperties props = {};

    if (!setup_and_get_props(env, obj, driverName, context, &props))
        return 0;

    uint32_t vendorID = props.vendorID;

    teardown_vulkan();

    return vendorID;
}

JNIEXPORT jstring JNICALL
Java_com_winlator_cmod_core_GPUInformation_getRenderer(JNIEnv *env, jclass obj, jstring driverName, jobject context) {
    VkPhysicalDeviceProperties props = {};

    if (!setup_and_get_props(env, obj, driverName, context, &props))
        return (*env)->NewStringUTF(env, "Unknown");

    char *renderer = strdup(props.deviceName);

    teardown_vulkan();

    jstring result = (*env)->NewStringUTF(env, renderer);
    free(renderer);
    return result;
}

JNIEXPORT jobjectArray JNICALL
Java_com_winlator_cmod_core_GPUInformation_enumerateExtensions(JNIEnv *env, jclass obj, jstring driverName, jobject context) {
    VkResult result;
    uint32_t extensionCount;
    jclass stringClass = (*env)->FindClass(env, "java/lang/String");
    if (!stringClass) return NULL;
    jobjectArray extensions = NULL;

    if (setup_vulkan(env, driverName, context) != VK_SUCCESS)
        goto cleanup;

    result = enumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, NULL);

    if (result != VK_SUCCESS || extensionCount < 1) {
        teardown_vulkan();
        goto cleanup;
    }

    VkExtensionProperties *extensionProperties = malloc(sizeof(VkExtensionProperties) * extensionCount);
    if (!extensionProperties) {
        teardown_vulkan();
        goto cleanup;
    }

    result = enumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, extensionProperties);

    if (result != VK_SUCCESS) {
        free(extensionProperties);
        teardown_vulkan();
        goto cleanup;
    }

    extensions = (jobjectArray) (*env)->NewObjectArray(env, extensionCount, stringClass, NULL);
    for (int i = 0; i < extensionCount; i++) {
        (*env)->SetObjectArrayElement(env, extensions, i,
                                      (*env)->NewStringUTF(env, extensionProperties[i].extensionName));
    }

    free(extensionProperties);
    teardown_vulkan();

cleanup:
    (*env)->DeleteLocalRef(env, stringClass);
    return extensions;
}

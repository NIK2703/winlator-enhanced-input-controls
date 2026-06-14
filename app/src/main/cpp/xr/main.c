#include "engine.h"
#include "input.h"
#include "math.h"
#include "renderer.h"

#include <string.h>

struct XrEngine xr_module_engine;
struct XrInput xr_module_input;
struct XrRenderer xr_module_renderer;
bool xr_initialized = false;

typedef struct {
    float head[7];
    float left_hand[7];
    float right_hand[7];
    float left_angular[3];
    float right_angular[3];
    bool buttons[19];
    float left_stick[2];
    float right_stick[2];
    float left_trigger;
    float right_trigger;
} XrInputData;

#define XR_INPUT_DATA_SIZE sizeof(XrInputData)

#if defined(_DEBUG)
#include <GLES2/gl2.h>
void GLCheckErrors(const char* file, int line) {
	for (int i = 0; i < 10; i++) {
		const GLenum error = glGetError();
		if (error == GL_NO_ERROR) {
			break;
		}
	}
}

void OXRCheckErrors(XrResult result, const char* file, int line) {
	if (XR_FAILED(result)) {
		char errorBuffer[XR_MAX_RESULT_STRING_SIZE];
		xrResultToString(xr_module_engine.Instance, result, errorBuffer);
	}
}
#endif

JNIEXPORT void JNICALL Java_com_winlator_XrActivity_init(JNIEnv *env, jobject obj) {

    if (xr_initialized) {
        return;
    }

    memset(&xr_module_engine, 0, sizeof(xr_module_engine));
    xr_module_engine.PlatformFlag[PLATFORM_CONTROLLER_QUEST] = true;
    xr_module_engine.PlatformFlag[PLATFORM_EXTENSION_PASSTHROUGH] = true;
    xr_module_engine.PlatformFlag[PLATFORM_EXTENSION_PERFORMANCE] = true;

    JavaVM* vm;
    (*env)->GetJavaVM(env, &vm);

    xrJava java;
    java.vm = vm;
    java.activity = (*env)->NewGlobalRef(env, obj);
    XrEngineInit(&xr_module_engine, &java, "Winlator", 1);

    XrEngineEnter(&xr_module_engine);
    XrInputInit(&xr_module_engine, &xr_module_input);
    XrRendererInit(&xr_module_engine, &xr_module_renderer);
    xr_initialized = true;
}

JNIEXPORT void JNICALL Java_com_winlator_XrActivity_bindFramebuffer(JNIEnv *env, jobject obj) {
    if (xr_initialized) {
        XrRendererBindFramebuffer(&xr_module_renderer);
    }
}

JNIEXPORT jint JNICALL Java_com_winlator_XrActivity_getWidth(JNIEnv *env, jobject obj) {
    int w, h;
    XrRendererGetResolution(&xr_module_engine, &xr_module_renderer, &w, &h);
    return w;
}
JNIEXPORT jint JNICALL Java_com_winlator_XrActivity_getHeight(JNIEnv *env, jobject obj) {
    int w, h;
    XrRendererGetResolution(&xr_module_engine, &xr_module_renderer, &w, &h);
    return h;
}

JNIEXPORT jboolean JNICALL Java_com_winlator_XrActivity_beginFrame(JNIEnv *env, jobject obj, jboolean immersive, jboolean sbs) {
    if (XrRendererInitFrame(&xr_module_engine, &xr_module_renderer)) {

        int mode = immersive ? RENDER_MODE_MONO_6DOF : RENDER_MODE_MONO_SCREEN;
        xr_module_renderer.ConfigFloat[CONFIG_CANVAS_DISTANCE] = 5.0f;
        xr_module_renderer.ConfigInt[CONFIG_PASSTHROUGH] = !immersive;
        xr_module_renderer.ConfigInt[CONFIG_MODE] = mode;
        xr_module_renderer.ConfigInt[CONFIG_SBS] = sbs;

        static int last_immersive = -1;
        if (last_immersive != immersive) {
            XrRendererRecenter(&xr_module_engine, &xr_module_renderer);
            last_immersive = immersive;
        }

        XrInputUpdate(&xr_module_engine, &xr_module_input);

        XrRendererBeginFrame(&xr_module_renderer, 0);

        return true;
    }
    return false;
}

JNIEXPORT void JNICALL Java_com_winlator_XrActivity_endFrame(JNIEnv *env, jobject obj) {
    XrRendererEndFrame(&xr_module_renderer);
    XrRendererFinishFrame(&xr_module_engine, &xr_module_renderer);
}

JNIEXPORT jfloatArray JNICALL Java_com_winlator_XrActivity_getAxes(JNIEnv *env, jobject obj) {
    XrPosef lPose = XrInputGetPose(&xr_module_input, 0);
    XrPosef rPose = XrInputGetPose(&xr_module_input, 1);
    XrVector2f lThumbstick = XrInputGetJoystickState(&xr_module_input, 0);
    XrVector2f rThumbstick = XrInputGetJoystickState(&xr_module_input, 1);
    XrVector3f lPosition = xr_module_renderer.Projections[0].pose.position;
    XrVector3f rPosition = xr_module_renderer.Projections[1].pose.position;
    XrVector3f angles = xr_module_renderer.HmdOrientation;

    XrVector3f left_euler = XrQuaternionfEulerAngles(lPose.orientation);
    XrVector3f right_euler = XrQuaternionfEulerAngles(rPose.orientation);

    XrInputData input;
    input.left_angular[0] = left_euler.x;
    input.left_angular[1] = left_euler.y;
    input.left_angular[2] = left_euler.z;
    input.left_stick[0] = lThumbstick.x;
    input.left_stick[1] = lThumbstick.y;
    input.left_hand[0] = lPose.position.x;
    input.left_hand[1] = lPose.position.y;
    input.left_hand[2] = lPose.position.z;
    input.right_angular[0] = right_euler.x;
    input.right_angular[1] = right_euler.y;
    input.right_angular[2] = right_euler.z;
    input.right_stick[0] = rThumbstick.x;
    input.right_stick[1] = rThumbstick.y;
    input.right_hand[0] = rPose.position.x;
    input.right_hand[1] = rPose.position.y;
    input.right_hand[2] = rPose.position.z;
    input.head[0] = angles.x;
    input.head[1] = angles.y;
    input.head[2] = angles.z;
    input.head[3] = (lPosition.x + rPosition.x) * 0.5f;
    input.head[4] = (lPosition.y + rPosition.y) * 0.5f;
    input.head[5] = (lPosition.z + rPosition.z) * 0.5f;
    input.head[6] = XrVector3fDistance(lPosition, rPosition);

    jfloat values[23];
    int count = 0;
    memcpy(values + count, input.left_angular, sizeof(input.left_angular)); count += 3;
    memcpy(values + count, input.left_stick, sizeof(input.left_stick)); count += 2;
    memcpy(values + count, input.left_hand, 3 * sizeof(float)); count += 3;
    memcpy(values + count, input.right_angular, sizeof(input.right_angular)); count += 3;
    memcpy(values + count, input.right_stick, sizeof(input.right_stick)); count += 2;
    memcpy(values + count, input.right_hand, 3 * sizeof(float)); count += 3;
    memcpy(values + count, input.head, sizeof(input.head)); count += 7;

    jfloatArray output = (*env)->NewFloatArray(env, count);
    (*env)->SetFloatArrayRegion(env, output, (jsize)0, (jsize)count, values);
    return output;
}

JNIEXPORT jbooleanArray JNICALL Java_com_winlator_XrActivity_getButtons(JNIEnv *env, jobject obj) {
    uint32_t l = XrInputGetButtonState(&xr_module_input, 0);
    uint32_t r = XrInputGetButtonState(&xr_module_input, 1);

    XrInputData input;
    input.buttons[0] = l & (int)Grip;
    input.buttons[1] = l & (int)Enter;
    input.buttons[2] = l & (int)LThumb;
    input.buttons[3] = l & (int)Left;
    input.buttons[4] = l & (int)Right;
    input.buttons[5] = l & (int)Up;
    input.buttons[6] = l & (int)Down;
    input.buttons[7] = l & (int)Trigger;
    input.buttons[8] = l & (int)X;
    input.buttons[9] = l & (int)Y;
    input.buttons[10] = r & (int)A;
    input.buttons[11] = r & (int)B;
    input.buttons[12] = r & (int)Grip;
    input.buttons[13] = r & (int)RThumb;
    input.buttons[14] = r & (int)Left;
    input.buttons[15] = r & (int)Right;
    input.buttons[16] = r & (int)Up;
    input.buttons[17] = r & (int)Down;
    input.buttons[18] = r & (int)Trigger;

    jboolean values[19];
    memcpy(values, input.buttons, 19 * sizeof(jboolean));
    jbooleanArray output = (*env)->NewBooleanArray(env, 19);
    (*env)->SetBooleanArrayRegion(env, output, (jsize)0, (jsize)19, values);
    return output;
}

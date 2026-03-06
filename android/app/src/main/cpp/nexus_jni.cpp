/*
 * NEXUS Protocol -- JNI Bridge for Android
 *
 * Maps the libnexus C API to Java/Kotlin methods.
 * The NexusNode Kotlin class calls these native methods.
 */
#include <jni.h>
#include <string.h>
#include <android/log.h>

extern "C" {
#include "nexus/node.h"
#include "nexus/identity.h"
#include "nexus/transport.h"
}

#define TAG "NexusJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* ── Global state ─────────────────────────────────────────────────────── */

static nx_node_t g_node;
static bool g_running = false;
static JavaVM *g_jvm = nullptr;

/* Callback references */
static jobject g_callback_obj = nullptr;
static jmethodID g_on_data_method = nullptr;
static jmethodID g_on_neighbor_method = nullptr;
static jmethodID g_on_session_method = nullptr;

/* ── Helper: get JNIEnv for current thread ────────────────────────────── */

static JNIEnv *get_env()
{
    JNIEnv *env = nullptr;
    if (g_jvm) {
        if (g_jvm->GetEnv((void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
            g_jvm->AttachCurrentThread(&env, nullptr);
        }
    }
    return env;
}

/* ── Callbacks from C to Java ─────────────────────────────────────────── */

static void jni_on_data(const nx_addr_short_t *src,
                        const uint8_t *data, size_t len, void *user)
{
    (void)user;
    JNIEnv *env = get_env();
    if (!env || !g_callback_obj || !g_on_data_method) return;

    jbyteArray jsrc = env->NewByteArray(4);
    jbyteArray jdata = env->NewByteArray((jsize)len);
    env->SetByteArrayRegion(jsrc, 0, 4, (jbyte *)src->bytes);
    env->SetByteArrayRegion(jdata, 0, (jsize)len, (jbyte *)data);

    env->CallVoidMethod(g_callback_obj, g_on_data_method, jsrc, jdata);

    env->DeleteLocalRef(jsrc);
    env->DeleteLocalRef(jdata);
}

static void jni_on_neighbor(const nx_addr_short_t *addr,
                            nx_role_t role, void *user)
{
    (void)user;
    JNIEnv *env = get_env();
    if (!env || !g_callback_obj || !g_on_neighbor_method) return;

    jbyteArray jaddr = env->NewByteArray(4);
    env->SetByteArrayRegion(jaddr, 0, 4, (jbyte *)addr->bytes);

    env->CallVoidMethod(g_callback_obj, g_on_neighbor_method, jaddr, (jint)role);

    env->DeleteLocalRef(jaddr);
}

static void jni_on_session(const nx_addr_short_t *src,
                           const uint8_t *data, size_t len, void *user)
{
    (void)user;
    JNIEnv *env = get_env();
    if (!env || !g_callback_obj || !g_on_session_method) return;

    jbyteArray jsrc = env->NewByteArray(4);
    jbyteArray jdata = env->NewByteArray((jsize)len);
    env->SetByteArrayRegion(jsrc, 0, 4, (jbyte *)src->bytes);
    env->SetByteArrayRegion(jdata, 0, (jsize)len, (jbyte *)data);

    env->CallVoidMethod(g_callback_obj, g_on_session_method, jsrc, jdata);

    env->DeleteLocalRef(jsrc);
    env->DeleteLocalRef(jdata);
}

/* ── JNI exports ──────────────────────────────────────────────────────── */

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;
    g_jvm = vm;
    LOGI("JNI_OnLoad");
    return JNI_VERSION_1_6;
}

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeInit(JNIEnv *env, jobject thiz,
                                                  jint role, jobject callback)
{
    (void)thiz;
    if (g_running) return JNI_FALSE;

    /* Store callback reference */
    g_callback_obj = env->NewGlobalRef(callback);
    jclass cls = env->GetObjectClass(callback);
    g_on_data_method = env->GetMethodID(cls, "onData", "([B[B)V");
    g_on_neighbor_method = env->GetMethodID(cls, "onNeighbor", "([BI)V");
    g_on_session_method = env->GetMethodID(cls, "onSession", "([B[B)V");

    /* Initialize transport registry */
    nx_transport_registry_init();

    /* Configure node */
    nx_node_config_t cfg = {};
    cfg.role = (nx_role_t)role;
    cfg.default_ttl = 7;
    cfg.beacon_interval_ms = 30000;
    cfg.on_data = jni_on_data;
    cfg.on_neighbor = jni_on_neighbor;
    cfg.on_session = jni_on_session;

    nx_err_t err = nx_node_init(&g_node, &cfg);
    if (err != NX_OK) {
        LOGE("Node init failed: %d", err);
        return JNI_FALSE;
    }

    g_running = true;

    const nx_identity_t *id = nx_node_identity(&g_node);
    LOGI("Node initialized: %02X%02X%02X%02X",
         id->short_addr.bytes[0], id->short_addr.bytes[1],
         id->short_addr.bytes[2], id->short_addr.bytes[3]);

    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeInitWithIdentity(
    JNIEnv *env, jobject thiz, jint role, jbyteArray identity_bytes,
    jobject callback)
{
    (void)thiz;
    if (g_running) return JNI_FALSE;

    /* Store callback */
    g_callback_obj = env->NewGlobalRef(callback);
    jclass cls = env->GetObjectClass(callback);
    g_on_data_method = env->GetMethodID(cls, "onData", "([B[B)V");
    g_on_neighbor_method = env->GetMethodID(cls, "onNeighbor", "([BI)V");
    g_on_session_method = env->GetMethodID(cls, "onSession", "([B[B)V");

    /* Deserialize identity */
    nx_identity_t id;
    jsize len = env->GetArrayLength(identity_bytes);
    if ((size_t)len != sizeof(nx_identity_t)) return JNI_FALSE;
    env->GetByteArrayRegion(identity_bytes, 0, len, (jbyte *)&id);

    nx_transport_registry_init();

    nx_node_config_t cfg = {};
    cfg.role = (nx_role_t)role;
    cfg.default_ttl = 7;
    cfg.beacon_interval_ms = 30000;
    cfg.on_data = jni_on_data;
    cfg.on_neighbor = jni_on_neighbor;
    cfg.on_session = jni_on_session;

    nx_err_t err = nx_node_init_with_identity(&g_node, &cfg, &id);
    if (err != NX_OK) return JNI_FALSE;

    g_running = true;
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeStop(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    if (!g_running) return;

    nx_node_stop(&g_node);
    g_running = false;

    if (g_callback_obj) {
        env->DeleteGlobalRef(g_callback_obj);
        g_callback_obj = nullptr;
    }
    LOGI("Node stopped");
}

JNIEXPORT void JNICALL
Java_com_nexus_mesh_service_NexusNode_nativePoll(JNIEnv *env, jobject thiz,
                                                  jint timeout_ms)
{
    (void)env; (void)thiz;
    if (!g_running) return;
    nx_node_poll(&g_node, (uint32_t)timeout_ms);
}

JNIEXPORT jbyteArray JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeGetAddress(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    if (!g_running) return nullptr;

    const nx_identity_t *id = nx_node_identity(&g_node);
    jbyteArray result = env->NewByteArray(4);
    env->SetByteArrayRegion(result, 0, 4, (jbyte *)id->short_addr.bytes);
    return result;
}

JNIEXPORT jbyteArray JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeGetIdentityBytes(JNIEnv *env,
                                                               jobject thiz)
{
    (void)thiz;
    if (!g_running) return nullptr;

    const nx_identity_t *id = nx_node_identity(&g_node);
    jbyteArray result = env->NewByteArray(sizeof(nx_identity_t));
    env->SetByteArrayRegion(result, 0, sizeof(nx_identity_t), (jbyte *)id);
    return result;
}

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeSend(JNIEnv *env, jobject thiz,
                                                  jbyteArray dest,
                                                  jbyteArray data)
{
    (void)thiz;
    if (!g_running) return JNI_FALSE;

    nx_addr_short_t dst;
    env->GetByteArrayRegion(dest, 0, 4, (jbyte *)dst.bytes);

    jsize len = env->GetArrayLength(data);
    jbyte *buf = env->GetByteArrayElements(data, nullptr);

    nx_err_t err = nx_node_send(&g_node, &dst, (uint8_t *)buf, (size_t)len);
    env->ReleaseByteArrayElements(data, buf, JNI_ABORT);

    return (err == NX_OK) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeSessionStart(JNIEnv *env,
                                                          jobject thiz,
                                                          jbyteArray dest)
{
    (void)thiz;
    if (!g_running) return JNI_FALSE;

    nx_addr_short_t dst;
    env->GetByteArrayRegion(dest, 0, 4, (jbyte *)dst.bytes);

    return (nx_node_session_start(&g_node, &dst) == NX_OK) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeSendSession(JNIEnv *env,
                                                         jobject thiz,
                                                         jbyteArray dest,
                                                         jbyteArray data)
{
    (void)thiz;
    if (!g_running) return JNI_FALSE;

    nx_addr_short_t dst;
    env->GetByteArrayRegion(dest, 0, 4, (jbyte *)dst.bytes);

    jsize len = env->GetArrayLength(data);
    jbyte *buf = env->GetByteArrayElements(data, nullptr);

    nx_err_t err = nx_node_send_session(&g_node, &dst, (uint8_t *)buf, (size_t)len);
    env->ReleaseByteArrayElements(data, buf, JNI_ABORT);

    return (err == NX_OK) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeAnnounce(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    if (!g_running) return;
    nx_node_announce(&g_node);
}

/* Inject raw packet data from BLE transport (received from ESP32) */
JNIEXPORT void JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeInjectPacket(JNIEnv *env,
                                                          jobject thiz,
                                                          jbyteArray packet)
{
    (void)thiz;
    if (!g_running) return;

    /* For BLE transport bridging: packets from ESP32 are injected
     * into the node's transport layer for processing. This requires
     * a pipe transport registered at init time. */
    /* TODO: implement BLE-to-pipe bridge */
    (void)env; (void)packet;
}

} /* extern "C" */

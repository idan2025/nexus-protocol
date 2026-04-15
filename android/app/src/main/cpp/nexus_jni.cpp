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
#include "nexus/group.h"
#include "nexus/platform.h"
}

#define TAG "NexusJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* ── Global state ─────────────────────────────────────────────────────── */

static nx_node_t g_node;
static bool g_running = false;
static JavaVM *g_jvm = nullptr;
static nx_transport_t *g_tcp_inet = nullptr;
static nx_transport_t *g_udp_mcast = nullptr;

/* Callback references */
static jobject g_callback_obj = nullptr;
static jmethodID g_on_data_method = nullptr;
static jmethodID g_on_neighbor_method = nullptr;
static jmethodID g_on_session_method = nullptr;
static jmethodID g_on_group_method = nullptr;

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

static void jni_on_group(const nx_addr_short_t *group_id,
                         const nx_addr_short_t *src,
                         const uint8_t *data, size_t len, void *user)
{
    (void)user;
    JNIEnv *env = get_env();
    if (!env || !g_callback_obj || !g_on_group_method) return;

    jbyteArray jgid = env->NewByteArray(4);
    jbyteArray jsrc = env->NewByteArray(4);
    jbyteArray jdata = env->NewByteArray((jsize)len);
    env->SetByteArrayRegion(jgid, 0, 4, (jbyte *)group_id->bytes);
    env->SetByteArrayRegion(jsrc, 0, 4, (jbyte *)src->bytes);
    env->SetByteArrayRegion(jdata, 0, (jsize)len, (jbyte *)data);

    env->CallVoidMethod(g_callback_obj, g_on_group_method, jgid, jsrc, jdata);

    env->DeleteLocalRef(jgid);
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
    g_on_group_method = env->GetMethodID(cls, "onGroup", "([B[B[B)V");

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
    cfg.on_group = jni_on_group;

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
    g_on_group_method = env->GetMethodID(cls, "onGroup", "([B[B[B)V");

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
    cfg.on_group = jni_on_group;

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

    nx_err_t err = nx_node_send_raw(&g_node, &dst, (uint8_t *)buf, (size_t)len);
    env->ReleaseByteArrayElements(data, buf, JNI_ABORT);

    return (err == NX_OK) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeSendLarge(JNIEnv *env, jobject thiz,
                                                      jbyteArray dest,
                                                      jbyteArray data)
{
    (void)thiz;
    if (!g_running) return JNI_FALSE;

    nx_addr_short_t dst;
    env->GetByteArrayRegion(dest, 0, 4, (jbyte *)dst.bytes);

    jsize len = env->GetArrayLength(data);
    jbyte *buf = env->GetByteArrayElements(data, nullptr);

    nx_err_t err = nx_node_send_large(&g_node, &dst, (uint8_t *)buf, (size_t)len);
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

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeRequestInbox(JNIEnv *env, jobject thiz,
                                                         jbyteArray target)
{
    (void)thiz;
    if (!g_running) return JNI_FALSE;

    nx_addr_short_t t;
    env->GetByteArrayRegion(target, 0, 4, (jbyte *)t.bytes);
    return (nx_node_request_inbox(&g_node, &t) == NX_OK) ? JNI_TRUE : JNI_FALSE;
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

/*
 * TCP Internet transport -- connect to remote NEXUS nodes over the network.
 *
 * listenPort: port to listen on (0 = no server)
 * peerHosts: array of "host" strings for outbound peers
 * peerPorts: array of port numbers for outbound peers
 * reconnectMs: auto-reconnect interval in ms
 */
JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeStartTcpInet(JNIEnv *env,
                                                          jobject thiz,
                                                          jint listenPort,
                                                          jobjectArray peerHosts,
                                                          jintArray peerPorts,
                                                          jint reconnectMs)
{
    (void)thiz;
    if (!g_running) return JNI_FALSE;
    if (g_tcp_inet) return JNI_TRUE; /* Already running */

    g_tcp_inet = nx_tcp_inet_transport_create();
    if (!g_tcp_inet) return JNI_FALSE;

    nx_tcp_inet_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.listen_port = (uint16_t)listenPort;
    cfg.listen_host = "0.0.0.0";
    cfg.reconnect_interval_ms = (uint32_t)(reconnectMs > 0 ? reconnectMs : 5000);

    /* Copy peer info from Java arrays */
    int peer_count = 0;
    static char peer_hosts_buf[NX_TCP_INET_MAX_PEERS][64];

    if (peerHosts && peerPorts) {
        peer_count = env->GetArrayLength(peerHosts);
        if (peer_count > NX_TCP_INET_MAX_PEERS)
            peer_count = NX_TCP_INET_MAX_PEERS;

        jint *ports = env->GetIntArrayElements(peerPorts, nullptr);
        for (int i = 0; i < peer_count; i++) {
            jstring jhost = (jstring)env->GetObjectArrayElement(peerHosts, i);
            const char *host = env->GetStringUTFChars(jhost, nullptr);
            strncpy(peer_hosts_buf[i], host, sizeof(peer_hosts_buf[i]) - 1);
            peer_hosts_buf[i][sizeof(peer_hosts_buf[i]) - 1] = '\0';
            env->ReleaseStringUTFChars(jhost, host);

            cfg.peers[i].host = peer_hosts_buf[i];
            cfg.peers[i].port = (uint16_t)ports[i];
        }
        env->ReleaseIntArrayElements(peerPorts, ports, JNI_ABORT);
    }
    cfg.peer_count = peer_count;

    nx_err_t err = g_tcp_inet->ops->init(g_tcp_inet, &cfg);
    if (err != NX_OK) {
        LOGE("TCP inet init failed: %d", err);
        nx_platform_free(g_tcp_inet);
        g_tcp_inet = nullptr;
        return JNI_FALSE;
    }

    err = nx_transport_register(g_tcp_inet);
    if (err != NX_OK) {
        LOGE("TCP inet register failed: %d", err);
        g_tcp_inet->ops->destroy(g_tcp_inet);
        nx_platform_free(g_tcp_inet);
        g_tcp_inet = nullptr;
        return JNI_FALSE;
    }

    LOGI("TCP inet started (listen:%d, peers:%d)", listenPort, peer_count);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeStopTcpInet(JNIEnv *env,
                                                         jobject thiz)
{
    (void)env; (void)thiz;
    if (g_tcp_inet) {
        g_tcp_inet->ops->destroy(g_tcp_inet);
        nx_platform_free(g_tcp_inet);
        g_tcp_inet = nullptr;
        LOGI("TCP inet stopped");
    }
}

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeIsTcpInetActive(JNIEnv *env,
                                                              jobject thiz)
{
    (void)env; (void)thiz;
    return (g_tcp_inet && g_tcp_inet->active) ? JNI_TRUE : JNI_FALSE;
}

/* ── Route / Neighbor info queries ───────────────────────────────────── */

/*
 * Get route info for a destination address.
 * Returns int array: [hop_count, via_transport, next_hop_b0..b3] or null.
 */
JNIEXPORT jintArray JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeGetRouteInfo(JNIEnv *env,
                                                          jobject thiz,
                                                          jbyteArray dest)
{
    (void)thiz;
    if (!g_running) return nullptr;

    nx_addr_short_t dst;
    env->GetByteArrayRegion(dest, 0, 4, (jbyte *)dst.bytes);

    const nx_route_table_t *rt = nx_node_route_table(&g_node);

    /* Check if it's a direct neighbor first */
    const nx_neighbor_t *nb = nx_neighbor_find(rt, &dst);
    if (nb) {
        /* Direct neighbor: hop_count=1, transport=0, next_hop=self */
        jintArray result = env->NewIntArray(6);
        jint vals[6] = { 1, 0,
            (jint)(uint8_t)nb->addr.bytes[0], (jint)(uint8_t)nb->addr.bytes[1],
            (jint)(uint8_t)nb->addr.bytes[2], (jint)(uint8_t)nb->addr.bytes[3] };
        env->SetIntArrayRegion(result, 0, 6, vals);
        return result;
    }

    /* Check route table */
    const nx_route_t *route = nx_route_lookup(rt, &dst);
    if (route) {
        jintArray result = env->NewIntArray(6);
        jint vals[6] = {
            (jint)route->hop_count, (jint)route->via_transport,
            (jint)(uint8_t)route->next_hop.bytes[0],
            (jint)(uint8_t)route->next_hop.bytes[1],
            (jint)(uint8_t)route->next_hop.bytes[2],
            (jint)(uint8_t)route->next_hop.bytes[3]
        };
        env->SetIntArrayRegion(result, 0, 6, vals);
        return result;
    }

    return nullptr; /* No route known */
}

/*
 * Check if an address is a direct neighbor.
 * Returns role (>=0) or -1 if not a neighbor.
 */
JNIEXPORT jint JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeIsNeighbor(JNIEnv *env,
                                                        jobject thiz,
                                                        jbyteArray addr)
{
    (void)thiz;
    if (!g_running) return -1;

    nx_addr_short_t a;
    env->GetByteArrayRegion(addr, 0, 4, (jbyte *)a.bytes);

    const nx_route_table_t *rt = nx_node_route_table(&g_node);
    const nx_neighbor_t *nb = nx_neighbor_find(rt, &a);
    return nb ? (jint)nb->role : -1;
}

/*
 * Live telemetry snapshot. Returns int[10]:
 *   0: neighbor count
 *   1: active routes
 *   2: NX_MAX_ROUTES
 *   3: anchor stored slots
 *   4: anchor capacity (NX_ANCHOR_MAX_STORED)
 *   5: session count
 *   6: NX_SESSION_MAX
 *   7: transport count
 *   8: node role
 *   9: 1 if running, else 0
 */
JNIEXPORT jintArray JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeGetTelemetry(JNIEnv *env,
                                                          jobject thiz)
{
    (void)thiz;
    jintArray result = env->NewIntArray(10);
    if (!result) return nullptr;

    jint vals[10] = {0};
    vals[2] = NX_MAX_ROUTES;
    vals[4] = NX_ANCHOR_MAX_STORED;
    vals[6] = NX_SESSION_MAX;

    if (g_running) {
        const nx_route_table_t *rt = nx_node_route_table(&g_node);
        vals[0] = nx_neighbor_count(rt);
        int active = 0;
        for (int i = 0; i < NX_MAX_ROUTES; i++) {
            if (rt->routes[i].valid) active++;
        }
        vals[1] = active;
        vals[3] = nx_anchor_count(&g_node.anchor);
        vals[5] = nx_session_count(&g_node.sessions);
        vals[7] = nx_transport_count();
        vals[8] = (jint)g_node.config.role;
        vals[9] = 1;
    }

    env->SetIntArrayRegion(result, 0, 10, vals);
    return result;
}

/* ── UDP Multicast (auto-discovery on all interfaces) ────────────────── */

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeStartUdpMulticast(JNIEnv *env,
                                                               jobject thiz)
{
    (void)env; (void)thiz;
    if (!g_running) return JNI_FALSE;
    if (g_udp_mcast) return JNI_TRUE;

    g_udp_mcast = nx_udp_mcast_transport_create();
    if (!g_udp_mcast) return JNI_FALSE;

    nx_udp_mcast_config_t cfg = { .group = nullptr, .port = 0 };
    nx_err_t err = g_udp_mcast->ops->init(g_udp_mcast, &cfg);
    if (err != NX_OK) {
        LOGE("UDP multicast init failed: %d", err);
        nx_platform_free(g_udp_mcast);
        g_udp_mcast = nullptr;
        return JNI_FALSE;
    }

    err = nx_transport_register(g_udp_mcast);
    if (err != NX_OK) {
        g_udp_mcast->ops->destroy(g_udp_mcast);
        nx_platform_free(g_udp_mcast);
        g_udp_mcast = nullptr;
        return JNI_FALSE;
    }

    LOGI("UDP multicast started on all interfaces");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeStopUdpMulticast(JNIEnv *env,
                                                              jobject thiz)
{
    (void)env; (void)thiz;
    if (g_udp_mcast) {
        g_udp_mcast->ops->destroy(g_udp_mcast);
        nx_platform_free(g_udp_mcast);
        g_udp_mcast = nullptr;
        LOGI("UDP multicast stopped");
    }
}

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeIsUdpMulticastActive(JNIEnv *env,
                                                                   jobject thiz)
{
    (void)env; (void)thiz;
    return (g_udp_mcast && g_udp_mcast->active) ? JNI_TRUE : JNI_FALSE;
}

/* ── Sign pubkey for QR code ──────────────────────────────────────────── */

JNIEXPORT jbyteArray JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeGetSignPubkey(JNIEnv *env,
                                                           jobject thiz)
{
    (void)thiz;
    if (!g_running) return nullptr;

    const nx_identity_t *id = nx_node_identity(&g_node);
    jbyteArray result = env->NewByteArray(32);
    env->SetByteArrayRegion(result, 0, 32, (jbyte *)id->sign_public);
    return result;
}

/* ── Group operations ────────────────────────────────────────────────── */

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeGroupCreate(JNIEnv *env,
                                                         jobject thiz,
                                                         jbyteArray groupId,
                                                         jbyteArray key)
{
    (void)thiz;
    if (!g_running) return JNI_FALSE;

    nx_addr_short_t gid;
    env->GetByteArrayRegion(groupId, 0, 4, (jbyte *)gid.bytes);

    uint8_t gkey[32];
    env->GetByteArrayRegion(key, 0, 32, (jbyte *)gkey);

    nx_err_t err = nx_node_group_create(&g_node, &gid, gkey);
    return (err == NX_OK) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeGroupAddMember(JNIEnv *env,
                                                            jobject thiz,
                                                            jbyteArray groupId,
                                                            jbyteArray memberAddr)
{
    (void)thiz;
    if (!g_running) return JNI_FALSE;

    nx_addr_short_t gid;
    env->GetByteArrayRegion(groupId, 0, 4, (jbyte *)gid.bytes);

    nx_addr_short_t addr;
    env->GetByteArrayRegion(memberAddr, 0, 4, (jbyte *)addr.bytes);

    nx_err_t err = nx_node_group_add_member(&g_node, &gid, &addr);
    return (err == NX_OK) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeGroupSend(JNIEnv *env,
                                                       jobject thiz,
                                                       jbyteArray groupId,
                                                       jbyteArray data)
{
    (void)thiz;
    if (!g_running) return JNI_FALSE;

    nx_addr_short_t gid;
    env->GetByteArrayRegion(groupId, 0, 4, (jbyte *)gid.bytes);

    jsize len = env->GetArrayLength(data);
    jbyte *buf = env->GetByteArrayElements(data, nullptr);

    nx_err_t err = nx_node_group_send(&g_node, &gid, (uint8_t *)buf, (size_t)len);
    env->ReleaseByteArrayElements(data, buf, JNI_ABORT);

    return (err == NX_OK) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobjectArray JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeGroupList(JNIEnv *env,
                                                       jobject thiz)
{
    (void)thiz;
    if (!g_running) return nullptr;

    int count = 0;
    for (int i = 0; i < NX_GROUP_MAX; i++) {
        if (g_node.groups.groups[i].valid) count++;
    }

    jclass byteArrayClass = env->FindClass("[B");
    jobjectArray result = env->NewObjectArray(count, byteArrayClass, nullptr);
    int idx = 0;
    for (int i = 0; i < NX_GROUP_MAX && idx < count; i++) {
        if (g_node.groups.groups[i].valid) {
            jbyteArray gid = env->NewByteArray(4);
            env->SetByteArrayRegion(gid, 0, 4,
                (jbyte *)g_node.groups.groups[i].group_id.bytes);
            env->SetObjectArrayElement(result, idx++, gid);
            env->DeleteLocalRef(gid);
        }
    }
    return result;
}

JNIEXPORT jobjectArray JNICALL
Java_com_nexus_mesh_service_NexusNode_nativeGroupGetMembers(JNIEnv *env,
                                                              jobject thiz,
                                                              jbyteArray groupId)
{
    (void)thiz;
    if (!g_running) return nullptr;

    nx_addr_short_t gid;
    env->GetByteArrayRegion(groupId, 0, 4, (jbyte *)gid.bytes);

    /* Find the group */
    const nx_group_t *grp = nx_group_find(&g_node.groups, &gid);
    if (!grp) return nullptr;

    /* Count valid members */
    int member_count = 0;
    for (int i = 0; i < NX_GROUP_MAX_MEMBERS; i++) {
        if (grp->members[i].valid) member_count++;
    }

    jclass byteArrayClass = env->FindClass("[B");
    jobjectArray result = env->NewObjectArray(member_count, byteArrayClass, nullptr);
    int idx = 0;
    for (int i = 0; i < NX_GROUP_MAX_MEMBERS && idx < member_count; i++) {
        if (grp->members[i].valid) {
            jbyteArray addr = env->NewByteArray(4);
            env->SetByteArrayRegion(addr, 0, 4, (jbyte *)grp->members[i].addr.bytes);
            env->SetObjectArrayElement(result, idx++, addr);
            env->DeleteLocalRef(addr);
        }
    }
    return result;
}

} /* extern "C" */

/****************************************************************************
 * app/home_scense/doubao/doubao_voice.c
 * Persistent-session push-to-talk coordinator for Doubao RealtimeAPI.
 *
 * A single background worker connects once at init and keeps the session
 * alive.  Each button press is a new *turn* on the same connection — no
 * reconnect per turn, and the server retains dialog context across turns.
 * Between turns the worker services WebSocket pings so the link stays up;
 * if the link drops it transparently reconnects before the next turn.
 ****************************************************************************/

#include "doubao_voice.h"
#include "doubao_protocol.h"
#include "voice_capture.h"
#include "voice_player.h"
#include "voice_transport.h"
#include "../wifi_status.h"

#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 诊断日志:写 /tmp/doubao.log(adb 可读)兼 stderr(串口)。带毫秒时间戳。 */
#include <time.h>
#define DVLOG(fmt, ...) \
  do { FILE *_f=fopen("/tmp/doubao.log","a"); \
    if(_f){ struct timespec _ts; clock_gettime(CLOCK_MONOTONIC,&_ts); \
      fprintf(_f,"[%ld.%03ld] " fmt "\n", \
        (long)_ts.tv_sec,(long)(_ts.tv_nsec/1000000),##__VA_ARGS__); \
      fclose(_f);} } while(0)

#if __has_include("doubao_secret.h")
#  include "doubao_secret.h"
#  define DOUBAO_SECRETS_AVAILABLE 1
#else
#  define DOUBAO_SECRETS_AVAILABLE 0
#  define DOUBAO_APP_ID ""
#  define DOUBAO_ACCESS_TOKEN ""
#  define DOUBAO_MODEL ""
#  define DOUBAO_SPEAKER ""
#endif

#ifndef CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_CAPTURE_DEV
#  define CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_CAPTURE_DEV "/dev/audio/pcm0c"
#endif
#ifndef CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_PLAYBACK_DEV
#  define CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_PLAYBACK_DEV "/dev/audio/pcm0p"
#endif

#define DOUBAO_FRAME_CAPACITY (DOUBAO_WS_BUFFER_SIZE + 1024)

typedef struct doubao_voice_context_s
{
  pthread_mutex_t mutex;
  pthread_t worker;
  bool initialized;
  bool worker_running;   /* 持久 worker 线程存活 */
  bool shutdown;         /* deinit 请求退出 worker */
  bool press;            /* 按钮当前请求录音/发起一轮 */
  doubao_voice_snapshot_t snapshot;
  doubao_voice_config_t config;
} doubao_voice_context_t;

static doubao_voice_context_t g_voice;

static void copy_text(char *destination, size_t size, const char *source)
{
  if (!destination || size == 0)
    {
      return;
    }
  if (!source)
    {
      destination[0] = '\0';
      return;
    }
  strncpy(destination, source, size - 1);
  destination[size - 1] = '\0';
}

static void set_state(doubao_voice_state_t state, const char *error)
{
  pthread_mutex_lock(&g_voice.mutex);
  g_voice.snapshot.state = state;
  if (error)
    {
      copy_text(g_voice.snapshot.error_text,
                sizeof(g_voice.snapshot.error_text), error);
    }
  else if (state != DOUBAO_VOICE_ERROR)
    {
      g_voice.snapshot.error_text[0] = '\0';
    }
  pthread_mutex_unlock(&g_voice.mutex);
}

static bool is_pressed(void)
{
  bool requested;
  pthread_mutex_lock(&g_voice.mutex);
  requested = g_voice.press;
  pthread_mutex_unlock(&g_voice.mutex);
  return requested;
}

static bool is_shutdown(void)
{
  bool requested;
  pthread_mutex_lock(&g_voice.mutex);
  requested = g_voice.shutdown;
  pthread_mutex_unlock(&g_voice.mutex);
  return requested;
}

static void begin_turn_snapshot(void)
{
  pthread_mutex_lock(&g_voice.mutex);
  /* turn_seq 在"开始"自增(与清空文字同一临界区):UI 侧据此在每轮边界
   * 只重置一次当前气泡引用,且重置时文字已空,不会用旧文字重复建气泡、
   * 也不会把新一轮的 ASR 覆盖到上一轮的气泡上。 */
  g_voice.snapshot.turn_seq++;
  g_voice.snapshot.user_text[0] = '\0';
  g_voice.snapshot.assistant_text[0] = '\0';
  g_voice.snapshot.error_text[0] = '\0';
  pthread_mutex_unlock(&g_voice.mutex);
}

static void append_reply(const char *text)
{
  size_t used;
  size_t available;

  if (!text)
    {
      return;
    }
  pthread_mutex_lock(&g_voice.mutex);
  used = strlen(g_voice.snapshot.assistant_text);
  available = sizeof(g_voice.snapshot.assistant_text) - used - 1;
  strncat(g_voice.snapshot.assistant_text, text, available);
  pthread_mutex_unlock(&g_voice.mutex);
}

static void make_identifier(char *output, size_t size, const char *prefix)
{
  static unsigned int next_id;
  snprintf(output, size, "%s-%08x", prefix, ++next_id);
}

static int send_json(voice_transport_t *transport, int event,
                     const char *session_id, const char *json)
{
  uint8_t frame[DOUBAO_FRAME_CAPACITY];
  size_t frame_size;
  int ret;

  ret = doubao_protocol_encode_json(event, session_id, json, frame,
                                    sizeof(frame), &frame_size);
  if (ret < 0)
    {
      return ret;
    }
  return voice_transport_send(transport, VOICE_WS_BINARY, frame, frame_size);
}

static int send_audio(voice_transport_t *transport, const char *session_id,
                      const uint8_t *audio, size_t audio_size)
{
  uint8_t frame[DOUBAO_CAPTURE_PACKET_BYTES + 256];
  size_t frame_size;
  int ret;

  ret = doubao_protocol_encode_audio(DOUBAO_EVENT_TASK_REQUEST, session_id,
                                     audio, audio_size, frame, sizeof(frame),
                                     &frame_size);
  if (ret < 0)
    {
      return ret;
    }
  return voice_transport_send(transport, VOICE_WS_BINARY, frame, frame_size);
}

static void build_start_session_json(char *output, size_t output_size)
{
  snprintf(output, output_size,
           "{\"asr\":{\"extra\":{\"enable_asr_twopass\":true}},"
           "\"tts\":{\"speaker\":\"%s\",\"audio_config\":{"
           "\"channel\":1,\"format\":\"pcm_s16le\",\"sample_rate\":24000}},"
           "\"dialog\":{\"bot_name\":\"Vela\","
           "\"system_role\":\"你是一个简洁友好的桌面语音助手。\","
           "\"speaking_style\":\"简洁自然\",\"extra\":{"
           "\"input_mod\":\"push_to_talk\",\"model\":\"%s\"}}}",
           g_voice.config.speaker, g_voice.config.model);
}

static void log_service_error(const doubao_packet_t *packet,
                              const voice_transport_t *transport)
{
  char payload[257];
  size_t length;
  size_t i;

  length = packet->payload_size < sizeof(payload) - 1 ?
           packet->payload_size : sizeof(payload) - 1;
  for (i = 0; i < length; i++)
    {
      uint8_t value = packet->payload[i];
      payload[i] = value >= 0x20 && value <= 0x7e ? (char)value : '.';
    }
  payload[length] = '\0';
  fprintf(stderr,
          "doubao: service error event=%d code=%d logid=%s payload=%s\n",
          packet->event, packet->error_code, voice_transport_logid(transport),
          payload);
}

static void handle_packet(const doubao_packet_t *packet,
                          const voice_transport_t *transport,
                          voice_player_t **player)
{
  char text[DOUBAO_REPLY_MAX];

  if (packet->kind == DOUBAO_PACKET_ERROR)
    {
      log_service_error(packet, transport);
      set_state(DOUBAO_VOICE_ERROR, "豆包服务返回错误，请查看日志");
      return;
    }

  if (packet->kind == DOUBAO_PACKET_AUDIO)
    {
      if (g_voice.config.tts_enabled && !*player)
        {
          if (voice_player_open(player, g_voice.config.playback_device,
                                DOUBAO_TTS_RATE, DOUBAO_TTS_CHANNELS,
                                DOUBAO_TTS_BITS) == 0)
            {
              set_state(DOUBAO_VOICE_PLAYING, NULL);
            }
        }
      if (*player)
        {
          (void)voice_player_write(*player, packet->payload,
                                   packet->payload_size);
        }
      return;
    }

  if (packet->kind != DOUBAO_PACKET_JSON)
    {
      return;
    }

  if (packet->event == DOUBAO_EVENT_ASR_RESPONSE)
    {
      if (doubao_protocol_get_text(packet->payload, packet->payload_size,
                                   "text", text, sizeof(text)))
        {
          pthread_mutex_lock(&g_voice.mutex);
          copy_text(g_voice.snapshot.user_text, sizeof(g_voice.snapshot.user_text),
                    text);
          pthread_mutex_unlock(&g_voice.mutex);
        }
    }
  else if (packet->event == DOUBAO_EVENT_CHAT_RESPONSE)
    {
      if (doubao_protocol_get_text(packet->payload, packet->payload_size,
                                   "content", text, sizeof(text)) ||
          doubao_protocol_get_text(packet->payload, packet->payload_size,
                                   "text", text, sizeof(text)))
        {
          append_reply(text);
        }
    }
}

/* ---- 建立会话：连接 + START_CONNECTION + START_SESSION + 等待确认 ----
 * 返回 0 成功并把 transport 写回 *out;<0 失败。 */
static int establish_session(voice_transport_t **out, char *session_id,
                             size_t session_id_size)
{
  voice_transport_t *transport = NULL;
  voice_transport_config_t transport_config;
  char connect_id[48];
  char start_session_json[768];
  int ret;

  make_identifier(session_id, session_id_size, "vela-session");
  make_identifier(connect_id, sizeof(connect_id), "vela-connect");
  memset(&transport_config, 0, sizeof(transport_config));
  transport_config.url = DOUBAO_WS_URL;
  transport_config.app_id = g_voice.config.app_id;
  transport_config.access_token = g_voice.config.access_token;
  transport_config.resource_id = g_voice.config.resource_id;
  transport_config.app_key = g_voice.config.app_key;
  transport_config.connect_id = connect_id;

  ret = voice_transport_connect(&transport, &transport_config,
                                DOUBAO_CONNECT_TIMEOUT_MS);
  if (ret < 0)
    {
      return ret;
    }
  printf("doubao: connected, logid=%s\n", voice_transport_logid(transport));

  build_start_session_json(start_session_json, sizeof(start_session_json));

  if (send_json(transport, DOUBAO_EVENT_START_CONNECTION, session_id, "{}") < 0 ||
      send_json(transport, DOUBAO_EVENT_START_SESSION, session_id,
                start_session_json) < 0)
    {
      voice_transport_close(transport);
      return -EIO;
    }

  /* Read back CONNECTION_STARTED + SESSION_STARTED events.  If we skip this,
   * the server's replies pile up in the TLS buffer and subsequent
   * mbedtls_ssl_write calls return WANT_READ, which the NuttX curl port
   * maps to CURLE_SEND_ERROR(55). */
  {
    uint8_t init_data[DOUBAO_WS_BUFFER_SIZE];
    voice_ws_opcode_t init_op;
    doubao_packet_t init_pkt;
    bool conn_ok = false, sess_ok = false;
    int init_tries = 0;

    while ((!conn_ok || !sess_ok) && init_tries < 50 && !is_shutdown())
      {
        ret = voice_transport_receive(transport, &init_op, init_data,
                                      sizeof(init_data), 5000);
        if (ret <= 0 || init_op != VOICE_WS_BINARY)
          {
            usleep(50000); init_tries++; continue;
          }
        if (doubao_protocol_decode(init_data, (size_t)ret, &init_pkt) < 0)
          {
            usleep(50000); init_tries++; continue;
          }
        if (init_pkt.event == DOUBAO_EVENT_CONNECTION_STARTED) conn_ok = true;
        if (init_pkt.event == DOUBAO_EVENT_SESSION_STARTED ||
            init_pkt.event == 150) sess_ok = true;
        init_tries++;
      }
    if (!sess_ok)
      {
        voice_transport_close(transport);
        return -EIO;
      }
  }
  printf("doubao: session started\n");
  *out = transport;
  return 0;
}

/* ---- 空闲等待：服务 ping 保活，直到按下或掉线或退出 ----
 * 返回 0 表示按下(需发起一轮)，<0 表示连接失效需重连。 */
static int idle_wait_for_press(voice_transport_t *transport)
{
  uint8_t buf[DOUBAO_WS_BUFFER_SIZE];
  voice_ws_opcode_t op;

  while (!is_shutdown())
    {
      if (is_pressed())
        {
          return 0;
        }
      /* 短超时 receive：既服务 ping/pong，又能检测掉线；无数据即返回 0。
       * transport 的 receive 内部把 CLOSE 之外的错误折叠为 0,故仅 CLOSE
       * (返回负)触发重连。 */
      int ret = voice_transport_receive(transport, &op, buf,
                                        sizeof(buf), 200);
      if (ret < 0)
        {
          DVLOG("doubao: idle disconnect ret=%d", ret);
          return ret;   /* 掉线 → 触发重连 */
        }
      if (ret > 0)
        {
          /* 空闲期收到帧:可能是服务端下发的会话超时/错误提示。记录事件号
           * 以定位频繁断连原因(正常 idle 不应收到业务帧)。 */
          doubao_packet_t pkt;
          if (doubao_protocol_decode(buf, (size_t)ret, &pkt) == 0)
            DVLOG("doubao: idle frame kind=%d event=%d", pkt.kind, pkt.event);
        }
    }
  return 0;
}

/* ---- 执行一轮对话：录音 → END_ASR → 收 ASR/TTS/Chat ----
 * 返回 0 成功(连接仍可用);<0 连接失效需重连。 */
static int run_turn(voice_transport_t *transport, const char *session_id)
{
  voice_capture_t *capture = NULL;
  voice_player_t *player = NULL;
  uint8_t audio[DOUBAO_CAPTURE_PACKET_BYTES];
  uint8_t received[DOUBAO_WS_BUFFER_SIZE];
  voice_ws_opcode_t opcode;
  doubao_packet_t packet;
  bool tts_ended = false;
  bool chat_ended = false;
  bool asr_seen = false;
  int ret;

  begin_turn_snapshot();
  DVLOG("doubao: run_turn ENTER");

  /* 先切 RECORDING 再开麦克风:voice_capture_open 耗时约 100ms,若此间 UI
   * 仍显示 IDLE,refresh 可能因某帧 wifi 抖动/状态判定把按钮 add DISABLED,
   * 而对"正被按住"的按钮置 DISABLED 会让 LVGL 立即发 PRESS_LOST →
   * action_release_cb → doubao_voice_stop 清 press。于是 worker 跑到录音
   * 循环时 press 已为 false,一个包都没录(sent_pkts=0),表现为按了没反应、
   * 卡在"等待回复超时"。提前进 RECORDING 态可消除这段 IDLE 窗口。 */
  set_state(DOUBAO_VOICE_RECORDING, NULL);

  ret = voice_capture_open(&capture, g_voice.config.capture_device,
                           DOUBAO_CAPTURE_RATE, DOUBAO_CAPTURE_CHANNELS,
                           DOUBAO_CAPTURE_BITS);
  if (ret < 0)
    {
      set_state(DOUBAO_VOICE_ERROR, "麦克风不可用");
      return 0;   /* 连接仍有效,不重连 */
    }

  /* 录音循环 — 从点击开始到再次点击结束期间持续上传(点击切换模式) */
  int sent_pkts = 0;
  int eagain_cnt = 0;
  while (is_pressed() && !is_shutdown())
    {
      /* 安全上限:每包约 20ms,1500 包≈30s。即便结束点击意外丢失(press
       * 未清),也不会永久卡在录音、UI 一直显示"正在录音"。 */
      if (sent_pkts >= 1500)
        {
          DVLOG("doubao: rec loop MAX reached, force stop");
          break;
        }
      ret = voice_capture_read_packet(capture, audio, sizeof(audio));
      if (ret == -EAGAIN)
        {
          eagain_cnt++;
          continue;
        }
      if (ret != (int)sizeof(audio))
        {
          voice_capture_close(capture);
          set_state(DOUBAO_VOICE_ERROR, "语音采集异常");
          return 0;
        }
      if (send_audio(transport, session_id, audio, sizeof(audio)) < 0)
        {
          voice_capture_close(capture);
          return -EIO;   /* 发送失败 → 重连 */
        }
      sent_pkts++;
    }
  DVLOG("doubao: recording done, sent_pkts=%d eagain=%d", sent_pkts, eagain_cnt);
  voice_capture_close(capture);

  if (is_shutdown())
    {
      return 0;
    }

  /* 录音过短(触摸抖动把一次点击弹成 start→立刻 stop,或误触):不发
   * END_ASR。豆包对空/极短音频不会给完整回复,若照常发 END_ASR 会白等
   * 到超时(rt recv timeout),既慢又占用连接。直接干净收尾回到 idle,
   * 连接仍有效不重连。20 包≈0.4s 为下限。 */
  if (sent_pkts < 20)
    {
      DVLOG("doubao: recording too short (%d pkts), abort turn", sent_pkts);
      set_state(DOUBAO_VOICE_IDLE, NULL);
      return 0;
    }

  set_state(DOUBAO_VOICE_WAITING_RESPONSE, NULL);

  if (send_json(transport, DOUBAO_EVENT_END_ASR, session_id, "{}") < 0)
    {
      return -EIO;
    }
  DVLOG("doubao: END_ASR sent, waiting response");

  /* 接收 ASR + TTS + Chat 回复。
   * 持久连接下,上一轮的 TTS 音频(event 350 等)可能被服务端慢速推送、
   * 渗到本轮 END_ASR 之后;若当本轮内容处理,会先花时间收完旧音频才轮到
   * 本轮 ASR,表现为"一直等待回复"。故设门控:在见到本轮 ASR(ASR_START
   * 450 / ASR_RESPONSE 451)之前,丢弃所有帧(尤其旧 TTS 音频)。 */
  int empty_polls = 0;
  while (!chat_ended && !is_shutdown())
    {
      ret = voice_transport_receive(transport, &opcode, received,
                                    sizeof(received), 5000);
      if (ret < 0)
        {
          DVLOG("doubao: run_turn recv ret<0 (%d) -> reconnect", ret);
          if (player) voice_player_close(player);
          return -EIO;   /* 掉线 → 重连 */
        }
      if (ret == 0)
        {
          /* ret==0 可能是:PING 被处理(即时返回)、瞬时无数据、或真超时。
           * 只以 chat_ended(559,文字回复完)为收尾标志:TTS_ENDED(459) 常
           * 先于 chat 回复(550)到达,若一见 tts_ended 就 break 会把随后的
           * 550 漏进 idle → 该轮无文字回复。故这里不看 tts_ended,持续等到
           * chat_ended 或累计静默超时。 */
          if (++empty_polls >= 6)
            {
              DVLOG("doubao: rt recv timeout (%d empty) -> reconnect", empty_polls);
              if (player) voice_player_close(player);
              set_state(DOUBAO_VOICE_ERROR, "等待豆包回复超时");
              /* 超时的连接已不可信(可能半死),必须重连:此前 return 0 保留
               * 坏连接,下一轮复用它发 END_ASR 服务端不再响应 → 又超时,
               * 表现为"连着几次都没回复"。return -EIO 触发 worker 重连。 */
              return -EIO;
            }
          continue;
        }
      empty_polls = 0;   /* 收到有效帧,重置静默计数 */
      if (opcode != VOICE_WS_BINARY ||
          doubao_protocol_decode(received, ret, &packet) < 0)
        {
          continue;
        }

      if (!asr_seen)
        {
          if (packet.event == DOUBAO_EVENT_ASR_START ||
              packet.event == DOUBAO_EVENT_ASR_RESPONSE)
            {
              asr_seen = true;   /* 本轮回复开始,后续正常处理 */
            }
          else
            {
              continue;          /* 上一轮残留帧,丢弃 */
            }
        }

      DVLOG("doubao: rt event=%d kind=%d", packet.event, packet.kind);
      handle_packet(&packet, transport, &player);
      if (packet.kind == DOUBAO_PACKET_ERROR)
        {
          if (player) voice_player_close(player);
          return 0;   /* 服务错误但连接可能仍在 */
        }
      tts_ended |= packet.event == DOUBAO_EVENT_TTS_ENDED;
      chat_ended |= packet.event == DOUBAO_EVENT_CHAT_ENDED;
      /* 本轮结束判定:CHAT_ENDED(559) 表示文字回复已完;此时若 TTS 也已
       * 结束(或本就无 TTS)即收尾。服务端不保证 459/559 都发或先后顺序,
       * 只等 chat_ended 即可退出,避免卡在 receive 傻等 60s。 */
      if (chat_ended)
        {
          DVLOG("doubao: turn done (tts_ended=%d)", (int)tts_ended);
          break;
        }
    }

  if (player)
    {
      voice_player_close(player);
      player = NULL;
    }
  set_state(DOUBAO_VOICE_IDLE, NULL);
  return 0;
}

/* ---- 持久 worker:启动即连接,持续服务多轮对话 ---- */
static void *voice_worker(void *arg)
{
  voice_transport_t *transport = NULL;
  char session_id[48] = {0};
  bool connected = false;
  bool first_connect = true;   /* 仅首次连接显示"正在连接";每轮后的重连静默 */

  (void)arg;

  while (!is_shutdown())
    {
      if (!connected)
        {
          /* 先等 Wi-Fi 就绪(拿到 IP)再连豆包:开机时 Wi-Fi 还没连上,
           * 过早发起 TLS 会反复 Timeout(28)。
           * 豆包每轮对话(559)后会主动关连接,故 worker 需在每轮后重连。
           * 重连是常态,不应每次都闪"正在连接豆包"打断体验:仅首次连接
           * 显示 CONNECTING,后续重连保持 IDLE 静默进行。 */
          if (first_connect)
            {
              set_state(DOUBAO_VOICE_CONNECTING, NULL);
            }
          while (!wifi_status_is_connected() && !is_shutdown())
            {
              usleep(500 * 1000);
            }
          if (is_shutdown())
            {
              break;
            }

          if (establish_session(&transport, session_id,
                                sizeof(session_id)) == 0)
            {
              connected = true;
              first_connect = false;
              set_state(DOUBAO_VOICE_IDLE, NULL);
            }
          else
            {
              if (is_shutdown())
                {
                  break;
                }
              /* 首次连不上才报错;重连失败静默重试,不打断 UI */
              if (first_connect)
                {
                  set_state(DOUBAO_VOICE_ERROR, "无法连接豆包服务");
                }
              usleep(1000 * 1000);   /* 1s 后重试 */
              continue;
            }
        }

      if (idle_wait_for_press(transport) < 0)
        {
          voice_transport_close(transport);
          transport = NULL;
          connected = false;
          continue;
        }
      if (is_shutdown())
        {
          break;
        }

      if (run_turn(transport, session_id) < 0)
        {
          voice_transport_close(transport);
          transport = NULL;
          connected = false;
        }
    }

  if (transport)
    {
      (void)send_json(transport, DOUBAO_EVENT_FINISH_SESSION, session_id, "{}");
      (void)send_json(transport, DOUBAO_EVENT_FINISH_CONNECTION, session_id, "{}");
      voice_transport_close(transport);
    }

  pthread_mutex_lock(&g_voice.mutex);
  g_voice.worker_running = false;
  pthread_mutex_unlock(&g_voice.mutex);
  return NULL;
}

int doubao_voice_init(void)
{
  int ret;

  memset(&g_voice, 0, sizeof(g_voice));
  pthread_mutex_init(&g_voice.mutex, NULL);

  g_voice.initialized = true;
#if DOUBAO_SECRETS_AVAILABLE
  g_voice.config.app_id = DOUBAO_APP_ID;
  g_voice.config.access_token = DOUBAO_ACCESS_TOKEN;
  g_voice.config.resource_id = DOUBAO_RESOURCE_ID;
  g_voice.config.app_key = DOUBAO_APP_KEY;
  g_voice.config.model = DOUBAO_MODEL;
  g_voice.config.speaker = DOUBAO_SPEAKER;
  g_voice.config.capture_device = CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_CAPTURE_DEV;
  g_voice.config.playback_device = CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_PLAYBACK_DEV;
  g_voice.config.tts_enabled = 1;
  g_voice.snapshot.state = DOUBAO_VOICE_CONNECTING;
#else
  g_voice.snapshot.state = DOUBAO_VOICE_UNCONFIGURED;
  return 0;
#endif

  if (!doubao_voice_is_configured())
    {
      g_voice.snapshot.state = DOUBAO_VOICE_UNCONFIGURED;
      return 0;
    }

  /* 应用启动即拉起持久 worker,后台连接豆包并常驻等待按键 */
  {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    /* 栈 256KB:run_turn 内有 received[8192] 等大缓冲,并会经 handle_packet
     * 走到 nxaudio 播放路径,64KB 偏紧。放大避免深调用栈溢出崩溃。 */
    pthread_attr_setstacksize(&attr, 262144);
    g_voice.worker_running = true;
    ret = pthread_create(&g_voice.worker, &attr, voice_worker, NULL);
    pthread_attr_destroy(&attr);
  }
  if (ret != 0)
    {
      g_voice.worker_running = false;
      set_state(DOUBAO_VOICE_ERROR, "worker 启动失败");
      return -ret;
    }
  return 0;
}

void doubao_voice_deinit(void)
{
  bool join = false;

  if (!g_voice.initialized)
    {
      return;
    }

  pthread_mutex_lock(&g_voice.mutex);
  g_voice.shutdown = true;
  g_voice.press = false;
  join = g_voice.worker_running;
  pthread_mutex_unlock(&g_voice.mutex);

  if (join)
    {
      (void)pthread_join(g_voice.worker, NULL);
    }
  pthread_mutex_destroy(&g_voice.mutex);
  memset(&g_voice, 0, sizeof(g_voice));
}

int doubao_voice_start(void)
{
  if (!g_voice.initialized || !doubao_voice_is_configured())
    {
      return -ENOKEY;
    }
  pthread_mutex_lock(&g_voice.mutex);
  g_voice.press = true;
  pthread_mutex_unlock(&g_voice.mutex);
  return 0;
}

int doubao_voice_stop(void)
{
  if (!g_voice.initialized)
    {
      return -EINVAL;
    }
  pthread_mutex_lock(&g_voice.mutex);
  g_voice.press = false;
  pthread_mutex_unlock(&g_voice.mutex);
  return 0;
}

int doubao_voice_reset(void)
{
  if (!g_voice.initialized)
    {
      return -EINVAL;
    }
  pthread_mutex_lock(&g_voice.mutex);
  g_voice.snapshot.user_text[0] = '\0';
  g_voice.snapshot.assistant_text[0] = '\0';
  g_voice.snapshot.error_text[0] = '\0';
  pthread_mutex_unlock(&g_voice.mutex);
  return 0;
}

bool doubao_voice_is_configured(void)
{
#if DOUBAO_SECRETS_AVAILABLE
  return g_voice.config.app_id && g_voice.config.app_id[0] &&
         g_voice.config.access_token && g_voice.config.access_token[0] &&
         strcmp(g_voice.config.app_id, "replace-with-your-app-id") != 0;
#else
  return false;
#endif
}

void doubao_voice_get_snapshot(doubao_voice_snapshot_t *snapshot)
{
  if (!snapshot)
    {
      return;
    }
  pthread_mutex_lock(&g_voice.mutex);
  *snapshot = g_voice.snapshot;
  pthread_mutex_unlock(&g_voice.mutex);
}

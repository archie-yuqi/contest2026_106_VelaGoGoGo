/****************************************************************************
 * app/home_scense/doubao/doubao_protocol.h
 * Doubao RealtimeAPI binary envelope and JSON event helpers.
 ****************************************************************************/

#ifndef HOME_SCENSE_DOUBAO_PROTOCOL_H
#define HOME_SCENSE_DOUBAO_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DOUBAO_EVENT_START_CONNECTION  1
#define DOUBAO_EVENT_FINISH_CONNECTION 2
#define DOUBAO_EVENT_START_SESSION     100
#define DOUBAO_EVENT_FINISH_SESSION    102
#define DOUBAO_EVENT_TASK_REQUEST      200
#define DOUBAO_EVENT_END_ASR           400
/* 纯文本 query:直接用文字发起对话(不走音频输入),服务端回文字(550)+
 * TTS 音频(352)。payload: {"content":"<问句>"}。用于程序化文字问答。 */
#define DOUBAO_EVENT_CHAT_TEXT_QUERY   501
#define DOUBAO_EVENT_CONVERSATION_RETRIEVE 512

#define DOUBAO_EVENT_CONNECTION_STARTED 50
#define DOUBAO_EVENT_SESSION_STARTED    150
#define DOUBAO_EVENT_SESSION_FINISHED   152
#define DOUBAO_EVENT_ASR_START         450
#define DOUBAO_EVENT_ASR_RESPONSE       451
#define DOUBAO_EVENT_ASR_ENDED          459
#define DOUBAO_EVENT_TTS_RESPONSE       352
#define DOUBAO_EVENT_TTS_ENDED          359
#define DOUBAO_EVENT_CHAT_RESPONSE      550
#define DOUBAO_EVENT_CHAT_ENDED         559
#define DOUBAO_EVENT_CONVERSATION_RETRIEVED 569

typedef enum doubao_packet_kind_e
{
  DOUBAO_PACKET_JSON,
  DOUBAO_PACKET_AUDIO,
  DOUBAO_PACKET_ERROR,
} doubao_packet_kind_t;

typedef struct doubao_packet_s
{
  doubao_packet_kind_t kind;
  int event;
  int error_code;
  int32_t sequence;
  const uint8_t *payload;
  size_t payload_size;
} doubao_packet_t;

int doubao_protocol_encode_json(int event, const char *session_id,
                                const char *json, uint8_t *output,
                                size_t capacity, size_t *output_size);
int doubao_protocol_encode_audio(int event, const char *session_id,
                                 const uint8_t *audio, size_t audio_size,
                                 uint8_t *output, size_t capacity,
                                 size_t *output_size);
int doubao_protocol_decode(const uint8_t *input, size_t input_size,
                           doubao_packet_t *packet);
int doubao_protocol_get_event(const uint8_t *json, size_t json_size);
bool doubao_protocol_get_text(const uint8_t *json, size_t json_size,
                              const char *key, char *output,
                              size_t output_size);

#endif /* HOME_SCENSE_DOUBAO_PROTOCOL_H */

/****************************************************************************
 * app/home_scense/doubao/wake_reply.c
 * Random wake-confirmation chime playback via nxaudio (voice_player).
 *
 * 资源为 8 段拼接的 16kHz/16bit/mono raw PCM(wake_reply_raw.bin,经
 * wake_reply_assets.S 以 .incbin 嵌入),wake_reply_assets.h 给出每段偏移/
 * 长度表。播放复用 voice_player(nxaudio):以 16kHz 打开,写入选中段,
 * close 内部 drain 播完尾音后返回,天然阻塞到播放结束。
 ****************************************************************************/

#include "wake_reply.h"

#include "voice_player.h"
#include "wake_reply_assets.h"
#include "doubao_config.h"

#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#ifndef CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_PLAYBACK_DEV
#  define CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_PLAYBACK_DEV "/dev/audio/pcm0p"
#endif

#define WAKE_REPLY_RATE     16000
#define WAKE_REPLY_CHANNELS 1
#define WAKE_REPLY_BITS     16

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int g_rand_state;
static int g_last_index = -1;

static unsigned int next_random(void)
{
  struct timespec ts;
  unsigned int value;

  pthread_mutex_lock(&g_lock);
  if (g_rand_state == 0)
    {
      clock_gettime(CLOCK_MONOTONIC, &ts);
      g_rand_state = (unsigned int)ts.tv_sec ^ (unsigned int)ts.tv_nsec;
      if (g_rand_state == 0)
        {
          g_rand_state = 1;
        }
    }
  g_rand_state = g_rand_state * 1103515245u + 12345u;
  value = g_rand_state;
  pthread_mutex_unlock(&g_lock);
  return value;
}

int wake_reply_play_random(void)
{
  voice_player_t *player = NULL;
  size_t index;
  unsigned int fallback;
  const wake_reply_asset_t *asset;
  int ret;

  if (WAKE_REPLY_ASSETS_COUNT == 0)
    {
      return -ENOENT;
    }

  index = next_random() % WAKE_REPLY_ASSETS_COUNT;
  fallback = next_random();

  pthread_mutex_lock(&g_lock);
  if (WAKE_REPLY_ASSETS_COUNT > 1 && (int)index == g_last_index)
    {
      index = (index + 1 + fallback % (WAKE_REPLY_ASSETS_COUNT - 1)) %
              WAKE_REPLY_ASSETS_COUNT;
    }
  g_last_index = (int)index;
  pthread_mutex_unlock(&g_lock);

  asset = &wake_reply_assets[index];
  if (asset->size == 0 || (asset->size & 1) != 0)
    {
      return -EINVAL;
    }

  ret = voice_player_open(&player,
                          CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_PLAYBACK_DEV,
                          WAKE_REPLY_RATE, WAKE_REPLY_CHANNELS, WAKE_REPLY_BITS);
  if (ret < 0)
    {
      DOUBAO_LOG("wake reply open failed: %d", ret);
      return ret;
    }

  ret = voice_player_write(player, wake_reply_raw_data + asset->offset,
                           asset->size);
  /* close 内部 drain 播完尾音再返回 → 阻塞到播放结束。 */
  voice_player_close(player);
  DOUBAO_LOG("wake reply asset=%zu size=%zu ret=%d", index + 1, asset->size, ret);
  return ret;
}

/****************************************************************************
 * app/home_scense/doubao/voice_player.c
 * Streaming PCM playback backed by NuttX nxaudio.
 ****************************************************************************/

#include "voice_player.h"

#include <nuttx/audio/audio.h>
#include <audioutils/nxaudio.h>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

struct voice_player_s
{
  struct nxaudio_s audio;
  int read_fd;
  int write_fd;
  pthread_t thread;
  volatile int running;
  volatile int aborted;   /* abort 请求:dequeue 回调停止回填,尽快静音 */
  int audio_initialized;
};

static void player_dequeue(unsigned long arg, struct ap_buffer_s *buffer)
{
  voice_player_t *player = (voice_player_t *)(uintptr_t)arg;
  ssize_t received;

  if (!player || !buffer)
    {
      return;
    }

  /* abort 后不再回填:直接交回空缓冲,让播放尽快停下(不再把在途 TTS
   * 灌进声卡)。 */
  if (player->aborted)
    {
      buffer->nbytes = 0;
      return;
    }

  received = read(player->read_fd, buffer->samp, buffer->nmaxbytes);
  if (received <= 0)
    {
      buffer->nbytes = 0;
      return;
    }

  buffer->nbytes = received;
  buffer->curbyte = 0;
  buffer->flags = 0;
  (void)nxaudio_enqbuffer(&player->audio, buffer);
}

static void player_complete(unsigned long arg)
{
  voice_player_t *player = (voice_player_t *)(uintptr_t)arg;
  if (player)
    {
      player->running = 0;
    }
}

static void player_user(unsigned long arg, struct audio_msg_s *message,
                        bool *running)
{
  (void)arg;
  (void)message;
  (void)running;
}

static void *player_thread(void *arg)
{
  voice_player_t *player = arg;
  struct nxaudio_callbacks_s callbacks =
  {
    player_dequeue,
    player_complete,
    player_user,
  };
  int i;

  for (i = 0; i < player->audio.abufnum; i++)
    {
      player_dequeue((unsigned long)(uintptr_t)player, player->audio.abufs[i]);
    }

  (void)nxaudio_start(&player->audio);
  (void)nxaudio_msgloop(&player->audio, &callbacks,
                        (unsigned long)(uintptr_t)player);
  player->running = 0;
  return NULL;
}

int voice_player_open(voice_player_t **out, const char *device,
                      uint32_t sample_rate, uint8_t channels,
                      uint8_t bits_per_sample)
{
  voice_player_t *player;
  int fds[2];
  int ret;

  if (!out || !device)
    {
      return -EINVAL;
    }

  player = calloc(1, sizeof(*player));
  if (!player)
    {
      return -ENOMEM;
    }
  player->read_fd = -1;
  player->write_fd = -1;

  if (pipe(fds) < 0)
    {
      free(player);
      return -errno;
    }
  player->read_fd = fds[0];
  player->write_fd = fds[1];

  ret = init_nxaudio_devname(&player->audio, sample_rate, bits_per_sample,
                             channels, device, "/doubao_player");
  if (ret < 0)
    {
      voice_player_close(player);
      return ret;
    }
  player->audio_initialized = 1;

  player->running = 1;
  ret = pthread_create(&player->thread, NULL, player_thread, player);
  if (ret != 0)
    {
      voice_player_close(player);
      return -ret;
    }

  *out = player;
  return 0;
}

int voice_player_write(voice_player_t *player, const uint8_t *data,
                       size_t size)
{
  size_t written = 0;

  if (!player || !data)
    {
      return -EINVAL;
    }

  while (written < size)
    {
      ssize_t ret = write(player->write_fd, data + written, size - written);
      if (ret <= 0)
        {
          return -errno;
        }
      written += ret;
    }

  return 0;
}

void voice_player_close(voice_player_t *player)
{
  if (!player)
    {
      return;
    }
  if (player->write_fd >= 0)
    {
      close(player->write_fd);
      player->write_fd = -1;
    }
  /* 必须无条件发 AUDIO_MSG_STOP:nxaudio_msgloop 只在收到 STOP 时才退出,
   * 播放自然结束(AUDIO_MSG_COMPLETE)只把回调里的 running 置 0,并不会让
   * msgloop 跳出——它会继续阻塞在 mq_receive。若因 running==0 而跳过
   * nxaudio_stop,后面的 pthread_join 会永久等待,worker 线程卡在
   * "Waiting MQ empty",整个语音功能死锁。故这里始终 stop。 */
  if (player->audio_initialized)
    {
      (void)nxaudio_stop(&player->audio);
    }
  if (player->thread)
    {
      (void)pthread_join(player->thread, NULL);
    }
  if (player->read_fd >= 0)
    {
      close(player->read_fd);
    }
  if (player->audio_initialized)
    {
      fin_nxaudio(&player->audio);
    }
  free(player);
}

void voice_player_abort(voice_player_t *player)
{
  if (!player)
    {
      return;
    }
  /* 置 abort:dequeue 回调停止从管道回填数据,在途 TTS 被丢弃;随后走与
   * close 相同的停止/回收流程(nxaudio_stop 立即停,不 drain 尾音)。 */
  player->aborted = 1;
  voice_player_close(player);
}

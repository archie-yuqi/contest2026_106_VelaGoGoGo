/****************************************************************************
 * app/home_scense/doubao/wake_reply.h
 * Random wake-confirmation chime playback (16kHz/16bit/mono PCM assets).
 ****************************************************************************/

#ifndef HOME_SCENSE_WAKE_REPLY_H
#define HOME_SCENSE_WAKE_REPLY_H

/* 随机播放一个唤醒确认短音(避免连续重复)。返回 0 成功,<0 失败。
 * 阻塞到播放完成。供唤醒收音前的确认提示使用。 */
int wake_reply_play_random(void);

#endif /* HOME_SCENSE_WAKE_REPLY_H */

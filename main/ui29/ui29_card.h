/*
 * ui29_card.h — 2.9" (128x296) 单词卡界面
 *
 * 状态：卡片 / 首页 / 同步页；键：PREV、NEXT、PLAY、REC、SYNC。
 * 局刷用整屏局刷（约 468 ms），每 20 次插一次全刷去残影。
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UI29_CARD_H
#define UI29_CARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* 进入单词卡应用，不返回。需要 storage/audio/buttons 已初始化。 */
void ui29_run(void);

/* 现场调方向（不用重烧）：bit0 = 上下翻，bit1 = 左右翻。会触发一次全刷。 */
void ui29_set_flip(int flip);

#ifdef __cplusplus
}
#endif
#endif /* UI29_CARD_H */

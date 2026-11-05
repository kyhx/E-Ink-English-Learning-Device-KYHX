/*
 * panel1680_test.h — 2.9" SSD1680 (ZJY128296-029EAAMFGN) 上电自检
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PANEL1680_TEST_H
#define PANEL1680_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/* 跑完整自检序列并在 UART0 打印结果行；不返回值的阻塞时间约 10 s。 */
void panel1680_test_run(void);

#ifdef __cplusplus
}
#endif
#endif /* PANEL1680_TEST_H */

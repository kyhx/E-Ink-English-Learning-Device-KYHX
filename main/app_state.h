/*
 * app_state.h — M2 application skeleton (record/play UI).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef APP_STATE_H
#define APP_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Show the M2 record/play screen and run the key-event loop.  Does not return. */
void app_state_run(void);

#ifdef __cplusplus
}
#endif
#endif /* APP_STATE_H */

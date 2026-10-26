/*
 * collect.h — device-side data collection mode.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef COLLECT_H
#define COLLECT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Run the collection UI and key loop.  Does not return. */
void collect_run(void);

/* Serial/PC hooks. */
void collect_request_record(void);
bool collect_set_state(int speaker, int word, int label);
void collect_info(void);
void collect_list(void);
void collect_csv(void);
bool collect_delete(const char *path);
const char *collect_last_path(void);
const char *collect_last_utt(void);

#ifdef __cplusplus
}
#endif
#endif /* COLLECT_H */

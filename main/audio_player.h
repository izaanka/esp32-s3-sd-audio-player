#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The main audio playback loop, runs as a FreeRTOS task.
 * @param arg Task arguments (unused)
 */
void audio_task(void *arg);

#ifdef __cplusplus
}
#endif

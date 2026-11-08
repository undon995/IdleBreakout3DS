#ifndef IDLE_AUDIO_DATA_H
#define IDLE_AUDIO_DATA_H
#include <stdint.h>
typedef struct {const int16_t* p; unsigned samples;} Sample;
extern const Sample samples[12];
#endif

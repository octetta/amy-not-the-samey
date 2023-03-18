#ifndef __PCM_USER_H
#define __PCM_USER_H

#define PCM_SAMPLES (67)
#define PCM_USER_SAMPLES (5)
#define PCM_LENGTH (1176036)
#define PCM_USER_LENGTH (4*44100)
//#define PCM_USER_LENGTH (4*44100*2)

typedef struct {
    uint32_t offset;
    uint32_t length;
    uint32_t loopstart;
    uint32_t loopend;
    uint8_t midinote;
} pcm_map_t;

extern int pcm_sample_max;
extern pcm_map_t pcm_map[];
extern int16_t pcm[];

#endif


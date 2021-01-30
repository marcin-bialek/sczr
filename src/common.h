/* Systemy Czasu Rzeczywistego
 *
 * Authors: 
 *   Piotr Sawicki (https://github.com/psawicki0)
 *   Marcin Białek (https://github.com/marcin-bialek)
 */


#pragma once
#include <inttypes.h>
#include <stddef.h>
#include <mqueue.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

#define BUFFERS_IN_MEM 16
#define START_MARKER 420

#define Fs 44100
#define FILTERS 31
#define PCM_BLOCKING 0
#define RESAMPLE 1
#define BUFFER_TIME 300000
#define PERIOD_TIME 100000
#define CAPTURE_DEVICE_NAME "hw:1,0"
#define CAPTURE_CHANNELS 1
#define PLAYBACK_DEVICE_NAME "hw:1,0"
#define PLAYBACK_CHANNELS 2

#define MQ_MAX_MSG 10
#define MQ_MAX_MSG_SIZE 4
#define MQ_INPUT "/input"
#define MQ_OUTPUT "/output"
#define MQ_SETTINGS "/settings"
#define MQ_SETTINGS_MAX_MSG_SIZE 8
#define MQ_MSG_PRIO 0

#define SHM_INPUT "/sczr_input"
#define SHM_OUTPUT "/sczr_output"

#define LOG_CAPTURE "/tmp/log-capture"
#define LOG_FILTER "/tmp/log-filter"
#define LOG_PLAYBACK "/tmp/log-playback"


typedef struct {
    const char* name;
    int size;
    int semaphores_id;
    struct {
        unsigned int block_id;
        ptrdiff_t offset;
    } buffers[BUFFERS_IN_MEM];
    pthread_barrier_t barrier;
} sczr_shared_t;


typedef struct {
    const char* name;
    snd_pcm_t* handle;
    snd_pcm_stream_t stream;
    snd_pcm_access_t access;
    snd_pcm_format_t format;
    unsigned int rate;
    unsigned int channels;
    unsigned int buffer_time;
    unsigned int period_time;
    snd_pcm_sframes_t buffer_size; 
    snd_pcm_sframes_t period_size;
} pcm_device_t;


int make_queue(const char* name, mqd_t* desc);
int open_queue(const char* name, mqd_t* desc);
int wait_for_marker(mqd_t queue);
int make_shared(const char* name, size_t buffer_size, sczr_shared_t** shared);
int open_shared(const char* name, size_t buffer_size, sczr_shared_t** shared);
int destroy_shared(sczr_shared_t* shared);
int close_shared(sczr_shared_t* shared);
void semaphore_wait(int set_id, int index);
void semaphore_signal(int set_id, int index);
int init_pcm_device(pcm_device_t* device);
long long time_us(void);
int set_sched(void);

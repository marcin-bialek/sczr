/* Systemy Czasu Rzeczywistego
 *
 * Authors: 
 *   Piotr Sawicki (https://github.com/psawicki0)
 *   Marcin Białek (https://github.com/marcin-bialek)
 */


#include <alsa/asoundlib.h>
#include <mqueue.h>
#include <stdlib.h>
#include <stdio.h>
#include "common.h"


int main(int argc, char* argv[]) {
    mqd_t mq_output;
    sczr_shared_t* shm_output;
    FILE* log;
    long long (*times)[2] = NULL;
    int times_i = 0;
    
    pcm_device_t device = {
        .name = PLAYBACK_DEVICE_NAME,
        .stream = SND_PCM_STREAM_PLAYBACK,
        .access = SND_PCM_ACCESS_RW_INTERLEAVED,
        .format = SND_PCM_FORMAT_S16_LE,
        .rate = Fs,
        .channels = PLAYBACK_CHANNELS,
        .buffer_time = BUFFER_TIME,
        .period_time = PERIOD_TIME
    };

    if(init_pcm_device(&device) < 0) {
        fprintf(stderr, "[playback] could not init playback device\n");
        return 1;
    }

    if(argc > 2 && !strcmp(argv[2], "--sched")) {
        if(set_sched() != 0) {
            fprintf(stderr, "[playback] could not change process priority\n");
            return 1;
        }
    }

    if(open_queue(MQ_OUTPUT, &mq_output) != 0) {
        snd_pcm_close(device.handle);
        fprintf(stderr, "[playback] could not open output queue\n");
        return 1;
    }

    if(wait_for_marker(mq_output) != 0) {
        mq_close(mq_output);
        snd_pcm_close(device.handle);
        fprintf(stderr, "[playback] marker timeout\n");
        return 1;
    }

    int input_period_size;
    mq_receive(mq_output, (char*)&input_period_size, MQ_MAX_MSG_SIZE, NULL);

    if(input_period_size != device.period_size) {
        mq_close(mq_output);
        snd_pcm_close(device.handle);
        fprintf(stderr, "[playback] input_period_size != output_period_size\n");
        return 1;
    }

    if(open_shared(SHM_OUTPUT, device.channels * device.period_size * sizeof(short), &shm_output) != 0) {
        snd_pcm_close(device.handle);
        mq_close(mq_output);
        fprintf(stderr, "[playback] could not init shared memory\n");
        return 1;
    }

    if((log = fopen(LOG_PLAYBACK, "w")) == NULL) {
        close_shared(shm_output);
        snd_pcm_close(device.handle);
        mq_close(mq_output);
        fprintf(stderr, "[playback] could not open log file\n");
        return 1;
    }

    unsigned int buffer_index = 0;
    unsigned int block_id;
    int error;
    int periods = INT32_MAX;;

    if(argc > 1) {
        periods = atoi(argv[1]);
        times = malloc(periods * 2 * sizeof(long long));
    }

    pthread_barrier_wait(&shm_output->barrier);
    usleep(200000);
    printf("[playback] running\n");

    while(periods > 0) {
        mq_receive(mq_output, (char*) &buffer_index, MQ_MAX_MSG_SIZE, NULL);
        block_id = shm_output->buffers[buffer_index].block_id;
        int16_t* buffer_addr = (void*)shm_output + shm_output->buffers[buffer_index].offset;

        if(times) {
            times[times_i][0] = block_id;
            times[times_i][1] = time_us();
            times_i++;
        }

        semaphore_wait(shm_output->semaphores_id, buffer_index);

        if((error = snd_pcm_writei(device.handle, buffer_addr, device.period_size)) < 0) {
            fprintf(stderr, "[playback] ");
            snd_pcm_recover(device.handle, error, 0);
        }

        semaphore_signal(shm_output->semaphores_id, buffer_index);
        periods--;
    }

    if(times) {
        for(int i = 0; i < times_i; i++) {
            fprintf(log, "%lld %lld\n", times[i][0], times[i][1]);
            fflush(log);
        }

        free(times);
    }

    destroy_shared(shm_output);
    mq_close(mq_output);
    snd_pcm_drain(device.handle);
    snd_pcm_close(device.handle);
    fflush(log);
    fclose(log);
    return 0;
}

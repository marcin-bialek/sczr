/* Systemy Czasu Rzeczywistego
 *
 * Authors: 
 *   Piotr Sawicki (https://github.com/psawicki0)
 *   Marcin Białek (https://github.com/marcin-bialek)
 */


#include <mqueue.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <math.h>
#include "common.h"


static float freq[FILTERS] = { 
    20, 25, 32, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 
    1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000 
};
static float b0[FILTERS], a1[FILTERS], sin_omega[FILTERS];
static pthread_mutex_t settings_mutex;
static float gains[FILTERS];


typedef struct {
    int filter_id;
    int size;
    int16_t* input;
    int16_t* output;
} FilterWorkerArgs;


static float from_db(float gain) {
    return pow(10, gain / 20);
}


static void* filter_worker(FilterWorkerArgs* args) {
    float alpha = sin_omega[args->filter_id] / 2 * from_db(-gains[args->filter_id]);

    float a0 = 1 + alpha;
    float a2 = 1 - alpha;

    float x0 = b0[args->filter_id] / a0;
    float x1 = 0;
    float x2 = -x0;
    float y1 = a1[args->filter_id] / a0;
    float y2 = a2 / a0;

    for(int i = 0; i < args->size; ++i) {
        args->output[i] = x0 * args->input[i];

        if(i > 0) {
            args->output[i] += x1 * args->input[i - 1] - y1 * args->input[i - 1];
        }
        if(i > 1) {
            args->output[i] += x2 * args->input[i - 2] - y2 * args->input[i - 2];
        }
    }
}


static void* settings_loop(void* arg) {
    mqd_t mq_settings;
    int buf[2];
    
    if(open_queue(MQ_SETTINGS, &mq_settings) != 0) {
        fprintf(stderr, "[filter] could not open settings queue\n");
        exit(1);
    }

    while(1) {
        mq_receive(mq_settings, (char*)buf, MQ_SETTINGS_MAX_MSG_SIZE, NULL);

        if(buf[0] >= 0 && buf[0] < FILTERS && gains[buf[0]] != buf[1]) {
            printf("[filter] received setting: set gain %d for freq %d\n", buf[1], buf[0]);

            pthread_mutex_lock(&settings_mutex);
            gains[buf[0]] = buf[1];
            pthread_mutex_unlock(&settings_mutex);
        }
    }
}


int main(int argc, char* argv[]) {
    mqd_t mq_settings;
    mqd_t mq_input;
    mqd_t mq_output;
    sczr_shared_t* shm_input;
    sczr_shared_t* shm_output;
    FILE* log;
    long long (*times)[2] = NULL;
    int times_i = 0;

    if(argc > 2 && !strcmp(argv[2], "--sched")) {   
        if(set_sched() != 0) {
            fprintf(stderr, "[filter] could not change process priority\n");
            return 1;
        }
    }

    // Open connection to capture process

    if(open_queue(MQ_INPUT, &mq_input) != 0) {
        fprintf(stderr, "[filter] could not open input queue\n");
        return 1;
    }

    if(wait_for_marker(mq_input) != 0) {
        mq_close(mq_input);
        fprintf(stderr, "[filter] marker timeout\n");
        return 1;
    }

    int input_period_size;
    mq_receive(mq_input, (char*)&input_period_size, MQ_MAX_MSG_SIZE, NULL);

    if(open_shared(SHM_INPUT, CAPTURE_CHANNELS * input_period_size * sizeof(short), &shm_input) != 0) {
        mq_close(mq_input);
        fprintf(stderr, "[filter] could not open input shared memory\n");
        return 1;
    }

    // Open connection to playback process

    if(make_queue(MQ_OUTPUT, &mq_output) != 0) {
        close_shared(shm_input);
        mq_close(mq_input);
        fprintf(stderr, "[filter] could not open output queue\n");
        return 1;
    }

    const int start_marker = START_MARKER;
    mq_send(mq_output, (char*)&start_marker, MQ_MAX_MSG_SIZE, MQ_MSG_PRIO);
    mq_send(mq_output, (char*)&input_period_size, MQ_MAX_MSG_SIZE, MQ_MSG_PRIO);

    if(make_shared(SHM_OUTPUT, PLAYBACK_CHANNELS * input_period_size * sizeof(short), &shm_output) != 0) {
        close_shared(shm_input);
        mq_close(mq_input);
        mq_close(mq_output);
        fprintf(stderr, "[filter] could not make output shared memory\n");
        return 1;
    }

    if((log = fopen(LOG_FILTER, "w")) == NULL) {
        destroy_shared(shm_output);
        close_shared(shm_input);
        mq_close(mq_input);
        mq_close(mq_output);
        fprintf(stderr, "[filter] could not open log file\n");
        return 1;
    }

    pthread_mutex_init(&settings_mutex, NULL);
    pthread_t settings_thread;
    pthread_create(&settings_thread, NULL, settings_loop, NULL);
    pthread_t filters_threads[FILTERS];
    FilterWorkerArgs* filters_args = malloc(FILTERS * sizeof(FilterWorkerArgs));
    int16_t* filters_output = malloc(input_period_size * sizeof(int16_t));

    for(int i = 0; i < FILTERS; i++) {
        gains[i] = 0;
        float omega = 2 * M_PI * freq[i] / Fs;
        sin_omega[i] = sin(omega);
        b0[i] = sin_omega[i] / 2;
        a1[i] = -2 * cos(omega);

        filters_args[i].filter_id = i;
        filters_args[i].size = input_period_size;
        filters_args[i].output = malloc(input_period_size * sizeof(short));
    }

    unsigned int input_buffer_index = 0;
    unsigned int output_buffer_index = 0;
    unsigned int block_id;
    int periods = INT32_MAX;;

    if(argc > 1) {
        periods = atoi(argv[1]);
        times = malloc(periods * 2 * sizeof(long long));
    }

    pthread_barrier_wait(&shm_input->barrier);
    pthread_barrier_wait(&shm_output->barrier);
    printf("[filter] running\n");

    while(periods > 0) {
        mq_receive(mq_input, (char*) &input_buffer_index, MQ_MAX_MSG_SIZE, NULL);

        block_id = shm_input->buffers[input_buffer_index].block_id;
        shm_output->buffers[output_buffer_index].block_id = block_id;
        int16_t (*x)[CAPTURE_CHANNELS] = (void*)shm_input + shm_input->buffers[input_buffer_index].offset;
        int16_t (*y)[PLAYBACK_CHANNELS] = (void*)shm_output + shm_output->buffers[output_buffer_index].offset;

        semaphore_wait(shm_input->semaphores_id, input_buffer_index);
        pthread_mutex_lock(&settings_mutex);

        int started = 0;
        
        for(int i = 0; i < FILTERS; ++i) {
            if(gains[i] != 0) {
                filters_args[i].input = (int16_t*)x;
                pthread_create(&filters_threads[i], NULL, (void*)filter_worker, &filters_args[i]);
                started++;
            }
        }

        if(started == 0) {
            memcpy(filters_output, x, input_period_size * sizeof(int16_t));
        }
        else {
            bzero(filters_output, input_period_size * sizeof(int16_t));
        }

        for(int i = 0; i < FILTERS; ++i) {
            if(gains[i] != 0) {
                pthread_join(filters_threads[i], NULL);
                pthread_cancel(filters_threads[i]);

                for (int j = 0; j < input_period_size; ++j) {
                    filters_output[j] += filters_args[i].output[j] / started;
                }
            }
        }

        pthread_mutex_unlock(&settings_mutex);
        semaphore_signal(shm_input->semaphores_id, input_buffer_index);
        semaphore_wait(shm_output->semaphores_id, output_buffer_index);

        for(int i = 0; i < input_period_size; i++) {
            y[i][0] = filters_output[i];
            y[i][1] = filters_output[i];
        }
        
        semaphore_signal(shm_output->semaphores_id, output_buffer_index);

        if(times) {
            times[times_i][0] = block_id;
            times[times_i][1] = time_us();
            times_i++;
        }

        mq_send(mq_output, (char*)&output_buffer_index, MQ_MAX_MSG_SIZE, MQ_MSG_PRIO);
        output_buffer_index = (output_buffer_index + 1) % BUFFERS_IN_MEM;
        periods--;
    }

    for(int i = 0; i < FILTERS; i++) {
        free(filters_args[i].output);
    }

    free(filters_args);
    free(filters_output);

    if(times) {
        for(int i = 0; i < times_i; i++) {
            fprintf(log, "%lld %lld\n", times[i][0], times[i][1]);
            fflush(log);
        }

        free(times);
    }

    close_shared(shm_output);
    destroy_shared(shm_input);
    mq_close(mq_input);
    mq_close(mq_output);
    fflush(log);
    fclose(log);
    return 0;
}

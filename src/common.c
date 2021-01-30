/* Systemy Czasu Rzeczywistego
 *
 * Authors: 
 *   Piotr Sawicki (https://github.com/psawicki0)
 *   Marcin Białek (https://github.com/marcin-bialek)
 */


#include "common.h"
#include <sys/sem.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/time.h>
#include <mqueue.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <stdio.h>
#include <errno.h>


int make_queue(const char* name, mqd_t* desc) {
    struct mq_attr attr;
    attr.mq_maxmsg = MQ_MAX_MSG;
    attr.mq_msgsize = MQ_MAX_MSG_SIZE;

    if((*desc = mq_open(name, O_RDWR | O_CREAT | O_EXCL, 0777, &attr)) == -1) {
        return 1;
    }

    return 0;
}


int open_queue(const char* name, mqd_t* desc) {
    for(int i = 0;; i++) {
        if((*desc = mq_open(name, O_RDONLY)) != -1) {
            break;
        }

        if(i >= 2) {
            return 1;
        }

        sleep(1);
    }

    return 0;
}


int wait_for_marker(mqd_t queue) {
    int marker;
    mq_receive(queue, (char*) &marker, MQ_SETTINGS_MAX_MSG_SIZE, NULL);
    return marker == START_MARKER ? 0 : 1;
}


int make_shared(const char* name, size_t buffer_size, sczr_shared_t** shared) {
    int shared_fd;
    const size_t size = sizeof(sczr_shared_t) + buffer_size * BUFFERS_IN_MEM;

    if((shared_fd = shm_open(name, O_CREAT | O_RDWR, 0777)) == -1) {
        return 1;
    }

    if(ftruncate(shared_fd, size) == -1) {
        shm_unlink(name);
        return 2;
    }

    *shared = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shared_fd, 0);
    (*shared)->name = name;
    (*shared)->size = size;
    //close(shared_fd);

    if(*shared == NULL) {
        shm_unlink(name);
        return 3;
    }

    ptrdiff_t buffer_offset = sizeof(sczr_shared_t);

    for(int i = 0; i < BUFFERS_IN_MEM; i++) {
        (*shared)->buffers[i].offset = buffer_offset;
        buffer_offset += buffer_size;
    }

    if(((*shared)->semaphores_id = semget(IPC_PRIVATE, BUFFERS_IN_MEM, 0600 | IPC_CREAT)) == -1) {
        munmap(*shared, size);
        shm_unlink(name);
        return 4;
    }

    unsigned short* values = malloc(BUFFERS_IN_MEM * sizeof(unsigned short));
    
    for(int i = 0; i < BUFFERS_IN_MEM; i++) {
        values[i] = 1;
    }

    if(semctl((*shared)->semaphores_id, 0, SETALL, values) == -1) {
        free(values);
        munmap(*shared, size);
        shm_unlink(name);
        return 5;
    }

    pthread_barrierattr_t attr;
    pthread_barrierattr_init(&attr);
    pthread_barrierattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    if(pthread_barrier_init(&(*shared)->barrier, &attr, 2) != 0) {
        free(values);
        munmap(*shared, size);
        shm_unlink(name);
        return 6;
    }

    free(values);
    return 0;
}


int open_shared(const char* name, size_t buffer_size, sczr_shared_t** shared) {
    int shared_fd;

    if((shared_fd = shm_open(name, O_RDWR, 0777)) == -1) {
        return 1;
    }

    const size_t size = sizeof(sczr_shared_t) + buffer_size * BUFFERS_IN_MEM;
    *shared = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shared_fd, 0);
    (*shared)->name = name;
    (*shared)->size = size;

    if(*shared == NULL) {
        return 2;
    }

    return 0;
}


int destroy_shared(sczr_shared_t* shared) {
    pthread_barrier_destroy(&shared->barrier);
    munmap(shared, shared->size);
    shm_unlink(shared->name);
}


int close_shared(sczr_shared_t* shared) {
    munmap(shared, shared->size);
}


void semaphore_wait(int set_id, int index) {
    semop(set_id, &(struct sembuf){
        .sem_num = index,
        .sem_op = -1, 
        .sem_flg = 0
    }, 1);
}


void semaphore_signal(int set_id, int index) {
    semop(set_id, &(struct sembuf){
        .sem_num = index,
        .sem_op = 1, 
        .sem_flg = 0
    }, 1);
}



int init_pcm_device(pcm_device_t* device) {
    int error, dir;

    if((error = snd_pcm_open(&device->handle, device->name, device->stream, 0)) < 0) {
        return error;
    }

    snd_pcm_hw_params_t* hw_params;
    snd_pcm_hw_params_malloc(&hw_params);
    snd_pcm_hw_params_any(device->handle, hw_params);

    if((error = snd_pcm_hw_params_set_access(device->handle, hw_params, device->access)) < 0) {
        return error;
    }

    if((error = snd_pcm_hw_params_set_format(device->handle, hw_params, device->format)) < 0) {
        return error;
    }

    if((error = snd_pcm_hw_params_set_channels(device->handle, hw_params, device->channels)) < 0) {
        return error;
    }

    if((error = snd_pcm_hw_params_set_rate_near(device->handle, hw_params, &device->rate, &dir)) < 0) {
        return error;
    }

    if((error = snd_pcm_hw_params_set_buffer_time_near(device->handle, hw_params, &device->buffer_time, &dir)) < 0) {
        return error;
    }

    if((error = snd_pcm_hw_params_get_buffer_size(hw_params, &device->buffer_size)) < 0) {
        return error;
    }

    if((error = snd_pcm_hw_params_set_period_time_near(device->handle, hw_params, &device->period_time, &dir)) < 0) {
        return error;
    }

    if((error = snd_pcm_hw_params_get_period_size(hw_params, &device->period_size, &dir)) < 0) {
        return error;
    }

    if((error = snd_pcm_hw_params(device->handle, hw_params)) < 0) {
        return error;
    }

    snd_pcm_hw_params_free(hw_params);
    return 0;
}


long long time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    return (1000000LL * (long long)ts.tv_sec) + ((long long)ts.tv_nsec / 1000LL);
}


int set_sched(void) {
    struct sched_param sp;
    sp.sched_priority = sched_get_priority_max(SCHED_RR);
    return sched_setscheduler(0, SCHED_RR, &sp);
}

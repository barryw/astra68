#ifndef ASTRA_POSIX_SEMAPHORE_H
#define ASTRA_POSIX_SEMAPHORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Unnamed, process-private POSIX semaphores use Astra's counting semaphore. */
typedef struct sem_t {
    uint32_t handle;
} sem_t;

#define SEM_VALUE_MAX 32767
#define SEM_FAILED ((sem_t *)-1)

int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_destroy(sem_t *sem);
int sem_post(sem_t *sem);
int sem_wait(sem_t *sem);
int sem_trywait(sem_t *sem);

#ifdef __cplusplus
}
#endif

#endif

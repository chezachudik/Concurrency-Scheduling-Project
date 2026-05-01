#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include "helpers.h"

typedef struct {
    char filename[64];
    int  size;
} DirEntry;

#define MAX_FILES 256
static DirEntry directory[MAX_FILES];
static int dir_count=0;


typedef struct {
    int readers_active;
    int managers_waiting;
    int supervisors_waiting;
    int writer_active;

    pthread_mutex_t mutex;
    sem_t writer_sem;
    sem_t reader_sem;
    sem_t manager_sem;
    sem_t supervisor_sem;
} DirLock;

static DirLock lock;


typedef struct {
    char type;/* 'W', 'M', 'S' */
    int  id;
    char op[8];/* READ or WRITE  */
    char filename[64];
} Operation;

static Operation *ops;
static int num_ops;
static int num_workers, num_managers, num_supervisors;

void dirlock_init(DirLock *l) {
    l->readers_active=0;
    l->managers_waiting=0;
    l->supervisors_waiting=0;
    l->writer_active=0;
    pthread_mutex_init(&l->mutex, NULL);
    sem_init(&l->writer_sem, 0, 0);
    sem_init(&l->reader_sem, 0, 0);
    sem_init(&l->manager_sem, 0, 0);
    sem_init(&l->supervisor_sem, 0, 0);
}

void dirlock_destroy(DirLock *l) {
    pthread_mutex_destroy(&l->mutex);
    sem_destroy(&l->writer_sem);
    sem_destroy(&l->reader_sem);
    sem_destroy(&l->manager_sem);
    sem_destroy(&l->supervisor_sem);
}

static int readers_waiting=0;

void worker_enter(DirLock *l, int id) {
    pthread_mutex_lock(&l->mutex);
    if (l->writer_active || l->supervisors_waiting>0 || l->managers_waiting>0) {
        printf("[Worker-%d] [worker blocked: supervisor pending] waiting...\n", id);
        readers_waiting++;
        pthread_mutex_unlock(&l->mutex);
        sem_wait(&l->reader_sem);
        pthread_mutex_lock(&l->mutex);
        readers_waiting--;
    }
    l->readers_active++;
    pthread_mutex_unlock(&l->mutex);
}

/* Worker (reader) exit */
void worker_exit(DirLock *l) {
    pthread_mutex_lock(&l->mutex);
    l->readers_active--;
    if (l->readers_active== 0) {
        /* wake a supervisor first, then manager */
        if (l->supervisors_waiting>0) {
            l->supervisors_waiting--;
            l->writer_active=1;
            sem_post(&l->supervisor_sem);
        } else if (l->managers_waiting>0) {
            l->managers_waiting--;
            l->writer_active=1;
            sem_post(&l->manager_sem);
        }
    }
    pthread_mutex_unlock(&l->mutex);
}

/* Manager (writer) enter */
void manager_enter(DirLock *l, int id) {
    pthread_mutex_lock(&l->mutex);
    if (l->writer_active || l->readers_active>0 || l->supervisors_waiting>0) {
        printf("[Manager-%d] waiting for write lock\n", id);
        l->managers_waiting++;
        pthread_mutex_unlock(&l->mutex);
        sem_wait(&l->manager_sem);
        return;
    }
    l->writer_active=1;
    pthread_mutex_unlock(&l->mutex);
}

/* Manager (writer) exit */
void manager_exit(DirLock *l) {
    pthread_mutex_lock(&l->mutex);
    l->writer_active=0;
    if (l->supervisors_waiting>0) {
        l->supervisors_waiting--;
        l->writer_active=1;
        sem_post(&l->supervisor_sem);
    } else if (l->managers_waiting>0) {
        l->managers_waiting--;
        l->writer_active=1;
        sem_post(&l->manager_sem);
    } else {
        int w=readers_waiting;
        pthread_mutex_unlock(&l->mutex);
        for (int i=0; i<w; i++)
            sem_post(&l->reader_sem);
        return;
    }
    pthread_mutex_unlock(&l->mutex);
}

/* Supervisor (writer) enter */
void supervisor_enter(DirLock *l, int id) {
    pthread_mutex_lock(&l->mutex);
    if (l->writer_active || l->readers_active>0) {
        l->supervisors_waiting++;
        pthread_mutex_unlock(&l->mutex);
        sem_wait(&l->supervisor_sem);
        return;
    }
    l->writer_active=1;
    pthread_mutex_unlock(&l->mutex);
}

/* Supervisor (writer) exit */
void supervisor_exit(DirLock *l) {
    pthread_mutex_lock(&l->mutex);
    l->writer_active=0;
    if (l->supervisors_waiting>0) {
        l->supervisors_waiting--;
        l->writer_active=1;
        sem_post(&l->supervisor_sem);
    } else if (l->managers_waiting>0) {
        l->managers_waiting--;
        l->writer_active=1;
        sem_post(&l->manager_sem);
    } else {
        int w=readers_waiting;
        pthread_mutex_unlock(&l->mutex);
        for (int i=0; i<w; i++)
            sem_post(&l->reader_sem);
        return;
    }
    pthread_mutex_unlock(&l->mutex);
}

/* find a file in the directory, return its size or 0 */
int dir_find(const char *filename) {
    for (int i=0; i<dir_count; i++)
        if (strcmp(directory[i].filename, filename)== 0)
            return directory[i].size;
    return 0;
}

/* update a file size, insert if not found */
void dir_update(const char *filename, int new_size) {
    for (int i=0; i<dir_count; i++) {
        if (strcmp(directory[i].filename, filename)== 0) {
            directory[i].size=new_size;
            return;
        }
    }
    strncpy(directory[dir_count].filename, filename, 63);
    directory[dir_count].size=new_size;
    dir_count++;
}

void *worker_thread(void *arg) {
    Operation *op=(Operation *)arg;
    worker_enter(&lock, op->id);

    pthread_mutex_lock(&lock.mutex);
    int concurrent=(lock.readers_active>1);
    pthread_mutex_unlock(&lock.mutex);

    int size=dir_find(op->filename);
    if (concurrent)
        printf("[Worker-%d] [concurrent read] FILE: %s SIZE: %d bytes\n",
               op->id, op->filename, size);
    else
        printf("[Worker-%d] FILE: %s SIZE: %d bytes\n",
               op->id, op->filename, size);

    simulate_delay();
    worker_exit(&lock);
    return NULL;
}

void *manager_thread(void *arg) {
    Operation *op=(Operation *)arg;
    manager_enter(&lock, op->id);

    int old_size=dir_find(op->filename);
    int new_size=old_size * 2 + 1024;

    /* check if supervisor preempted */
    printf("[Manager-%d] acquired write lock\n", op->id);
    dir_update(op->filename, new_size);
    printf("[Manager-%d] updated %s → %d bytes\n",
           op->id, op->filename, new_size);

    simulate_delay();
    manager_exit(&lock);
    return NULL;
}

void *supervisor_thread(void *arg) {
    Operation *op=(Operation *)arg;

    pthread_mutex_lock(&lock.mutex);
    int mgr_waiting=lock.managers_waiting;
    pthread_mutex_unlock(&lock.mutex);

    supervisor_enter(&lock, op->id);

    int old_size=dir_find(op->filename);
    int new_size=old_size * 2 + 512;

    if (mgr_waiting>0)
        printf("[Supervisor-%d] [supervisor preempts manager] acquired write lock\n", op->id);
    else
        printf("[Supervisor-%d] acquired write lock\n", op->id);

    dir_update(op->filename, new_size);
    printf("[Supervisor-%d] updated %s → %d bytes\n",
           op->id, op->filename, new_size);

    simulate_delay();
    supervisor_exit(&lock);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc<2) {
        fprintf(stderr, "Usage: ./warehouse <input_file>\n");
        return 1;
    }

    FILE *f=fopen(argv[1], "r");
    if (!f) { perror("fopen"); return 1; }

    fscanf(f, "%d %d %d %d",
           &num_workers, &num_managers, &num_supervisors, &num_ops);

    /* seed directory with some files */
    dir_update("item_0042.dat", 2048);
    dir_update("item_0099.dat", 512);
    dir_update("item_0001.dat", 1024);
    dir_update("item_0002.dat", 4096);
    dir_update("item_0003.dat", 256);

    ops=malloc(sizeof(Operation) * num_ops);
    for (int i=0; i<num_ops; i++) {
        fscanf(f, " %c %d %s %s",
               &ops[i].type, &ops[i].id, ops[i].op, ops[i].filename);
    }
    fclose(f);

    dirlock_init(&lock);

    pthread_t *threads=malloc(sizeof(pthread_t) * num_ops);
    for (int i=0; i<num_ops; i++) {
        if (ops[i].type== 'W')
            pthread_create(&threads[i], NULL, worker_thread,     &ops[i]);
        else if (ops[i].type== 'M')
            pthread_create(&threads[i], NULL, manager_thread,    &ops[i]);
        else
            pthread_create(&threads[i], NULL, supervisor_thread, &ops[i]);
        usleep(5000);  /* 5ms stagger so threads arrive in order */
    }

    for (int i=0; i<num_ops; i++)
        pthread_join(threads[i], NULL);

    dirlock_destroy(&lock);
    free(ops);
    free(threads);
    return 0;
}
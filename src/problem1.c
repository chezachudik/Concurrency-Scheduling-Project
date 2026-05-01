#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include "helpers.h"

typedef struct {
    int order_id;
    int raw_value;
} RawPacket;

typedef struct {
    int order_id;
    int encoded_value;
} EncodedPacket;

typedef struct {
    void **data;
    int capacity;
    int head;
    int tail;
    sem_t empty;
    sem_t full;
    pthread_mutex_t mutex;
} BoundedBuffer;

typedef struct {
    int T;
    sem_t *token_sem;
} TokenPool;

static int P,M,N,num_orders,T;
static int *token_counts;
static int *tA,*tB;

static BoundedBuffer bufA,bufB;
static TokenPool     pool;

static int next_order_id=0;
static pthread_mutex_t order_id_mutex=PTHREAD_MUTEX_INITIALIZER;


static int quant_done=0;
static pthread_mutex_t quant_done_mutex=PTHREAD_MUTEX_INITIALIZER;

void buf_init(BoundedBuffer *buf,int cap) {
    buf->data=malloc(sizeof(void *)*cap);
    buf->capacity=cap;
    buf->head=0;
    buf->tail=0;
    sem_init(&buf->empty,0,cap);
    sem_init(&buf->full, 0,0);
    pthread_mutex_init(&buf->mutex,NULL);
}

void buf_put(BoundedBuffer *buf,void *item) {
    sem_wait(&buf->empty);
    pthread_mutex_lock(&buf->mutex);
    buf->data[buf->head]=item;
    buf->head=(buf->head+1)%buf->capacity;
    pthread_mutex_unlock(&buf->mutex);
    sem_post(&buf->full);
}

void *buf_get(BoundedBuffer *buf) {
    sem_wait(&buf->full);
    pthread_mutex_lock(&buf->mutex);
    void *item=buf->data[buf->tail];
    buf->tail=(buf->tail+1)%buf->capacity;
    pthread_mutex_unlock(&buf->mutex);
    sem_post(&buf->empty);
    return item;
}

void buf_destroy(BoundedBuffer *buf) {
    free(buf->data);
    sem_destroy(&buf->empty);
    sem_destroy(&buf->full);
    pthread_mutex_destroy(&buf->mutex);
}

void pool_init(TokenPool *p,int num_types,int *counts) {
    p->T=num_types;
    p->token_sem=malloc(sizeof(sem_t)*num_types);
    for (int i=0; i<num_types; i++)
        sem_init(&p->token_sem[i],0,counts[i]);
}

void acquire_two_tokens(TokenPool *p,int typeA,int typeB) {
    int first=(typeA<typeB) ? typeA:typeB;
    int second=(typeA<typeB) ? typeB:typeA;
    sem_wait(&p->token_sem[first]);
    sem_wait(&p->token_sem[second]);
}

void release_two_tokens(TokenPool *p,int typeA,int typeB) {
    sem_post(&p->token_sem[typeA]);
    sem_post(&p->token_sem[typeB]);
}

void pool_destroy(TokenPool *p) {
    for (int i=0; i<p->T; i++)
        sem_destroy(&p->token_sem[i]);
    free(p->token_sem);
}

void *quantizer_thread(void *arg) {
    int id=*(int *)arg;
    int share=num_orders/P;
    int extra=(id== P-1) ? (num_orders%P):0;
    int total=share+extra;

    for (int i=0; i<total; i++) {
        simulate_delay();
        RawPacket *pkt=malloc(sizeof(RawPacket));
        pthread_mutex_lock(&order_id_mutex);
        pkt->order_id=next_order_id++;
        pthread_mutex_unlock(&order_id_mutex);
        pkt->raw_value=rand()%100;
        buf_put(&bufA,pkt);
    }

    pthread_mutex_lock(&quant_done_mutex);
    quant_done++;
    if (quant_done== P) {
        for (int i=0; i<P; i++)
            buf_put(&bufA,NULL);
    }
    pthread_mutex_unlock(&quant_done_mutex);

    return NULL;
}

void *encoder_thread(void *arg) {
    int id =*(int *)arg;
    int typeA=tA[id];
    int typeB=tB[id];

    while (1) {
        void *item=buf_get(&bufA);

        if (item==NULL) {
            buf_put(&bufB,NULL);
            break;
        }

        RawPacket *raw=(RawPacket *)item;

        acquire_two_tokens(&pool,typeA,typeB);
        simulate_delay();

        EncodedPacket *enc=malloc(sizeof(EncodedPacket));
        enc->order_id=raw->order_id;
        enc->encoded_value=raw->raw_value*2+typeA+typeB;

        release_two_tokens(&pool,typeA,typeB);
        free(raw);

        buf_put(&bufB,enc);
    }
    return NULL;
}

void *logger_thread(void *arg) {
    (void)arg;
    int sentinels_seen=0;

    while (1) {
        void *item=buf_get(&bufB);

        if (item==NULL) {
            sentinels_seen++;
            if (sentinels_seen== P)
                break;
            continue;
        }

        EncodedPacket *enc=(EncodedPacket *)item;
        printf("[Logger] order_id=%d encoded=%d\n",
               enc->order_id,enc->encoded_value);
        free(enc);
    }
    return NULL;
}

int main(int argc,char *argv[]) {
    if (argc<6) {
        fprintf(stderr,"Usage: ./pipeline P M N num_orders T cnt_0..cnt_{T-1} tA_0 tB_0...\n");
        return 1;
    }

    int idx=1;
    P=atoi(argv[idx++]);
    M=atoi(argv[idx++]);
    N=atoi(argv[idx++]);
    num_orders=atoi(argv[idx++]);
    T=atoi(argv[idx++]);

    token_counts=malloc(sizeof(int)*T);
    for (int i=0; i<T; i++)
        token_counts[i]=atoi(argv[idx++]);

    tA=malloc(sizeof(int)*P);
    tB=malloc(sizeof(int)*P);
    for (int i=0; i<P; i++) {
        tA[i]=atoi(argv[idx++]);
        tB[i]=atoi(argv[idx++]);
    }

    srand(42);

    buf_init(&bufA,M);
    buf_init(&bufB,N);
    pool_init(&pool,T,token_counts);

    pthread_t *quantizers=malloc(sizeof(pthread_t)*P);
    pthread_t *encoders=malloc(sizeof(pthread_t)*P);
    pthread_t  logger;
    int       *ids  =malloc(sizeof(int)*P);

    pthread_create(&logger,NULL,logger_thread,NULL);

    for (int i=0; i<P; i++) {
        ids[i]=i;
        pthread_create(&encoders[i],  NULL,encoder_thread,  &ids[i]);
        pthread_create(&quantizers[i],NULL,quantizer_thread,&ids[i]);
    }

    for (int i=0; i<P; i++)
        pthread_join(quantizers[i],NULL);
    for (int i=0; i<P; i++)
        pthread_join(encoders[i],NULL);
    pthread_join(logger,NULL);

    buf_destroy(&bufA);
    buf_destroy(&bufB);
    pool_destroy(&pool);

    free(token_counts); free(tA); free(tB);
    free(quantizers); free(encoders); free(ids);

    return 0;
}
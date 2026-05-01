#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_JOBS 100
#define MAX_GANTT 10000

typedef struct {
    char job_id[16];
    int  arrival_time;
    int  burst_time;
    int  remaining_time;
    int  completion_time;
    int  turnaround_time;
    int  waiting_time;
} Job;

typedef struct {
    char job_id[16];
    int  start;
    int  end;
} GanttSlot;

static Job jobs[MAX_JOBS];
static int num_jobs=0;


void reset_jobs(Job *src, Job *dst, int n) {
    for (int i=0; i<n; i++) {
        dst[i]=src[i];
        dst[i].remaining_time=src[i].burst_time;
        dst[i].completion_time=0;
        dst[i].turnaround_time=0;
        dst[i].waiting_time=0;
    }
}

/* ── Print Gantt chart ── */
void print_gantt(GanttSlot *g, int len) {
    printf("Gantt: |");
    for (int i=0; i<len; i++)
        printf(" %s |", g[i].job_id);
    printf("\nTime:  %d", g[0].start);
    for (int i=0; i<len; i++)
        printf(" %d", g[i].end);
    printf("\n");
}

/* ── Print metrics table ── */
void print_metrics(Job *j, int n) {
    printf("| Job | Arrival | Burst | Completion | Turnaround | Waiting |\n");
    printf("|-----|---------|-------|------------|------------|---------|\n");
    double sum_ta=0, sum_wt=0;
    int total_burst=0, last_end=0;
    for (int i=0; i<n; i++) {
        printf("| %s | %d | %d | %d | %d | %d |\n",
               j[i].job_id, j[i].arrival_time, j[i].burst_time,
               j[i].completion_time, j[i].turnaround_time, j[i].waiting_time);
        sum_ta     +=j[i].turnaround_time;
        sum_wt     +=j[i].waiting_time;
        total_burst +=j[i].burst_time;
        if (j[i].completion_time > last_end)
            last_end=j[i].completion_time;
    }
    printf("\nAverage Turnaround Time : %.1f\n", sum_ta / n);
    printf("Average Waiting Time    : %.1f\n",  sum_wt / n);
    printf("CPU Utilisation         : %.1f%%\n",
           100.0 * total_burst / last_end);
}

/* ── FCFS ── */
void fcfs(Job *j, int n, GanttSlot *g, int *glen) {
    Job tmp[MAX_JOBS];
    reset_jobs(j, tmp, n);
    *glen=0;
    int time=0;

    for (int i=0; i<n; i++) {
        if (time<tmp[i].arrival_time)
            time=tmp[i].arrival_time;
        g[*glen].start=time;
        time +=tmp[i].burst_time;
        g[*glen].end=time;
        strcpy(g[*glen].job_id, tmp[i].job_id);
        (*glen)++;
        tmp[i].completion_time=time;
        tmp[i].turnaround_time=time-tmp[i].arrival_time;
        tmp[i].waiting_time=tmp[i].turnaround_time-tmp[i].burst_time;
    }
    for (int i=0; i<n; i++) j[i]=tmp[i];
}

/* ── Preemptive SJF (SRTF) ── */
void preemptive_sjf(Job *j, int n, GanttSlot *g, int *glen) {
    Job tmp[MAX_JOBS];
    reset_jobs(j, tmp, n);
    *glen=0;

    int done=0, time=0;
    

    while (done<n) {
        /* find arrived job with shortest remaining time */
        int sel=-1;
        for (int i=0; i<n; i++) {
            if (tmp[i].arrival_time <=time && tmp[i].remaining_time > 0) {
                if (sel==-1 ||
                    tmp[i].remaining_time<tmp[sel].remaining_time ||
                    (tmp[i].remaining_time==tmp[sel].remaining_time &&
                     tmp[i].arrival_time<tmp[sel].arrival_time))
                    sel=i;
            }
        }

        if (sel==-1) { time++; continue; }

        /* merge into gantt if same job continues */
        if (*glen > 0 && strcmp(g[*glen-1].job_id, tmp[sel].job_id)==0) {
            g[*glen-1].end=time+1;
        } else {
            g[*glen].start=time;
            g[*glen].end=time+1;
            strcpy(g[*glen].job_id, tmp[sel].job_id);
            (*glen)++;
        }

        tmp[sel].remaining_time--;
        time++;

        if (tmp[sel].remaining_time==0) {
            tmp[sel].completion_time=time;
            tmp[sel].turnaround_time=time-tmp[sel].arrival_time;
            tmp[sel].waiting_time=tmp[sel].turnaround_time-tmp[sel].burst_time;
            done++;
        }
    }
    for (int i=0; i<n; i++) j[i]=tmp[i];
}

/* ── Round Robin ── */
void round_robin(Job *j, int n, GanttSlot *g, int *glen, int quantum) {
    Job tmp[MAX_JOBS];
    reset_jobs(j, tmp, n);
    *glen=0;

    int queue[MAX_JOBS * 100];
    int qhead=0, qtail=0;
    int time=0, done=0;
    int in_queue[MAX_JOBS]={0};

    /* enqueue jobs that arrive at time 0 */
    for (int i=0; i<n; i++) {
        if (tmp[i].arrival_time==0) {
            queue[qtail++]=i;
            in_queue[i]=1;
        }
    }

    while (done<n) {
        if (qhead==qtail) {
            time++;
            for (int i=0; i<n; i++)
                if (!in_queue[i] && tmp[i].remaining_time > 0 &&
                    tmp[i].arrival_time <=time) {
                    queue[qtail++]=i;
                    in_queue[i]=1;
                }
            continue;
        }

        int idx=queue[qhead++];
        int run=(tmp[idx].remaining_time<quantum) ?
                   tmp[idx].remaining_time : quantum;

        g[*glen].start=time;
        strcpy(g[*glen].job_id, tmp[idx].job_id);
        time +=run;
        g[*glen].end=time;
        (*glen)++;

        tmp[idx].remaining_time -=run;

        /* enqueue newly arrived jobs */
        for (int i=0; i<n; i++)
            if (!in_queue[i] && tmp[i].remaining_time > 0 &&
                tmp[i].arrival_time <=time) {
                queue[qtail++]=i;
                in_queue[i]=1;
            }

        if (tmp[idx].remaining_time==0) {
            tmp[idx].completion_time=time;
            tmp[idx].turnaround_time=time-tmp[idx].arrival_time;
            tmp[idx].waiting_time=tmp[idx].turnaround_time-tmp[idx].burst_time;
            done++;
        } else {
            queue[qtail++]=idx;
        }
    }
    for (int i=0; i<n; i++) j[i]=tmp[i];
}

/* ── MLFQ ── */
/* Level 1: RR q=4, Level 2: RR q=8, Level 3: FCFS */
void mlfq(Job *j, int n, GanttSlot *g, int *glen) {
    Job tmp[MAX_JOBS];
    reset_jobs(j, tmp, n);
    *glen=0;

    
    int in_queue[MAX_JOBS];
    for (int i=0; i<n; i++) { in_queue[i]=0; }

    int q1[MAX_JOBS*100], q1h=0, q1t=0;
    int q2[MAX_JOBS*100], q2h=0, q2t=0;
    int q3[MAX_JOBS*100], q3h=0, q3t=0;
    

    int time=0, done=0;

    /* enqueue arriving jobs at time 0 into level 1 */
    for (int i=0; i<n; i++)
        if (tmp[i].arrival_time==0) {
            q1[q1t++]=i;
            in_queue[i]=1;
        }

    while (done<n) {
        int idx=-1, cur_level=-1, quantum=0;

        if (q1h<q1t) { idx=q1[q1h++]; cur_level=0; quantum=4; }
        else if (q2h<q2t) { idx=q2[q2h++]; cur_level=1; quantum=8; }
        else if (q3h<q3t) { idx=q3[q3h++]; cur_level=2; quantum=tmp[idx].remaining_time; }
        else { time++;
            for (int i=0; i<n; i++)
                if (!in_queue[i] && tmp[i].remaining_time > 0 &&
                    tmp[i].arrival_time <=time) {
                    q1[q1t++]=i; in_queue[i]=1;
                }
            continue;
        }

        int run=(quantum==0 || tmp[idx].remaining_time <=quantum) ?
                   tmp[idx].remaining_time : quantum;

        g[*glen].start=time;
        strcpy(g[*glen].job_id, tmp[idx].job_id);
        time +=run;
        g[*glen].end=time;
        (*glen)++;

        tmp[idx].remaining_time -=run;

        /* enqueue newly arrived */
        for (int i=0; i<n; i++)
            if (!in_queue[i] && tmp[i].remaining_time > 0 &&
                tmp[i].arrival_time <=time) {
                q1[q1t++]=i; in_queue[i]=1;
            }

        if (tmp[idx].remaining_time==0) {
            tmp[idx].completion_time=time;
            tmp[idx].turnaround_time=time-tmp[idx].arrival_time;
            tmp[idx].waiting_time=tmp[idx].turnaround_time-tmp[idx].burst_time;
            done++;
        } else {
            /* demote to next level */
            if (cur_level==0)      { q2[q2t++]=idx; }
            else if (cur_level==1) { q3[q3t++]=idx; }
            else                     { q3[q3t++]=idx; }
        }
    }
    for (int i=0; i<n; i++) j[i]=tmp[i];
}

int main(int argc, char *argv[]) {
    if (argc<2) {
        fprintf(stderr, "Usage: ./scheduler <input_file>\n");
        return 1;
    }

    FILE *f=fopen(argv[1], "r");
    if (!f) { perror("fopen"); return 1; }

    char line[128];
    num_jobs=0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#' || line[0]=='\n') continue;
        if (sscanf(line, "%s %d %d",
                   jobs[num_jobs].job_id,
                   &jobs[num_jobs].arrival_time,
                   &jobs[num_jobs].burst_time)==3) {
            jobs[num_jobs].remaining_time=jobs[num_jobs].burst_time;
            num_jobs++;
        }
    }
    fclose(f);

    Job  tmp[MAX_JOBS];
    GanttSlot g[MAX_GANTT];
    int  glen;

    /*===FCFS===*/
    printf("===FCFS===\n");
    reset_jobs(jobs, tmp, num_jobs);
    fcfs(tmp, num_jobs, g, &glen);
    print_gantt(g, glen);
    print_metrics(tmp, num_jobs);
    printf("\n");

    /*===Preemptive SJF===*/
    printf("===Preemptive SJF===\n");
    reset_jobs(jobs, tmp, num_jobs);
    preemptive_sjf(tmp, num_jobs, g, &glen);
    print_gantt(g, glen);
    print_metrics(tmp, num_jobs);
    printf("\n");

    /*===RR q=3===*/
    printf("===Round Robin (q=3)===\n");
    reset_jobs(jobs, tmp, num_jobs);
    round_robin(tmp, num_jobs, g, &glen, 3);
    print_gantt(g, glen);
    print_metrics(tmp, num_jobs);
    printf("\n");

    /*===RR q=6===*/
    printf("===Round Robin (q=6)===\n");
    reset_jobs(jobs, tmp, num_jobs);
    round_robin(tmp, num_jobs, g, &glen, 6);
    print_gantt(g, glen);
    print_metrics(tmp, num_jobs);
    printf("\n");

    /*===MLFQ===*/
    printf("===MLFQ===\n");
    reset_jobs(jobs, tmp, num_jobs);
    mlfq(tmp, num_jobs, g, &glen);
    print_gantt(g, glen);
    print_metrics(tmp, num_jobs);
    printf("\n");

    return 0;
}
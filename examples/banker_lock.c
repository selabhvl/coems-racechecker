/* banker_lock.c */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define N_ACCOUNTS 10
#define N_THREADS  2
#define N_ROUNDS   100

/* Flag that activates/deactivates the data race condition from the command line */
int DATA_RACE = 0 ;

struct account {
        long balance;
        /* add a mutex to prevent races on balance */
        pthread_mutex_t mtx;
} accts[N_ACCOUNTS];

pthread_mutex_t transaction_mtx;

int rand_range(int N) {
        return (int)((double)rand() / ((double)RAND_MAX + 1) * N);
}

void *disburse(void *arg) {
        int id = * ((int *) arg);
        size_t i, from, to;
        long payment;

        for (i = 0; i < N_ROUNDS; i++) {
                from = rand_range(N_ACCOUNTS);
                do {
                        to = rand_range(N_ACCOUNTS);
                } while (to == from);

                /* get an exclusive lock on both balances before
                   updating (there's a problem with this, see below) */
                pthread_mutex_lock(&transaction_mtx);
                if( !DATA_RACE || (DATA_RACE & (id != 0)) ){
                    /* In case of DATA_RACE flag is 'on', the thread_id 0 forgets
                    to lock the accts[from].mtx mutex */
                    pthread_mutex_lock(&accts[from].mtx);
                } else {
                    printf("Data race condition. Thread id '%d' omits to lock mutex 'accts[%zu].mtxt'\n", id, from);
                }
                pthread_mutex_lock(&accts[to].mtx);
                pthread_mutex_unlock(&transaction_mtx);

                /* Do the actual transfer. */
                if (accts[from].balance > 0) {
                        payment = 1 + rand_range(accts[from].balance); usleep(250);
                        accts[from].balance -= payment; usleep(250);
                        accts[to].balance   += payment;
                }

                pthread_mutex_unlock(&accts[to].mtx);
                if( !DATA_RACE || (DATA_RACE & (id != 0)) ){
                    /* In case of DATA_RACE flag is 'on', the thread_id 0 forgets
                    to lock the accts[from].mtx mutex */
                        pthread_mutex_unlock(&accts[from].mtx);
                }
        }
        return NULL;
}

int main(int argc, char **argv) {
        size_t i;
        long total;
        pthread_t ts[N_THREADS];
        int args[N_THREADS];

        /* Pass any argument on command-line to trigger the race: */
        DATA_RACE = (argc > 1);

        srand(time(NULL));

        pthread_mutex_init(&transaction_mtx, NULL);

        /* set the initial balance, but also create a
           new mutex for each account */
        for (i = 0; i < N_ACCOUNTS; i++)
                accts[i] = (struct account)
                        {100, PTHREAD_MUTEX_INITIALIZER};

        for (i = 0; i < N_THREADS; i++){
            args[i] = i;
            pthread_create(&ts[i], NULL, disburse, &args[i]);
        }

        for (i = 0; i < N_THREADS; i++)
                pthread_join(ts[i], NULL);

        for (total = 0, i = 0; i < N_ACCOUNTS; i++)
                total += accts[i].balance;

        printf("Total money in system: %ld\n", total);
}

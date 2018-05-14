#include <signal.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "vkil_session.h"

typedef struct _vkil_session_entry
{
    pid_t     pid;
    int16_t  session_id;
    int8_t   card_id;
} vkil_session_entry;

typedef struct _vkil_session_table
{
    int                total_count;
    int                counts[VKIL_MAX_CARD];
    vkil_session_entry table[VKIL_MAX_SESSION];
} vkil_session_table;

vkil_session_entry curr_vse = {0}; // a local copy of the current session entry

/*
    get System V semaphore set id
    initialize sem
*/
int vkil_get_semid()
{
    key_t semkey   = ftok(".", 'S');
    int nsems      = 1;
    int semflag    = IPC_CREAT | IPC_EXCL | 0666;
    int semid      = semget(semkey, nsems, semflag);

    if (semid >= 0) {
        struct sembuf sb = {0, 1, 0};

        if (semop(semid, &sb, 1) < 0)
            goto fail;

    } else if (errno == EEXIST) {
        // sem exists already, check if the sem has been inited
        if ((semid = semget(semkey, nsems, 0)) < 0)
            goto fail;

        union semun {
            int              val;
            struct semid_ds *buf;
            unsigned short  *array;
            struct seminfo  *__buf;
        };
        union semun arg;
        struct semid_ds buf;
        arg.buf = &buf;
        int inited = 0;

        for (int i = 0; i < 10 && !inited; i++) {
            semctl(semid, 0, IPC_STAT, arg);
            if (arg.buf->sem_otime != 0) // otime not zero, thus sem has been inited
                inited = 1;
            else
                sleep(0.5);
        }
        if(!inited)
            goto fail;
    } else {
        goto fail;
    }

    return semid;

fail:
    semctl(semid, 0, IPC_RMID);
    return -1;
}

int vkil_lock_sem(const int semid)
{
    struct sembuf sb = {0, -1, 0};
    if (semop(semid, &sb, 1) < 0) // wait for and get resource
        return -1;

    return 0;
}

int vkil_unlock_sem(const int semid)
{
    struct sembuf sb = {0, 1, 0};
    if (semop(semid, &sb, 1) < 0) // release
        return -1;

    return 0;
}

/*
    This will probably need more info from the caller (vkapi/vkil_api).
    currently stub, simply return 0.
*/
int8_t vkil_select_card()
{
    // note:
    // take in considerations of sessions count in each card, watch for VKIL_MAX_SESSION_PER_CARD
    return 0;
}

int vkil_create_session(vkil_session_table* vst, const int index)
{
    vst->table[index].pid        = getpid();
    vst->table[index].session_id = index;
    vst->table[index].card_id    = vkil_select_card();
    vst->total_count++;
    vst->counts[vst->table[index].card_id]++;
    return 0;
}

/* 
    'updates' table, or clear out finished sessions 
*/
void vkil_update_session_table(vkil_session_table *vst)
{
    if (vst) {
        for (int i = 0; i < VKIL_MAX_SESSION; i++) {
            if (kill(vst->table[i].pid, 0)) {
                // process with pid no longer running (session finished)
                vst->table[i].pid = 0;
                vst->total_count--;
                vst->counts[vst->table[i].card_id]--;
            }
        }
    }
}

vkil_session_table* vkil_open_session_table()
{
    key_t shmkey   = ftok(".", 'M');
    size_t shmsize = sizeof(vkil_session_table);
    int shmflag    = IPC_CREAT | 0666;
    int shmid      = shmget(shmkey, shmsize, shmflag); //contents are initialized to zeros

    vkil_session_table *vst = (vkil_session_table *) shmat(shmid, NULL, 0);
    vkil_update_session_table(vst);
    return vst;
}

int vkil_close_session_table(const vkil_session_table* vst)
{
    return shmdt((void *) vst);
}

/*
    1  : found
    0  : no match entry but got a open slot
    -1 : no match entry but table full
*/
int vkil_find_session_entry(const vkil_session_table* vst, int *index)
{
    if(!vst->total_count) {
        *index = 0;
        return 0;
    }

    *index = -1;
    int pid = getpid();
    int empty = VKIL_MAX_SESSION;
    for (int i = 0; i < VKIL_MAX_SESSION; i++) {
        // entry in table
        if (pid == vst->table[i].pid) {
            curr_vse = vst->table[i];
            *index = i;
            return 1;
        } else if (!vst->table[i].pid && empty > i)
            empty = i;
    }

    if (empty < VKIL_MAX_SESSION) {
        *index = empty;
        return 0;
    }

    return -1;
}

/*
    Gets the current session entry (create one if new session) for the current process.
    Copies to the local curr_vse container for faster lookups
*/
int vkil_get_current_session_entry()
{
    int index, ret, semid;

    if ((semid = vkil_get_semid()) < 0)
        goto fail_sem;

    if (vkil_lock_sem(semid) < 0)
        goto fail_sem;

    vkil_session_table* vst = vkil_open_session_table();

    if (!vst) 
        goto fail;

    if ((ret = vkil_find_session_entry(vst, &index)) < 0)
        goto fail;

    if (index >= 0 && !ret) {
        if (vkil_create_session(vst, index) < 0)
            goto fail;
    }

    curr_vse.pid        = vst->table[index].pid;
    curr_vse.session_id = vst->table[index].session_id;
    curr_vse.card_id    = vst->table[index].card_id;

    if (vkil_close_session_table(vst) < 0)
        goto fail;
    // unlock
    if (vkil_unlock_sem(semid) < 0)
        goto fail;

    return 0;
    

fail:
    vkil_close_session_table(vst);
fail_sem:
    if (semid >= 0)
        vkil_unlock_sem(semid);
    curr_vse.pid        = -1;
    curr_vse.session_id = -1;
    curr_vse.card_id    = -1;
    return -1;
}

int16_t vkil_get_session_id()
{
    if (!curr_vse.pid)
        vkil_get_current_session_entry();
    return curr_vse.session_id;
}

int8_t vkil_get_card_id()
{
    if (!curr_vse.pid)
        vkil_get_current_session_entry();

    return curr_vse.card_id;
}

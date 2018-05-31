/*
 * Copyright 2018 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 (GPLv2) for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 (GPLv2) along with this source code.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>
#include "vkil_session.h"

typedef struct _vkil_session_entry {
	pid_t     pid;
	int16_t  session_id;
	int8_t   card_id;
} vkil_session_entry;

typedef struct _vkil_session_table {
	int                total_count;
	int                counts[VKIL_MAX_CARD];
	vkil_session_entry table[VKIL_MAX_SESSION];
} vkil_session_table;

/* a local copy of the current session entry */
vkil_session_entry curr_vse = {0};

/**
 * Get a System V semaphore's semid of this session manager
 * The semaphore is used to prevent concurrent accesses to the session table
 *
 * @return semid on success, -1 otherwise
 */
int vkil_get_semid(void)
{
	/*
	 * TODO: Use a better path for ftok
	 *       Calling from different working directories creates different
	 *       semkeys, thus different semids
	 */
	key_t semkey   = ftok(".", 'S');
	int nsems      = 1;
	int semflag    = IPC_CREAT | IPC_EXCL | 0666;
	int semid      = semget(semkey, nsems, semflag);

	if (semid >= 0) {
		/* sem created, init val to 1 */
		int ret;
		struct sembuf sb = {0, 1, 0};

		ret = semop(semid, &sb, 1);
		if (ret)
			goto fail;

	} else if (errno == EEXIST) {
		/* sem exists already */
		semid = semget(semkey, nsems, 0);
		if (semid < 0)
			goto fail;

		/*
		 * Check if sem is inited, wait a few moment if not
		 * Make semctl() call to get sem's info and check on sem_otime
		 *
		 * As suggested on the semctl's man page, type union semun needs
		 * be defined by the caller for the use of the fourth argument
		 */
		union semun {
			int              val;
			struct semid_ds *buf;
			unsigned short  *array;
			struct seminfo  *__buf;
		};
		union semun arg;
		struct semid_ds buf;
		int inited = 0;
		int i;

		arg.buf = &buf;

		for (i = 0; i < 10 && !inited; i++) {
			semctl(semid, 0, IPC_STAT, arg);
			if (arg.buf->sem_otime != 0)
				inited = 1;
			else
				sleep(0.5);
		}

		if (!inited)
			goto fail;
	} else {
		goto fail;
	}

	return semid;

fail:
	semctl(semid, 0, IPC_RMID); /* TODO: need better code cleanup */
	return -1;
}

/**
 * Gets/locks the semaphore
 *
 * @param semid    the semid of the semaphore
 * @return         zero on success, -1 otherwise
 */
int vkil_lock_sem(const int semid)
{
	int ret;
	struct sembuf sb = {0, -1, 0};

	ret = semop(semid, &sb, 1);

	return ret;
}

/**
 * Releases/unlocks the semaphore
 *
 * @param semid    the semid of the semaphore
 * @return         zero on success, -1 otherwise
 */
int vkil_unlock_sem(const int semid)
{
	int ret;
	struct sembuf sb = {0, 1, 0};

	ret = semop(semid, &sb, 1);

	return ret;
}

/**
 * Selects the appropriate card for the current session
 *
 * @return selected card id on success, -1 otherwise
 */
int8_t vkil_select_card(void)
{
	/* TODO */
	return 0;
}

/**
 * Creates a session entry in the session table
 *
 * @param vst      a vkil session table
 * @param index    table index to have the entry created onto
 * @return         zero on success, -1 otherwise
 */
int vkil_create_session(vkil_session_table *vst, const int index)
{
	vst->table[index].pid        = getpid();
	vst->table[index].session_id = index;
	vst->table[index].card_id    = vkil_select_card();
	vst->total_count++;
	vst->counts[vst->table[index].card_id]++;

	return 0;
}

/**
 * Updates the session table.
 * Finished sessions are removed from the table
 *
 * @param vst    the session table to be updated
 */
void vkil_update_session_table(vkil_session_table *vst)
{
	if (vst) {
		for (int i = 0; i < VKIL_MAX_SESSION; i++) {
			int ret = kill(vst->table[i].pid, 0);

			if (ret) {
				vst->table[i].pid = 0;
				vst->total_count--;
				vst->counts[vst->table[i].card_id]--;
			}
		}
	}
}

/**
 * Opens and gets the session table
 * The session table resides in a shared memory of this session manager
 *
 * @return the session table on success, NULL otherwise
 */
vkil_session_table *vkil_open_session_table()
{
	/*
	 * TODO: Use a better path for ftok
	 *       Calling from different working directories creates different
	 *       shmkeys, thus different shmids
	 */
	key_t shmkey            = ftok(".", 'M');
	size_t shmsize          = sizeof(vkil_session_table);
	int shmflag             = IPC_CREAT | 0666;
	int shmid               = shmget(shmkey, shmsize, shmflag);
	vkil_session_table *vst = shmat(shmid, NULL, 0);

	vkil_update_session_table(vst);

	/* TODO: shmat() does NOT return NULL but (void *)-1 on error */
	return vst;
}

/**
 * Closes the session table
 *
 * @return zero on success, -1 otherwise
 */
int vkil_close_session_table(const vkil_session_table *vst)
{
	return shmdt(vst);
}

/**
 * Finds the current session entry in the session table
 *
 * @param vst      the session table to look for
 * @param index    the index of the found entry
 * @return         1  if entry found
 *                 0  if entry not found but table has a open slot
 *                 -1 if entry not found but table is full
 */
int vkil_find_session_entry(const vkil_session_table *vst, int *index)
{
	int pid;
	int empty;

	if (!vst->total_count) {
		*index = 0;
		return 0;
	}

	pid = getpid();
	empty = VKIL_MAX_SESSION;

	for (int i = 0; i < VKIL_MAX_SESSION; i++) {
		if (pid == vst->table[i].pid) {
			curr_vse = vst->table[i];
			*index = i;
			return 1;
		} else if (!vst->table[i].pid && empty > i) {
			empty = i;
		}
	}

	if (empty < VKIL_MAX_SESSION) {
		*index = empty;
		return 0;
	}

	return -1;
}

/**
 * Gets the current session entry (create one if a new session)
 * Make a local copy to curr_vse
 * In case of errors, fields in curr_vse are all set to -1
 *
 * return zero on success, -1 otherwise
 */
int vkil_get_current_session_entry(void)
{
	int index;
	int ret;
	int semid;
	vkil_session_table *vst;

	semid = vkil_get_semid();
	if (semid < 0)
		goto fail_sem;

	ret = vkil_lock_sem(semid);
	if (ret)
		goto fail_sem;

	vst = vkil_open_session_table();
	if (!vst)
		goto fail_search;

	ret = vkil_find_session_entry(vst, &index);
	if (ret < 0) {
		goto fail_search;
	} else if (ret == 0) {
		ret = vkil_create_session(vst, index);
		if (ret)
			goto fail_search;
	}

	curr_vse.pid        = vst->table[index].pid;
	curr_vse.session_id = vst->table[index].session_id;
	curr_vse.card_id    = vst->table[index].card_id;

	ret = vkil_close_session_table(vst);
	if (ret)
		goto fail_close;

	ret = vkil_unlock_sem(semid);
	if (ret)
		goto fail_sem;

	return 0;

fail_search:
	vkil_close_session_table(vst);

fail_close:
	vkil_unlock_sem(semid);

fail_sem:
	curr_vse.pid        = -1;
	curr_vse.session_id = -1;
	curr_vse.card_id    = -1;

	return -1;
}


/**
 * Gets the session id of the current session
 * A valid session id is >= 0, an invalid session id is -1
 *
 * @return the session id
 */
int16_t vkil_get_session_id(void)
{
	if (!curr_vse.pid)
		vkil_get_current_session_entry();

	return curr_vse.session_id;
}

/**
 * Gets the card id of the current session
 * A valid card id is >= 0, an invalid card id is -1
 *
 * @return the card id
 */
int8_t vkil_get_card_id(void)
{
	if (!curr_vse.pid)
		vkil_get_current_session_entry();

	return curr_vse.card_id;
}

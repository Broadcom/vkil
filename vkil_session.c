#include <stdio.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "vkil_session.h"

#define VKIL_MAX_SESSION 10 // some fixe size for now
#define VKIL_SHM_KEY     123 // some key value for now
#define VKIL_SHM_SIZE    (1 + 3 * VKIL_MAX_SESSION) * sizeof(int) // some size for now
#define VKIL_SHM_FLAG    0666
#define VKIL_SHM_ID      shmget(VKIL_SHM_KEY, VKIL_SHM_SIZE, IPC_CREAT|VKIL_SHM_FLAG)

typedef struct _vkil_session_entry
{
	int pid;
	int session_id;
	int card_id;
} vkil_session_entry;

typedef struct _vkil_session_table
{
	int count;
	vkil_session_entry table[VKIL_MAX_SESSION];
} vkil_session_table;

uint8_t vkil_select_card()
{
	// some mechanism to select the appropriate card
	return 1;
}

void vkil_create_session(vkil_session_table* session_table, int entry_index)
{
	// use count as the session id for now
	int session_id = session_table->count++;
	session_table->table[entry_index].pid = getpid();
	session_table->table[entry_index].session_id = session_id;
	session_table->table[entry_index].card_id = vkil_select_card();
}

vkil_session_table* vkil_update_session_table(vkil_session_table *session_table)
{
	// remove entries with exited pids
	// update count
	// will need to re-design the table
	return session_table;
}

vkil_session_table* vkil_get_session_table()
{
	// shmctl(VKIL_SHM_ID, IPC_RMID, NULL);
	return vkil_update_session_table((vkil_session_table *) shmat(VKIL_SHM_ID, NULL, 0));
}

int vkil_get_session_entry_index(vkil_session_table* session_table)
{
	// table empty
	if(session_table->count == 0)
		return 0;

	// entry in table
    int pid = getpid();
    int i;
	for (i = 0; i < session_table->count; i++) {
		if(pid == session_table->table[i].pid)
			return i;
	}

	// entry not in table
	if(i < VKIL_MAX_SESSION)
		return session_table->count;

	return -1;
}

uint16_t vkil_get_session_id()
{
	vkil_session_table* session_table = vkil_get_session_table();
	int entry_index = vkil_get_session_entry_index(session_table);
	if (entry_index < 0)
		return -1;
	if (session_table->count == 0 || entry_index == session_table->count)
		vkil_create_session(session_table, entry_index);

	return (uint16_t) session_table->table[entry_index].session_id;
}

uint8_t vkil_get_card_id()
{
	vkil_session_table* session_table = vkil_get_session_table();
	int entry_index = vkil_get_session_entry_index(session_table);
	if (entry_index < 0)
		return -1;
	if (session_table->count == 0 || entry_index == session_table->count)
		vkil_create_session(session_table, entry_index);

	return (uint8_t) session_table->table[entry_index].card_id;
}
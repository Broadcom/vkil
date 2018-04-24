#include <stdio.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "vkil_session.h"

#define vk_shm_key  123     // some key value for now
#define vk_shm_size 123     // some size for now
#define vk_shm_flg  0666
#define vk_shm_id   shmget(vk_shm_key, vk_shm_size, IPC_CREAT|vk_shm_flg)

uint16_t vkil_create_session()
{
	return 0;
}

uint8_t vkil_select_card()
{
	return 0;
}

uint16_t vkil_get_session_id()
{
	return 0;
}

uint8_t vkil_get_card_id()
{
	return 0;
}
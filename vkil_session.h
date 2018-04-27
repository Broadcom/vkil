#ifndef vkil_session_h__
#define vkil_session_h__

#include <stdint.h>

#define VKIL_MAX_CARD                4
#define VKIL_MAX_SESSION_PER_CARD    32 
#define VKIL_MAX_SESSION             VKIL_MAX_SESSION_PER_CARD * VKIL_MAX_CARD

/*
	returns session_id [0, VKIL_MAX_SESSION-1] if successful,
	-1 otherwise
*/
int16_t vkil_get_session_id();

/*
	returns card_id [0, VKIL_MAX_CARD-1] if successful,
	-1 otherwise
*/
int8_t vkil_get_card_id();

#endif //vkil_session_h__

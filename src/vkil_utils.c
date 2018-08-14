// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright(c) 2018 Broadcom
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "vkdrv_access.h"
#include "vkil_backend.h"
#include "vkil_error.h"
#include "vkil_internal.h"
#include "vkil_session.h"
#include "vkil_utils.h"

/**
 * Append a node at the the end of a linked list
 * if the linked list is not existing yet, create one
 * @param[in,out] head of the linked list
 * @param data to put i the linked list
 * @return added node handle if success, NULL otherwise
 */
vkil_node *vkil_ll_append(vkil_node **head, void *data)
{
	vkil_node *cursor = *head;
	int32_t ret;

	while (cursor != NULL)
		cursor = cursor->next;

	ret = vk_mallocz((void **)&cursor, sizeof(vkil_node));
	if (ret)
		goto fail;
	if (*head == NULL)
		*head = cursor;

	cursor->data = data;
	return cursor;

fail:
	return (NULL);
}

/**
 * Delete a node from the linked list
 * if it is the only node in the list, the list will be deleted
 * @param[in,out] head of the linked list, can be changed by tis function
 * @param[in] nd node to delete
 * @return 0 if success, error code otherwise
 */
int32_t vkil_ll_delete(vkil_node **head, vkil_node *nd)
{
	vkil_node *cursor = *head;
	vkil_node *prev   = NULL;
	int32_t ret;

	while (cursor != nd && cursor->next) {
		prev = cursor;
		cursor = cursor->next;
	}

	if (cursor != nd)
		// didn't find the node in the list
		goto fail;

	if (!prev) {
		// the node to remove is the head of the list
		if (cursor->next)
			*head = cursor->next;
		else
			*head = NULL;
	} else
		prev->next = cursor->next;

	vk_free((void **)&cursor);
	return 0;

fail:
	return (-EINVAL);

}

/**
 * Search for a node in a linked list
 * @param[in] head of the linked list
 * @param[in] search function to apply
 * @param[in] data_ref pointer to search against
 * @return node containing the data, otherwise NULL
 */
vkil_node *vkil_ll_search(vkil_node *head,
		int32_t (*cmp_f)(const void *data, const void *data_ref),
		const void *data_ref)
{
	vkil_node *cursor = head;
	int32_t ret;

	while (cursor != NULL) {
		ret = cmp_f(cursor->data, data_ref);
		if (!ret)
			return cursor;
		cursor = cursor->next;
	}

fail:
	return NULL;
}

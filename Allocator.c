/*Connor Cormier, 8/12/14
  Buddy block memory allocator
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "Allocator.h"

/*Smallest possible block is 32 bytes (FreeNode is 24 on 64 bit machine)*/
#define LOW_ORDER 32
#define FREE 1
#define USED 0

typedef struct
{
	/*Only use a single bit to indicate freedom*/
	unsigned int freedom : 1;
	/*Identify the top level in this tree, allowing us to know whether the
	 block has a buddy at any given time*/
	unsigned int maxOrder : 7;
	/*7 bits for order, so block sizes from 1-2^126 bytes possible.*/
	unsigned int order  : 7;	
} Header;

typedef struct FreeNode
{
	Header head;
	struct FreeNode *next;
	struct FreeNode *prev;
} FreeNode;

typedef struct
{
	FreeNode *start;
	FreeNode *end;
} FreeList;

/*Number of orders. First structure in memory, gives length of following list*/
typedef struct
{
	unsigned int orders : 7;
} NumOrders;

/*The start of the heap*/
static void *heapStart;

unsigned int calcRequiredOrder(size_t reqBytes)
{
	/*Add size of header to required bytes*/
	reqBytes += sizeof(Header);

	/*Now get the minimum possible order*/
	int order = ceil(log((double)reqBytes / LOW_ORDER) / log(2));

	return order >= 0 ? order : 0;
}

/*Get the max order structure off of the heap*/
NumOrders *getNumOrders()
{
	return (NumOrders*)heapStart;
}

/*Get the 0 order free list off of the heap*/
FreeList *getFreeListStart()
{
	return (FreeList *)(((NumOrders*)heapStart) + 1);
}

/*Get a free list with a given order*/
FreeList *getListWithOrder(unsigned int order)
{
	FreeList *start = getFreeListStart();

	return start + order;
}

void removeFreeBlock(FreeNode *node)
{
	FreeList *lst = getListWithOrder(node->head.order);

	if (lst->start == node)
		lst->start = node->next;
	if (lst->end == node)
		lst->end = node->prev;

	if (node->prev != NULL)
	{
		node->prev->next = node->next;
		node->prev = NULL;
	}
	if (node->next != NULL)
	{
		node->next->prev = node->prev;
		node->next = NULL;
	}
}

void addFreeBlock(FreeNode *node)
{
	FreeList *lst = getListWithOrder(node->head.order);

	if (!lst->start)
		lst->start = node;
	
	if (!lst->end)
		lst->end = node;
	else
	{
		lst->end->next = node;
		node->prev = lst->end;
		node->next = NULL;
		lst->end = node;
	}
}

Header *getBuddy(Header *head)
{
	/*No buddies here*/
	if (head->maxOrder == head->order)
		return NULL;
	
	size_t offset = (size_t)head;

	/*Flip the relevant bit*/
	unsigned int flipMe = head->order + (unsigned int)(log(LOW_ORDER) / log(2));
	size_t buddyLoc = ((size_t)head - offset) ^ (size_t)(1 << flipMe);

	/*Add the offset back in*/
	return (Header*)(offset + buddyLoc);
}

/*Split a block in half, move down to the next smallest list */
void splitBlock(FreeNode *node)
{
	/*Can't split a low order block!*/
	if (node->head.order == 0)
		return;

	/*Remove from original list*/
	removeFreeBlock(node);

	/*Decrement order and get buddy*/
	node->head.order--;	
	FreeNode *buddy = (FreeNode*)getBuddy((Header*)node);

	buddy->head.freedom = FREE;
	buddy->head.order = node->head.order;
	buddy->head.maxOrder = node->head.maxOrder;


	/*Add both this block and its buddy to the appropriate list*/
	addFreeBlock(buddy);
	addFreeBlock(node);
}

FreeNode *getFreeBlockWithOrder(unsigned int order)
{
	unsigned int numOrders = getNumOrders()->orders;

	FreeNode *free = NULL;

	int i, j;
	for (i = order; i < numOrders && free == NULL; i++)
	{		
		free = getListWithOrder(i)->start;

		/*Split it down to the right size if not null*/
		for (j = 0; free != NULL && j < i - order; j++)
			splitBlock(free);
	}

	return free;
}

void setupHeap(void *start, size_t heapSize)
{
	heapStart = start;

	/*Set max order*/
	NumOrders *n = getNumOrders();

	int numOrders = 1;
	size_t orderSize = LOW_ORDER;
	while ((int)(heapSize - (orderSize) - sizeof(NumOrders) - (numOrders * sizeof(FreeList)))
		   >= 0)
	{
		orderSize <<= 1;
		numOrders++;
	}
	numOrders -= 1;

	n->orders = numOrders;

	if (numOrders <= 0)
	{
		fprintf(stderr, "Not enough space to set up heap!\n");
		exit(2);
	}

	/*Set up each free list*/
	int i;
	for (i = 0; i < numOrders; i++)
	{
		FreeList *lst = getListWithOrder(i);
		lst->start = NULL;
		lst->end = NULL;
	}

	size_t overhead = sizeof(NumOrders) + (numOrders * sizeof(FreeList));

	/*Now set up blocks*/
	size_t avaSpace = heapSize - overhead;
	size_t freespaceStartPosition = (size_t)heapStart + overhead;

	while (avaSpace >= LOW_ORDER)
	{
		unsigned int maxOrder = (unsigned int)(log((double)avaSpace / 32) / log(2));
		size_t orderSize = LOW_ORDER << maxOrder;

		avaSpace -= orderSize;

		/*Add the pointer to the appropriate list*/
		freespaceStartPosition += orderSize;

		FreeNode *node = (FreeNode*)freespaceStartPosition;
		node->head.freedom = FREE;
		node->head.order = maxOrder;
		node->head.maxOrder = maxOrder;
		node->next = NULL;
		node->prev = NULL;

		addFreeBlock(node);
	}
}

void *xMalloc(size_t bytes)
{
	unsigned int order = calcRequiredOrder(bytes);

	FreeNode *node = (FreeNode*)getFreeBlockWithOrder(order);
	

	/*No memory for you*/
	if (node == NULL)
		return NULL;

	removeFreeBlock(node);

	node->head.freedom = USED;

	return (void *)((Header*)node + 1);
}

void addAndConsolidate(FreeNode *node)
{
	FreeNode *other = (FreeNode*)getBuddy((Header*)node);

	if (other != NULL && other->head.freedom == FREE)
	{
		/*Remove other node*/
		removeFreeBlock(other);
		
		FreeNode *combo = node < other ? node : other;

		combo->head.order++;

		addAndConsolidate(combo);
	}
	else
	{
		addFreeBlock(node);
	}
}

void xFree(void *pnt)
{
	FreeNode *node = (FreeNode*)((Header*)pnt - 1);

	if (node->head.freedom == FREE)
	{
		fprintf(stderr, "Double free!");
		exit(3);
	}

	node->head.freedom = FREE;

	/*Combine with buddy if possible*/
	addAndConsolidate(node);
}

int main(int argc, char **argv)
{
	size_t heapSize = argc > 1 ? atoi(argv[1]) : (512 * 1024);

	void *heapStart = malloc(heapSize);

	if (!heapStart)
	{
		fprintf(stderr, "Failed to allocate %lu bytes for heap!\n", heapSize);
		return 1;
	}

	setupHeap(heapStart, heapSize);

	size_t bytes = 112 * 1024;

	void *one = xMalloc(bytes);
	printf("One: %p\n", one);
	void *two = xMalloc(bytes);
	printf("Two: %p\n", two);
	void *three = xMalloc(bytes);
	printf("Three: %p\n", three);
	void *four = xMalloc(bytes);
	printf("Four: %p\n", four);

	/*Try again to allocate four after freeing three*/
	xFree(one);
	four = xMalloc(bytes);
	printf("Four (post free): %p\n", four);
	
	return 0;
}

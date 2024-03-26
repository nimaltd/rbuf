
/************************************************************************************************************
**************    Include Headers
************************************************************************************************************/

#include "rbuf.h"

/************************************************************************************************************
**************    Private Definitions
************************************************************************************************************/

// none

/************************************************************************************************************
**************    Private Variables
************************************************************************************************************/

// none

/************************************************************************************************************
**************    Private Functions
************************************************************************************************************/

// none

/************************************************************************************************************
**************    Public Functions
************************************************************************************************************/

/**
  * @brief  Initialize the RingBuffer.
  * @note   Creates and inits new circular buffer object
  *
  * @param  ItemSize: Size of Item (in Byte)
  * @param  Capacity: Number of Item Could Store
  *
  * @retval RBUF_HandleTypeDef: NULL or RBUF_HandleTypeDef* pointer
  */
RBUF_HandleTypeDef* RBUF_Init(size_t ItemSize, int Capacity)
{
	if (Capacity <= 0)
		return NULL;
	RBUF_HandleTypeDef *newRBUF = (RBUF_HandleTypeDef *)malloc(sizeof(RBUF_HandleTypeDef));
  if (newRBUF)
	{
		newRBUF->ItemSize = ItemSize;
		newRBUF->Capacity = Capacity;
		newRBUF->Head = 0;
		newRBUF->Tail = 0;
		newRBUF->Data = (void*) malloc(ItemSize * (Capacity + 1));
		if (!newRBUF->Data)
		{
			free(newRBUF);
			return NULL;
		}
		memset(newRBUF->Data, 0, ItemSize * (Capacity + 1));
	}
  return newRBUF;
}

/***********************************************************************************************************/

/**
  * @brief  De-Initialize the RingBuffer.
  * @note   Deletes the RingBuffer object and frees the allocated space
  *
  * @param  **RingBuffer: The RingBuffer Struct
  *
  * @retval RBUF_HandleTypeDef: NULL or RBUF_HandleTypeDef* pointer
  */
void RBUF_Destroy(RBUF_HandleTypeDef **RingBuffer)
{
	if (!(*RingBuffer))
		return;
	if ((*RingBuffer)->Data)
		free((*RingBuffer)->Data);
	free(*RingBuffer);
	*RingBuffer = NULL;
}

/***********************************************************************************************************/

/**
  * @brief  Push data.
  * @note   Pushes new data to buffer and moves tail to next position
  *
  * @param  RingBuffer: The RingBuffer Struct
  * @param  *Data: the Data pointer should written
  *
  * @retval bool: true or false
  */
bool RBUF_Push(RBUF_HandleTypeDef *RingBuffer, const void *Data)
{
	if (RBUF_IsFull(RingBuffer))
		return false;
	memcpy((void*) &RingBuffer->Data[RingBuffer->Tail + 1], Data, RingBuffer->ItemSize);
	RingBuffer->Tail += RingBuffer->ItemSize;
	return true;
}

/***********************************************************************************************************/

/**
  * @brief  pop Data.
  * @note   Poppes data from buffer and moves head to next position
  *
  * @param  RingBuffer: The RingBuffer Struct
  * @param  *Data: the Data pointer should read
  *
  * @retval bool: true or false
  */
bool RBUF_Pop(RBUF_HandleTypeDef *RingBuffer, void *Data)
{
	if (RBUF_IsEmpty(RingBuffer))
		return false;
	memcpy(Data, (void*) &RingBuffer->Data[RingBuffer->Head + 1], RingBuffer->ItemSize);
	RingBuffer->Data[RingBuffer->Head] = 0;
	RingBuffer->Head += RingBuffer->ItemSize;
	if (RBUF_IsEmpty(RingBuffer))
	{
		RingBuffer->Head = 0;
		RingBuffer->Tail = 0;
	}
	return true;
}

/***********************************************************************************************************/

/**
  * @brief  Check RingBuffer is Empty.
  * @note   Checks whether the circular buffer empty (true) or not (false)
  *
  * @param  RingBuffer: The RingBuffer Struct
  *
  * @retval bool: true or false
  */
bool RBUF_IsEmpty(RBUF_HandleTypeDef *RingBuffer)
{
	return (RingBuffer->Tail == RingBuffer->Head) ? true : false;
}

/***********************************************************************************************************/

/**
  * @brief  Check RingBuffer is Full.
  * @note   Checks whether the circular buffer full (true) or not (false)
  *
  * @param  RingBuffer: The RingBuffer Struct
  *
  * @retval bool: true or false
  */
bool RBUF_IsFull(RBUF_HandleTypeDef *RingBuffer)
{
	return (((RingBuffer->Tail + RingBuffer->ItemSize) \
					% ((RingBuffer->Capacity + 1) * RingBuffer->ItemSize)) == RingBuffer->Head) ? true : false;
}

/***********************************************************************************************************/

/**
  * @brief  Get free Space
  * @note   Gets the free available space in circular buffer
  *
  * @param  RingBuffer: The RingBuffer Struct
  *
  * @retval int: returns available space
  */
int RBUF_GetFree(RBUF_HandleTypeDef *RingBuffer)
{
	int freeSpace = 0;
	if (RingBuffer->Head > RingBuffer->Tail)
		freeSpace = RingBuffer->Capacity \
				- ((RingBuffer->Head - RingBuffer->Tail) / RingBuffer->ItemSize);
	else
		freeSpace = RingBuffer->Capacity \
				- ((RingBuffer->Tail - RingBuffer->Head) / RingBuffer->ItemSize);
	return freeSpace;
}

/***********************************************************************************************************/

/**
  * @brief  Get Capacity
  * @note   Gets the capacity of circular buffer
  *
  * @param  RingBuffer: The RingBuffer Struct
  *
  * @retval int: returns capacity
  */
int RBUF_GetCapacity(RBUF_HandleTypeDef *RingBuffer)
{
	return RingBuffer->Capacity;
}

/***********************************************************************************************************/


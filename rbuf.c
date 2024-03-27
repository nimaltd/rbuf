
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
  * @brief  Initializes a ring buffer.
  * @note   This function initializes a ring buffer with the specified item size
  *         and capacity. Memory is allocated for the buffer based on the item size
  *         and capacity provided. The buffer is initially empty.
  * @param  ItemSize: Size of each item in the buffer.
  * @param  Capacity: Maximum number of items the buffer can hold.
  * @retval Pointer to the initialized RBUF_HandleTypeDef structure if successful,
  *         NULL if memory allocation fails.
  */
RBUF_HandleTypeDef* RBUF_Init(size_t ItemSize, uint32_t Capacity)
{
  RBUF_HandleTypeDef *rbuf = (RBUF_HandleTypeDef*) malloc(sizeof(RBUF_HandleTypeDef));
  if (rbuf == NULL)
  {
    return NULL;
  }
  rbuf->ItemSize = ItemSize;
  rbuf->Capacity = Capacity;
  rbuf->Buffer = malloc(ItemSize * Capacity);
  if (rbuf->Buffer == NULL)
  {
    free(rbuf);
    return NULL;
  }
  rbuf->Head = 0;
  rbuf->Tail = 0;
  rbuf->Count = 0;
  return rbuf;
}

/***********************************************************************************************************/

/**
  * @brief  Destroys a ring buffer and frees memory.
  * @note   This function deallocates the memory allocated for the ring buffer
  *         pointed to by the given handle.
  * @param  rbuf: Pointer to the ring buffer handle.
  * @retval None
  */
void RBUF_DeInit(RBUF_HandleTypeDef *rbuf)
{
  if (rbuf != NULL)
  {
    if (rbuf->Buffer != NULL)
    {
      free(rbuf->Buffer);
    }
    free(rbuf);
  }
}

/***********************************************************************************************************/

/**
  * @brief  Checks if the ring buffer is full.
  * @note   This function checks whether the ring buffer is full or not.
  *         It returns true if the number of elements in the buffer is equal
  *         to its capacity, indicating that the buffer is full; otherwise,
  *         it returns false.
  * @param  rbuf: Pointer to the ring buffer handle.
  * @retval true if the buffer is full, false otherwise.
  */
bool RBUF_IsFull(RBUF_HandleTypeDef *rbuf)
{
  return rbuf->Count == rbuf->Capacity;
}

/***********************************************************************************************************/

/**
  * @brief  Checks if the ring buffer is empty.
  * @note   This function checks whether the ring buffer is empty or not.
  *         It returns true if the number of elements in the buffer is zero,
  *         indicating that the buffer is empty; otherwise, it returns false.
  * @param  rbuf: Pointer to the ring buffer handle.
  * @retval true if the buffer is empty, false otherwise.
  */
bool RBUF_IsEmpty(RBUF_HandleTypeDef *rbuf)
{
  return rbuf->Count == 0;
}

/***********************************************************************************************************/

/**
  * @brief  Get the number of free slots available in the ring buffer.
  * @note   This function returns the number of free slots available in the
  *         ring buffer. It calculates the difference between the capacity
  *         of the buffer and the current count of elements.
  * @param  rbuf: Pointer to the ring buffer handle.
  * @retval Number of free slots in the ring buffer.
  */
uint32_t RBUF_Available(RBUF_HandleTypeDef *rbuf)
{
  return rbuf->Capacity - rbuf->Count;
}

/***********************************************************************************************************/

/**
  * @brief  Pushes data into the ring buffer.
  * @note   This function pushes data into the ring buffer if there is available
  *         space. If the buffer is full, the operation fails and returns false.
  * @param  rbuf: Pointer to the ring buffer handle.
  * @param  data: Pointer to the data to be pushed into the buffer.
  * @retval true if the data was successfully pushed, false otherwise (buffer full).
  */
bool RBUF_Push(RBUF_HandleTypeDef *rbuf, const void *data)
{
  if (RBUF_IsFull(rbuf))
  {
    return 0;
  }

  uint8_t *dest = (uint8_t*) rbuf->Buffer + rbuf->Head * rbuf->ItemSize;
  memcpy(dest, data, rbuf->ItemSize);
  rbuf->Head = (rbuf->Head + 1) % rbuf->Capacity;
  rbuf->Count++;
  return 1;
}

/***********************************************************************************************************/

/**
  * @brief  Pops data from the ring buffer.
  * @note   This function pops data from the ring buffer if there is available
  *         data. If the buffer is empty, the operation fails and returns false.
  * @param  rbuf: Pointer to the ring buffer handle.
  * @param  data: Pointer to the memory where the popped data will be stored.
  * @retval true if data was successfully popped, false otherwise (buffer empty).
  */
bool RBUF_Pop(RBUF_HandleTypeDef *rbuf, void *data)
{
  if (RBUF_IsEmpty(rbuf))
  {
    return 0;
  }
  uint8_t *src = (uint8_t*) rbuf->Buffer + rbuf->Tail * rbuf->ItemSize;
  memcpy(data, src, rbuf->ItemSize);
  rbuf->Tail = (rbuf->Tail + 1) % rbuf->Capacity;
  rbuf->Count--;
  return 1;
}

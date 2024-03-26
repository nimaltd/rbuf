
#ifndef _CBUF_H_
#define _CBUF_H_

/***********************************************************************************************************

  Author:     Nima Askari
  Github:     https://www.github.com/NimaLTD
  LinkedIn:   https://www.linkedin.com/in/nimaltd
  Youtube:    https://www.youtube.com/@nimaltd
  Instagram:  https://instagram.com/github.NimaLTD

  I have modified this library: https://github.com/serbayozkan/GenericRingBuffer_C

  Version:    1.0.0

  History:

              1.0.0
              - First Release
              - Support STM32CubeMx Packet installer

***********************************************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/************************************************************************************************************
**************    Include Headers
************************************************************************************************************/

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/************************************************************************************************************
**************    Public Definitions
************************************************************************************************************/

// none

/************************************************************************************************************
**************    Public struct/enum
************************************************************************************************************/

typedef struct
{
	int 								Head;
  int 								Tail;
	int 								Capacity;
	size_t 							ItemSize;
	uint8_t 						*Data;

} RBUF_HandleTypeDef;

/************************************************************************************************************
**************    Public Functions
************************************************************************************************************/

RBUF_HandleTypeDef* 	RBUF_Init(size_t ItemSize, int Capacity);
void 									RBUF_Destroy(RBUF_HandleTypeDef **RingBuffer);
bool 									RBUF_Push(RBUF_HandleTypeDef *RingBuffer, const void *Data);
bool 									RBUF_Pop(RBUF_HandleTypeDef *RingBuffer, void *Data);
bool									RBUF_IsEmpty(RBUF_HandleTypeDef *RingBuffer);
bool 									RBUF_IsFull(RBUF_HandleTypeDef *RingBuffer);
int 									RBUF_GetFree(RBUF_HandleTypeDef *RingBuffer);
int 									RBUF_GetCapacity(RBUF_HandleTypeDef *RingBuffer);

#ifdef __cplusplus
}
#endif

#endif

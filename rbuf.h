#ifndef _RBUF_H_
#define _RBUF_H_

/***********************************************************************************************************

  Author:     Nima Askari
  Github:     https://www.github.com/NimaLTD
  LinkedIn:   https://www.linkedin.com/in/nimaltd
  Youtube:    https://www.youtube.com/@nimaltd
  Instagram:  https://instagram.com/github.NimaLTD

  Version:    2.1.0

  History:
			  2.1.0
              - Added RBUF_Used(),RBUF_Format() functions

              2.0.0
              - Rewrite again
              - Support STM32CubeMx Packet installer

***********************************************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/************************************************************************************************************
**************    Include Headers
************************************************************************************************************/

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/************************************************************************************************************
**************    Public Definitions
************************************************************************************************************/

// none

/************************************************************************************************************
**************    Public struct/enum
************************************************************************************************************/

typedef struct
{
  size_t               ItemSize;
  uint32_t             Capacity;
  uint32_t             Head;
  uint32_t             Tail;
  uint32_t             Count;
  void                 *Buffer;

} RBUF_HandleTypeDef;

/************************************************************************************************************
**************    Public Functions
************************************************************************************************************/

RBUF_HandleTypeDef*    RBUF_Init(size_t ItemSize, uint32_t Capacity);
void                   RBUF_DeInit(RBUF_HandleTypeDef *rbuf);
bool                   RBUF_IsFull(RBUF_HandleTypeDef *rbuf);
bool                   RBUF_IsEmpty(RBUF_HandleTypeDef *rbuf);
uint32_t               RBUF_Available(RBUF_HandleTypeDef *rbuf);
uint32_t               RBUF_Used(RBUF_HandleTypeDef *rbuf);
void                   RBUF_Format(RBUF_HandleTypeDef *rbuf);
bool                   RBUF_Push(RBUF_HandleTypeDef *rbuf, const void *data);
bool                   RBUF_Pop(RBUF_HandleTypeDef *rbuf, void *data);

#ifdef __cplusplus
}
#endif
#endif

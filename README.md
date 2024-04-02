# Multitype Ring Buffer (Circular Buffer) Library for STM32
---  
## Please Do not Forget to get STAR, DONATE and support me on social networks. Thank you. :sparkling_heart:  
---   
-  Author:     Nima Askari  
-  Github:     https://www.github.com/NimaLTD
-  Youtube:    https://www.youtube.com/@nimaltd  
-  LinkedIn:   https://www.linkedin.com/in/nimaltd  
-  Instagram:  https://instagram.com/github.NimaLTD  
---
> [!CAUTION]
> This Library using malloc(), Please increase the Heap Size.
---
* Install Library from https://github.com/nimaltd/STM32-PACK/raw/main/RBUF/NimaLTD.I-CUBE-RBUF.pdsc
* Add and enable it.
* Increase the Heap Size.
* Generate Code.
---
# Watch the Video:
<div align="center">
  <a href="https://www.youtube.com/watch?v=y0S2fBNsExo"><img src="https://img.youtube.com/vi/y0S2fBNsExo/0.jpg" alt="Video"></a>
</div>
---
```
#include "rbuf.h"
.
.
.
int main(void)
{
  .
  .
  .
  uint8_t wdata = 5, rdata;
  RBUF_HandleTypeDef* rbuf = RBUF_Init(sizeof(uint8_t), 16);
  RBUF_Push(&rbuf, &wdata);
  RBUF_Pop(&rbuf, &rdata);
}
```

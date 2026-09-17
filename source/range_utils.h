#ifndef RANGE_UTILS_H
#define RANGE_UTILS_H
#include <stdint.h>
#include <stdbool.h>
#define MESH_STALE_MS 3000u
static inline uint16_t mesh_u16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static inline void mesh_p16(uint8_t *p,uint16_t v){p[0]=v;p[1]=v>>8;}
static inline uint16_t mesh_age16(uint32_t now,uint32_t then){uint32_t n=now-then;return n>65535?65535:(uint16_t)n;}
static inline bool mesh_fresh(bool valid,uint32_t now,uint32_t at){return valid && (uint32_t)(now-at)<=MESH_STALE_MS;}
static inline bool mesh_retry_due(uint32_t now,uint32_t progress,uint32_t attempt,uint32_t interval){
 return (uint32_t)(now-progress)>=interval && (uint32_t)(now-attempt)>=interval;
}
#endif

#ifndef WB_SERVICE_PROTOCOL_H
#define WB_SERVICE_PROTOCOL_H
#include <stdint.h>
#define WBM_REQUEST 0x0efb0000u
#define WBM_REPLY_BASE 0x0efc0000u
#define WBM_BOARD_F0 0xf0
#define WBM_VERSION 1
#define WBM_CAPS 0x7f
#define WBM_CONFIRM 0xa5
/* v2 routing keeps the complete v1 payload. Both selectors are in the CAN ID.
 * Requests: 0x0EFDhhii, replies: 0x0EFEhhii (hh=HW ID, ii=old data index).
 * A reply uses the address captured before APPLY/DEFAULTS changes the index. */
#define WBM_INDEX_REQUEST_BASE 0x0efd0000u
#define WBM_INDEX_REPLY_BASE 0x0efe0000u
#define WBM_INDEX_VERSION 2
static inline uint32_t wbm_index_request(uint8_t hw,uint8_t index){return WBM_INDEX_REQUEST_BASE|((uint32_t)hw<<8)|index;}
static inline uint32_t wbm_index_reply(uint8_t hw,uint8_t index){return WBM_INDEX_REPLY_BASE|((uint32_t)hw<<8)|index;}
static inline int wbm_index_match(uint32_t id,uint8_t len,const uint8_t *data,uint8_t hw,uint8_t index,uint8_t op,uint8_t transaction){return id==wbm_index_reply(hw,index)&&len==8&&data[0]==op&&data[1]==transaction;}
/* Requests always 8 bytes: op,hw,transaction,payload[5]. Replies:
 * op,transaction,result,payload[5]; reply ID identifies hardware target. */
enum { WBM_INFO=1,WBM_CONFIG=2,WBM_APPLY=3,WBM_DEFAULTS=4,WBM_IDENTIFY=5,WBM_RESET=6,WBM_DIAG1=7,WBM_DIAG2=8,WBM_RGET=9,WBM_RSET=10 };
enum { WBM_OK=0,WBM_INVALID=1,WBM_FLASH_ERROR=2,WBM_BOOTLOADER_REQUIRED=3,WBM_UNSUPPORTED=4 };
/* F0 config payload = index,sensor,pending bitrate,Tx flags,active bitrate.
 * Apply payload = index,sensor,pending bitrate,Tx flags,confirmation.
 * Tx flags bit0 standard, bit1 diagnostic. Defaults preserve pending bitrate. */
static inline int wbm_valid_config(uint8_t sensor,uint8_t bitrate,uint8_t flags){return sensor<=4&&bitrate<=1&&flags<=3;}
static inline int wbm_match(uint32_t id,uint8_t len,const uint8_t *data,uint8_t hw,uint8_t op,uint8_t transaction){return id==(WBM_REPLY_BASE|hw)&&len==8&&data[0]==op&&data[1]==transaction;}
#endif

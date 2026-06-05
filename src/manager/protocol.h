/**
 * protocol.h — Wire protocol for portd daemon ↔ client communication.
 *
 * All multi-byte integers are in HOST byte order (communication
 * is local-only: Unix socket or loopback TCP).
 * If remote TCP is ever needed, add hton/ntoh conversions.
 *
 * Request:  8 bytes fixed
 * Response: 8 bytes fixed
 *
 * This keeps the hot path simple and allocation latency minimal.
 *
 * MIT License — see LICENSE
 */
#ifndef PORTD_PROTOCOL_H
#define PORTD_PROTOCOL_H

#include <stdint.h>

#define PORTD_MAGIC_REQ  0xAB
#define PORTD_MAGIC_RESP 0xCD

/* ── Operations ─────────────────────────────────────────────────────── */
typedef enum {
    OP_ALLOC        = 1,  /* Allocate lowest free port        */
    OP_ALLOC_FROM   = 2,  /* Allocate >= desired port         */
    OP_FREE         = 3,  /* Return port to pool              */
    OP_STATUS       = 4,  /* Query pool stats (JSON response) */
    OP_PING         = 5,  /* Health check                     */
    OP_RESET        = 6,  /* Admin: reset pool to full        */
} portd_op_t;

/* ── Error codes ────────────────────────────────────────────────────── */
typedef enum {
    ERR_OK          =  0,
    ERR_EXHAUSTED   = -1,  /* pool is full / empty */
    ERR_RANGE       = -2,  /* port out of range    */
    ERR_BAD_POOL    = -3,  /* unknown pool id      */
    ERR_ALREADY_FREE= -4,  /* port already freed   */
    ERR_PROTO       = -5,  /* protocol error       */
    ERR_INTERNAL    = -6,  /* internal server error*/
} portd_err_t;

/* ── Request packet (8 bytes) ────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  magic;     /* PORTD_MAGIC_REQ */
    uint8_t  op;        /* portd_op_t      */
    uint8_t  pool_id;   /* 0..POOL_MAX_COUNT-1 */
    uint8_t  flags;     /* reserved, set 0 */
    uint16_t port;      /* for OP_ALLOC_FROM / OP_FREE */
    uint16_t reserved;  /* padding, set 0 */
} portd_req_t;

/* ── Response packet (8 bytes) ──────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  magic;     /* PORTD_MAGIC_RESP */
    uint8_t  op;        /* echoes request op */
    int16_t  error;     /* portd_err_t — 0 on success */
    uint16_t port;      /* allocated port (OP_ALLOC / OP_ALLOC_FROM) */
    uint16_t reserved;
} portd_resp_t;

/* ── Status response (variable, sent after base resp) ──────────────── */
/* For OP_STATUS, after the base 8-byte response, the server sends:    */
/*   uint16_t json_len  — length of following JSON payload             */
/*   uint8_t  json[json_len]  — UTF-8 JSON string                     */

#define PORTD_STATUS_JSON_MAX 512

#endif /* PORTD_PROTOCOL_H */

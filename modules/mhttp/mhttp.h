/*
 * http 1.1 客户端实现
*/
#ifndef MHTTP_H
#define MHTTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "mbedtls/net_sockets.h"

#define MHTTP_MAX_REQ_SIZE 1024

typedef struct mhttp_response
{
    int status_code;
    char* reason_phrase;
    char* head;
    char* body;
}mhttp_rsp;

/* 完整 URL (scheme://user:password@host:port/path?query#fragment) + 请求头 + 请求体*/
typedef struct mhttp_request
{
    char* url_scheme;   // http 或 https
    char* url_user;     // 无则置 NULL
    char* url_password; // 无则置 NULL
    char* url_host;
    uint16_t url_port;  // 若置 0（RFC 6335 规定它不应被分配），则使用默认端口
    char* url_path;

    char* head;         // 除 Host 字段外的其余头部字段。无则置 NULL
    char* body;         // 无则置 NULL
}mhttp_req;

typedef struct mhttp_handle
{
    mbedtls_net_context context;
    uint8_t is_connected;
}mhttp_hdl;

// 初始化一个连接句柄
extern void mhttp_hdl_init(mhttp_hdl* hdl);

/*
 * 发送 http 请求
 * @param hdl       连接句柄。若未连接，会先尝试连接
 * @param req       请求消息
 * @param rsp_cb    响应回调函数，置 NULL 则忽略。mhttp_rsp 会在返回后自动释放，请勿手动干预
 * @param timeout   毫秒。对于需要接收响应消息的请求，设置阻塞等待，超时自动断开连接
 * @return          成功返回 0
*/
extern int mhttp_send_req(mhttp_hdl* hdl, mhttp_req req, int (*rsp_cb)(mhttp_rsp), uint32_t timeout);


#ifdef __cplusplus
}
#endif

#endif

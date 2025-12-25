/**
 * @file time_sync_client_standalone.h
 * @brief 独立时间同步客户端头文件
 */

#ifndef TIME_SYNC_CLIENT_STANDALONE_H
#define TIME_SYNC_CLIENT_STANDALONE_H

#include <stdint.h>

/**
 * @brief 初始化独立时间同步客户端
 * @param device_id 设备ID
 * @param server_ip 服务器IP地址
 * @param server_port 服务器端口（0=使用默认端口9999）
 * @return 0=成功，-1=失败
 */
int time_sync_client_standalone_init(uint32_t device_id, 
                                      const char *server_ip, 
                                      int server_port);

/**
 * @brief 清理独立时间同步客户端
 */
void time_sync_client_standalone_cleanup(void);

#endif // TIME_SYNC_CLIENT_STANDALONE_H

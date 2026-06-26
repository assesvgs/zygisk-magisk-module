#pragma once

#include <cstdint>

int zygisk_connect();
void zygisk_disconnect();
bool zygisk_is_available();
int zygisk_request(int opcode, int pid, int32_t *out_uid, uint32_t *out_flags, int *out_fd);

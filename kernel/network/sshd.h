#ifndef CUPID_SSHD_H
#define CUPID_SSHD_H

#include "types.h"

int sshd_start(uint16_t port);
void sshd_stop(void);
int sshd_is_running(void);
uint16_t sshd_port(void);
int sshd_set_password(const char *password);

#endif

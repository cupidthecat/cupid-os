#ifndef SHELL_H
#define SHELL_H

#include "types.h"

struct shell_command {
    const char* name;
    const char* description;
    void (*func)(const char*);
};

extern const struct shell_command commands[];

void shell_run(void);

#include "isr.h"

typedef enum {
    PF_ACTION_CONTINUE = 0,
    PF_ACTION_REBOOT   = 1
} pf_action_t;

pf_action_t shell_pagefault_prompt(const struct registers* r, uint32_t cr2);

#endif
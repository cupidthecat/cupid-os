#ifndef APP_LAUNCH_H
#define APP_LAUNCH_H

#include "types.h"
#include "process.h"

typedef enum {
    APP_RUNTIME_KERNEL_SERVICE = 0,
    APP_RUNTIME_HOSTED_CUPIDC,
    APP_RUNTIME_EXTERNAL_BINARY
} app_runtime_t;

typedef enum {
    APP_ICON_STYLE_DEFAULT = 0,
    APP_ICON_STYLE_TERMINAL,
    APP_ICON_STYLE_NOTEPAD
} app_icon_style_t;

typedef struct {
    const char      *name;
    const char      *path;
    const char      *label;
    const char      *description;
    app_runtime_t    runtime;
    process_domain_t domain;
    uint8_t          desktop_visible;
    int              icon_x;
    int              icon_y;
    int              icon_type;
    uint32_t         icon_color;
    app_icon_style_t icon_style;
} app_descriptor_t;

const app_descriptor_t *app_find_by_name(const char *name);
const app_descriptor_t *app_find_by_path(const char *path);
const app_descriptor_t *app_descriptor_at(int index);
int app_descriptor_count(void);
const char *app_runtime_name(app_runtime_t runtime);

bool app_launch_by_name(const char *name, const char *args);
bool app_launch_by_path(const char *path, const char *args);
bool app_launch_registered(const app_descriptor_t *app, const char *args);
bool app_launch_cupidc_process(const char *path,
                               const char *process_name,
                               process_domain_t domain);

#endif

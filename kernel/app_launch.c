#include "app_launch.h"

#include "cupidc.h"
#include "shell.h"
#include "string.h"
#include "terminal_app.h"

static const app_descriptor_t g_apps[] = {
    {
        .name = "terminal",
        .path = "/bin/terminal.cc",
        .label = "Terminal",
        .description = "CupidOS Terminal",
        .runtime = APP_RUNTIME_KERNEL_SERVICE,
        .domain = PROCESS_DOMAIN_KERNEL,
        .desktop_visible = 1,
        .icon_x = 10,
        .icon_y = 10,
        .icon_type = 0,
        .icon_color = 0x404040u,
        .icon_style = APP_ICON_STYLE_TERMINAL
    },
    {
        .name = "notepad",
        .path = "/bin/notepad.cc",
        .label = "Notepad",
        .description = "CupidC Text Editor",
        .runtime = APP_RUNTIME_HOSTED_CUPIDC,
        .domain = PROCESS_DOMAIN_HOSTED,
        .desktop_visible = 1,
        .icon_x = 200,
        .icon_y = 250,
        .icon_type = 0,
        .icon_color = 0xFFFFAAu,
        .icon_style = APP_ICON_STYLE_NOTEPAD
    },
    {
        .name = "fm",
        .path = "/bin/fm.cc",
        .label = "My Computer",
        .description = "File Manager",
        .runtime = APP_RUNTIME_HOSTED_CUPIDC,
        .domain = PROCESS_DOMAIN_HOSTED,
        .desktop_visible = 1,
        .icon_x = 10,
        .icon_y = 130,
        .icon_type = 1,
        .icon_color = 0xFFFF00u,
        .icon_style = APP_ICON_STYLE_DEFAULT
    }
};

static void app_cupidc_process_entry(uint32_t arg) {
  const char *path = (const char *)arg;
  cupidc_jit(path);
  process_exit();
}

const app_descriptor_t *app_find_by_name(const char *name) {
  int i;

  if (!name || name[0] == '\0') {
    return NULL;
  }

  for (i = 0; i < (int)(sizeof(g_apps) / sizeof(g_apps[0])); i++) {
    if (strcmp(g_apps[i].name, name) == 0) {
      return &g_apps[i];
    }
  }

  return NULL;
}

const app_descriptor_t *app_find_by_path(const char *path) {
  int i;

  if (!path || path[0] == '\0') {
    return NULL;
  }

  for (i = 0; i < (int)(sizeof(g_apps) / sizeof(g_apps[0])); i++) {
    if (strcmp(g_apps[i].path, path) == 0) {
      return &g_apps[i];
    }
  }

  return NULL;
}

const app_descriptor_t *app_descriptor_at(int index) {
  if (index < 0 || index >= (int)(sizeof(g_apps) / sizeof(g_apps[0]))) {
    return NULL;
  }
  return &g_apps[index];
}

int app_descriptor_count(void) {
  return (int)(sizeof(g_apps) / sizeof(g_apps[0]));
}

const char *app_runtime_name(app_runtime_t runtime) {
  static const char *names[] = {
      "kernel",
      "hosted-cupidc",
      "external"
  };

  if (runtime > APP_RUNTIME_EXTERNAL_BINARY) {
    return names[APP_RUNTIME_KERNEL_SERVICE];
  }
  return names[runtime];
}

bool app_launch_cupidc_process(const char *path,
                               const char *process_name,
                               process_domain_t domain) {
  if (!path || !process_name) {
    return false;
  }

  shell_set_output_mode(SHELL_OUTPUT_GUI);
  return process_create_with_arg_ex((void (*)(void))app_cupidc_process_entry,
                                    process_name, 262144u, (uint32_t)path,
                                    domain) != 0;
}

bool app_launch_registered(const app_descriptor_t *app, const char *args) {
  (void)args;

  if (!app) {
    return false;
  }

  switch (app->runtime) {
  case APP_RUNTIME_KERNEL_SERVICE:
    terminal_launch();
    return true;
  case APP_RUNTIME_HOSTED_CUPIDC:
    return app_launch_cupidc_process(app->path, app->name, app->domain);
  case APP_RUNTIME_EXTERNAL_BINARY:
    return false;
  }

  return false;
}

bool app_launch_by_name(const char *name, const char *args) {
  return app_launch_registered(app_find_by_name(name), args);
}

bool app_launch_by_path(const char *path, const char *args) {
  const app_descriptor_t *app;

  if (!path || path[0] == '\0') {
    return false;
  }

  if (strcmp(path, "__kernel_terminal") == 0) {
    return app_launch_by_name("terminal", args);
  }
  if (strcmp(path, "__kernel_notepad") == 0) {
    return app_launch_by_name("notepad", args);
  }

  app = app_find_by_path(path);
  return app_launch_registered(app, args);
}

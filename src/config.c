/**
 * @file config.c
 * @brief Runtime configuration management
 *
 * Handles loading and saving of user preferences to zaplink.conf.
 * Configuration format is simple key=value pairs:
 *   TRANSCODE_BACKEND=software
 *   TRANSCODE_CODEC=h264
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_config.h"
#include "config.h"

/** Global configuration instance */
AppConfig app_config;

void config_load() {
    // Set defaults
    strcpy(app_config.backend, "software");
    strcpy(app_config.codec, "h264");

    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *key = line;
            char *val = eq + 1;
            // Trim newline
            val[strcspn(val, "\n")] = 0;
            
            if (strcmp(key, "TRANSCODE_BACKEND") == 0) {
                strncpy(app_config.backend, val, sizeof(app_config.backend) - 1);
            } else if (strcmp(key, "TRANSCODE_CODEC") == 0) {
                strncpy(app_config.codec, val, sizeof(app_config.codec) - 1);
            }
        }
    }
    fclose(f);
}

void config_save() {
    FILE *f = fopen(CONFIG_FILE, "w");
    if (!f) return;
    
    fprintf(f, "TRANSCODE_BACKEND=%s\n", app_config.backend);
    fprintf(f, "TRANSCODE_CODEC=%s\n", app_config.codec);
    
    fclose(f);
}

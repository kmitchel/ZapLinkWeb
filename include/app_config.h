/**
 * @file app_config.h
 * @brief Runtime application configuration for transcoding
 *
 * Manages user-configurable settings that persist across restarts.
 * Configuration is stored in zaplink.conf and can be modified via
 * the web dashboard's settings panel.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/**
 * Runtime configuration structure for transcoding preferences
 */
typedef struct {
    char backend[32];  /**< Transcoding backend: "software", "qsv", "nvenc", "vaapi" */
    char codec[32];    /**< Video codec: "h264", "hevc", "av1" */
} AppConfig;

/** Global configuration instance */
extern AppConfig app_config;

/**
 * Load configuration from CONFIG_FILE
 * Falls back to defaults ("software", "h264") if file doesn't exist
 */
void config_load(void);

/**
 * Save current configuration to CONFIG_FILE
 */
void config_save(void);

#endif

/**
 * @file config.h
 * @brief Compile-time configuration constants for ZapLinkWeb
 *
 * This header defines the fundamental configuration values that control
 * the server's behavior. These are compile-time constants; for runtime
 * configuration, see app_config.h.
 */

#ifndef CONFIG_H
#define CONFIG_H

/** Port for the HTTP server to listen on */
#define WEB_PORT 3000

/** Path to the SQLite database file (relative to working directory) */
#define DB_PATH "zaplinkweb.db"

/** Directory containing static web assets (HTML, CSS, JS) */
#define PUBLIC_DIR "./public"

/** Runtime configuration file for transcoding settings */
#define CONFIG_FILE "zaplink.conf"

#endif

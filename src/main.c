/**
 * @file main.c
 * @brief ZapLinkWeb application entry point
 *
 * Initializes all subsystems and starts the HTTP server:
 * 1. Database connection
 * 2. Runtime configuration loading
 * 3. mDNS service discovery
 * 4. DVR scheduler
 * 5. HTTP server (blocking)
 *
 * Command line options:
 *   -v    Enable verbose/debug logging
 *   -h    Show help
 */

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config.h"
#include "web.h"
#include "db.h"
#include "app_config.h"
#include "discovery.h"
#include "scheduler.h"
#include "log.h"

/** Global verbose flag - controls LOG_DEBUG visibility */
int g_verbose = 0;

static void print_banner(int port) {
    printf("\n");
    printf(COLOR_CYAN "╔═══════════════════════════════════════════╗\n");
    printf("║" COLOR_RESET "          " COLOR_GREEN " ⚡ ZapLinkWeb ⚡ " COLOR_RESET "             " COLOR_CYAN "║\n");
    printf("║" COLOR_RESET "        Stream Proxy Server v2.0          " COLOR_CYAN "║\n");
    printf("╠═══════════════════════════════════════════╣\n");
    printf("║" COLOR_RESET "  Port: " COLOR_YELLOW "%-34d" COLOR_RESET COLOR_CYAN " ║\n", port);
    printf("║" COLOR_RESET "  Mode: " COLOR_YELLOW "%-34s" COLOR_RESET COLOR_CYAN " ║\n", g_verbose ? "Verbose" : "Normal");
    printf("╚═══════════════════════════════════════════╝" COLOR_RESET "\n\n");
}

static void print_usage(const char *progname) {
    printf("Usage: %s [-v]\n", progname);
    printf("  -v    Enable verbose/debug logging\n");
}

void handle_signal(int sig) {
    (void)sig;
    LOG_INFO("MAIN", "Shutting down...");
    db_close();
    exit(0);
}

int main(int argc, char *argv[]) {
    /* Parse command line arguments */
    int opt;
    while ((opt = getopt(argc, argv, "vh")) != -1) {
        switch (opt) {
            case 'v':
                g_verbose = 1;
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    print_banner(WEB_PORT);
    fflush(stdout);

    if (!db_init()) {
        LOG_ERROR("DB", "Failed to initialize database");
        return 1;
    }
    LOG_INFO("DB", "Database initialized");

    config_load();
    LOG_INFO("CONFIG", "Backend=%s, Codec=%s", app_config.backend, app_config.codec);
    
    /* Start mDNS advertising and discovery */
    start_mdns_service(WEB_PORT);

    /* Start DVR Scheduler */
    start_scheduler();

    LOG_INFO("HTTP", "Starting web server on port %d", WEB_PORT);
    start_web_server(WEB_PORT);

    return 0;
}


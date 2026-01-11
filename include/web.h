/**
 * @file web.h
 * @brief HTTP server for API and static file serving
 *
 * The web server handles:
 * - Static file serving from PUBLIC_DIR
 * - REST API endpoints (/api/...)
 * - Video streaming via /stream/ and /transcode/
 * - Recording playback via /api/play/
 *
 * The server is single-threaded per connection, spawning a pthread
 * for each incoming request.
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

/**
 * Start the HTTP server
 *
 * This function blocks and runs the main accept() loop.
 * Each client connection is handled in a new thread.
 *
 * @param port TCP port to listen on
 */
void start_web_server(int port);

#endif

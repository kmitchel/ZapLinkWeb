/**
 * @file discovery.h
 * @brief mDNS/Avahi service discovery and advertisement
 *
 * ZapLinkWeb uses mDNS (via Avahi) for two purposes:
 * 1. Advertise itself as "_http._tcp" so clients can discover it
 * 2. Discover ZapLinkCore to obtain the stream source URL
 *
 * The discovery runs in a background thread and automatically updates
 * the core URL when ZapLinkCore is found or changes.
 */

#ifndef DISCOVERY_H
#define DISCOVERY_H

/**
 * Start mDNS services in a background thread
 *
 * This function:
 * - Advertises ZapLinkWeb as "_http._tcp" on the specified port
 * - Begins browsing for ZapLinkCore instances
 * - Resolves found services to get their IP addresses
 *
 * The function returns immediately; discovery runs asynchronously.
 *
 * @param port The HTTP port to advertise
 */
void start_mdns_service(int port);

/**
 * Get the discovered ZapLinkCore base URL
 *
 * Returns the URL of the discovered ZapLinkCore instance,
 * e.g., "http://192.168.1.5:18392" or "http://127.0.0.1:18392"
 *
 * The discovery module prioritizes:
 * 1. IPv4 localhost (127.0.0.1) - preferred
 * 2. Other IPv4 addresses
 * 3. IPv6 addresses (fallback)
 *
 * @return Static string with the URL, NULL if not yet discovered
 * @note Do NOT free the returned pointer - it's managed internally
 */
const char* get_core_base_url(void);

#endif

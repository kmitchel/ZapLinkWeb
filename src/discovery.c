/**
 * @file discovery.c
 * @brief mDNS service discovery and advertisement using Avahi
 *
 * This module handles zero-configuration networking:
 * - Advertises ZapLinkWeb as "_http._tcp" for client discovery
 * - Browses for ZapLinkCore instances to obtain stream source
 *
 * The discovery runs in a separate thread using Avahi's threaded poll.
 * When ZapLinkCore is found, its URL is stored and can be retrieved
 * via get_core_base_url().
 *
 * URL prioritization (highest to lowest):
 * 1. IPv4 localhost (127.0.0.1)
 * 2. Other IPv4 addresses
 * 3. IPv6 addresses
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#include <avahi-common/thread-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

#include "discovery.h"
#include "log.h"

/* Module state - all managed by the Avahi thread */
static AvahiThreadedPoll *threaded_poll = NULL;
static AvahiClient *client = NULL;
static AvahiEntryGroup *group = NULL;
static char core_url[256] = {0};  /**< Discovered ZapLinkCore URL */

static void resolve_callback(
    AvahiServiceResolver *r,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiResolverEvent event,
    const char *name,
    const char *type,
    const char *domain,
    const char *host_name,
    const AvahiAddress *address,
    uint16_t port,
    AvahiStringList *txt,
    AvahiLookupResultFlags flags,
    void* userdata) {

    (void) interface;
    (void) protocol;
    (void) type;
    (void) domain;
    (void) host_name;
    (void) txt;
    (void) flags;
    (void) userdata;

    if (event == AVAHI_RESOLVER_FOUND) {
        char a[AVAHI_ADDRESS_STR_MAX];
        avahi_address_snprint(a, sizeof(a), address);
        
        // We found ZapLinkCore!
        LOG_DEBUG("MDNS", "Found Service: %s at %s:%u", name, a, port);
        
        // Construct URL. Prefer IPv4 for simplicity if both come in, but we handle what we get.
        // TODO: Handle IPv6 brackets if needed.
        char new_url[256];
        if (address->proto == AVAHI_PROTO_INET6) {
             snprintf(new_url, sizeof(new_url), "http://[%s]:%u", a, port);
        } else {
             snprintf(new_url, sizeof(new_url), "http://%s:%u", a, port);
        }

        // Prioritization Logic
        int take_it = 0;
        if (strlen(core_url) == 0) {
            take_it = 1;
        } else {
            // Check current protocol (if it has brackets, it's IPv6)
            int current_is_ipv6 = (strchr(core_url, '[') != NULL);
            int new_is_ipv6 = (address->proto == AVAHI_PROTO_INET6);
            
            if (current_is_ipv6 && !new_is_ipv6) {
                // Upgrade from IPv6 to IPv4
                take_it = 1;
            } else if (!new_is_ipv6) {
                // If both are IPv4, prefer localhost?
                if (strstr(new_url, "127.0.0.1") != NULL) take_it = 1;
            }
        }

        if (take_it) {
            strncpy(core_url, new_url, sizeof(core_url));
            LOG_INFO("MDNS", "Core URL: %s", core_url);
        } else {
            LOG_DEBUG("MDNS", "Ignoring candidate: %s (Keeping %s)", new_url, core_url);
        }
    }

    avahi_service_resolver_free(r);
}

static void browse_callback(
    AvahiServiceBrowser *b,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name,
    const char *type,
    const char *domain,
    AvahiLookupResultFlags flags,
    void* userdata) {

    (void) b;
    (void) flags;
    (void) userdata;

    if (event == AVAHI_BROWSER_NEW) {
        // Check if this is the core we are looking for
        if (strcmp(name, "ZapLinkCore") == 0) {
            LOG_DEBUG("MDNS", "Discovered ZapLinkCore. Resolving...");
            if (!(avahi_service_resolver_new(client, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC, 0, resolve_callback, NULL)))
                LOG_ERROR("MDNS", "Failed to resolve service '%s': %s", name, avahi_strerror(avahi_client_errno(client)));
        }
    }
}

static void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata) {
    (void) g;
    (void) userdata;
    if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {
        LOG_INFO("MDNS", "Service 'ZapLinkWeb' established");
    }
}

static void create_services(AvahiClient *c, int port) {
    if (!group) {
        if (!(group = avahi_entry_group_new(c, entry_group_callback, NULL))) {
            LOG_ERROR("MDNS", "avahi_entry_group_new() failed: %s", avahi_strerror(avahi_client_errno(c)));
            return;
        }
    }

    if (avahi_entry_group_is_empty(group)) {
        if (avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, "ZapLinkWeb", "_http._tcp", NULL, NULL, port, "path=/", NULL) < 0) {
            LOG_ERROR("MDNS", "Failed to add _http._tcp service: %s", avahi_strerror(avahi_client_errno(c)));
            return;
        }
        if (avahi_entry_group_commit(group) < 0) {
            LOG_ERROR("MDNS", "Failed to commit entry group: %s", avahi_strerror(avahi_client_errno(c)));
            return;
        }
    }
}

static void client_callback(AvahiClient *c, AvahiClientState state, void *userdata) {
    int port = *(int*)userdata;
    if (state == AVAHI_CLIENT_S_RUNNING) {
        // Server is running, register our service
        create_services(c, port);
    } else if (state == AVAHI_CLIENT_FAILURE) {
        LOG_ERROR("MDNS", "Client failure: %s", avahi_strerror(avahi_client_errno(c)));
        avahi_threaded_poll_quit(threaded_poll);
    }
}

void start_mdns_service(int port) {
    int error;
    static int p; 
    p = port; // Keep port in safe memory for callback

    if (!(threaded_poll = avahi_threaded_poll_new())) {
        LOG_ERROR("MDNS", "Failed to create threaded poll object");
        return;
    }

    client = avahi_client_new(avahi_threaded_poll_get(threaded_poll), AVAHI_CLIENT_NO_FAIL, client_callback, &p, &error);
    if (!client) {
        LOG_ERROR("MDNS", "Failed to create client: %s", avahi_strerror(error));
        return;
    }

    // Start browsing for ZapLinkCore
    AvahiServiceBrowser *sb = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_http._tcp", NULL, 0, browse_callback, client);
    if (!sb) {
        LOG_ERROR("MDNS", "Failed to create browser: %s", avahi_strerror(avahi_client_errno(client)));
        return;
    }

    avahi_threaded_poll_start(threaded_poll);
    LOG_INFO("MDNS", "mDNS service started");
}

const char* get_core_base_url() {
    if (strlen(core_url) == 0) return NULL;
    return core_url;
}

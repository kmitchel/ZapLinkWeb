/**
 * @file channels.c
 * @brief Channel configuration parsing from channels.conf
 *
 * Parses the channels.conf file format used by dvbv5 tools.
 * Each channel block starts with [ChannelName] and contains
 * key=value pairs for VCHANNEL, SERVICE_ID, FREQUENCY, etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "channels.h"

/** Path to channels configuration file */
#define CHANNELS_CONF "channels.conf"

/**
 * Trim leading and trailing whitespace from a string in-place
 */
static char *trim(char *str) {
    if (!str) return str;
    
    /* Trim leading */
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    /* Trim trailing */
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    
    return str;
}

Channel *channels_load(int *count) {
    *count = 0;
    
    FILE *f = fopen(CHANNELS_CONF, "r");
    if (!f) return NULL;
    
    /* First pass: count channels */
    int capacity = 64;
    Channel *channels = malloc(sizeof(Channel) * capacity);
    int num_channels = 0;
    
    char line[512];
    Channel current = {0};
    int in_channel = 0;
    
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = trim(line);
        
        /* New channel block: [Name] */
        if (trimmed[0] == '[') {
            /* Save previous channel if valid */
            if (in_channel && strlen(current.number) > 0) {
                if (num_channels >= capacity) {
                    capacity *= 2;
                    channels = realloc(channels, sizeof(Channel) * capacity);
                }
                channels[num_channels++] = current;
            }
            
            /* Start new channel */
            memset(&current, 0, sizeof(current));
            in_channel = 1;
            
            /* Extract name (between [ and ]) */
            char *end = strchr(trimmed, ']');
            if (end) {
                int len = end - trimmed - 1;
                if (len > 0 && len < (int)sizeof(current.name)) {
                    strncpy(current.name, trimmed + 1, len);
                    current.name[len] = '\0';
                }
            }
        }
        /* Key = Value pairs */
        else if (in_channel) {
            char *eq = strchr(trimmed, '=');
            if (eq) {
                *eq = '\0';
                char *key = trim(trimmed);
                char *val = trim(eq + 1);
                
                if (strcmp(key, "VCHANNEL") == 0) {
                    strncpy(current.number, val, sizeof(current.number) - 1);
                } else if (strcmp(key, "SERVICE_ID") == 0) {
                    strncpy(current.service_id, val, sizeof(current.service_id) - 1);
                } else if (strcmp(key, "FREQUENCY") == 0) {
                    strncpy(current.frequency, val, sizeof(current.frequency) - 1);
                }
            }
        }
    }
    
    /* Don't forget the last channel */
    if (in_channel && strlen(current.number) > 0) {
        if (num_channels >= capacity) {
            capacity *= 2;
            channels = realloc(channels, sizeof(Channel) * capacity);
        }
        channels[num_channels++] = current;
    }
    
    fclose(f);
    
    /* Sort by channel number (natural sort) */
    /* Simple bubble sort for small arrays */
    for (int i = 0; i < num_channels - 1; i++) {
        for (int j = 0; j < num_channels - i - 1; j++) {
            /* Parse major.minor for comparison */
            int maj1, min1, maj2, min2;
            sscanf(channels[j].number, "%d.%d", &maj1, &min1);
            sscanf(channels[j+1].number, "%d.%d", &maj2, &min2);
            
            if (maj1 > maj2 || (maj1 == maj2 && min1 > min2)) {
                Channel temp = channels[j];
                channels[j] = channels[j+1];
                channels[j+1] = temp;
            }
        }
    }
    
    *count = num_channels;
    return channels;
}

void channels_free(Channel *channels, int count) {
    (void)count;  /* Not needed for simple free */
    free(channels);
}

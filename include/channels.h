/**
 * @file channels.h
 * @brief Channel configuration parsing
 *
 * Parses channels.conf to provide channel metadata for:
 * - M3U playlist generation
 * - Channel number to name mapping
 */

#ifndef CHANNELS_H
#define CHANNELS_H

/**
 * Channel information structure
 */
typedef struct {
    char name[128];       /**< Channel display name */
    char number[16];      /**< Virtual channel number (e.g., "15.1") */
    char service_id[16];  /**< ATSC service ID */
    char frequency[16];   /**< Tuning frequency */
} Channel;

/**
 * Get list of all channels from channels.conf
 *
 * @param count Output: number of channels loaded
 * @return Heap-allocated array of Channel structs (caller must free)
 *         Returns NULL if channels.conf cannot be read
 */
Channel *channels_load(int *count);

/**
 * Free channels array returned by channels_load()
 *
 * @param channels Array to free
 * @param count Number of channels in array
 */
void channels_free(Channel *channels, int count);

#endif

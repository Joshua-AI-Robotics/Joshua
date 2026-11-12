// Per-image function count; pin assignments arrive in PROFILE/CONFIGURE.
#include "stepdir_channel.h"

ChannelState g_channels[1];
jw_runtime_function_t g_functions[sizeof(g_channels) / sizeof(g_channels[0])];
jw_resource_claim_t g_stepdir_resources[3 * sizeof(g_channels) / sizeof(g_channels[0])];
const uint8_t g_num_channels = sizeof(g_channels) / sizeof(g_channels[0]);

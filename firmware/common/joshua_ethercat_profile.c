// Object dictionary values, cross-plane request ordering, retained replies and
// fail-latched software watchdogs. The caller owns snapshot/critical sections.
#include "joshua_ethercat_profile.h"

#include <string.h>

static uint16_t U16(const uint8_t* p) {
  return p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t U32(const uint8_t* p) {
  return U16(p) | ((uint32_t)U16(p + 2) << 16);
}
static void Put16(uint8_t* p, uint16_t v) {
  p[0] = v;
  p[1] = v >> 8;
}
static void Put32(uint8_t* p, uint32_t v) {
  Put16(p, v);
  Put16(p + 2, v >> 16);
}

int JoshuaEthercatProfileInit(JoshuaEthercatProfile* p,
                              const JoshuaEthercatProfileConfig* config,
                              uint32_t comm_us,
                              uint32_t target_us) {
  if (!p || !config || !comm_us || !target_us || !config->command || !config->reset ||
      !config->stop || !config->enabled || !config->identity.n_channels ||
      config->identity.n_channels > JW_MAX_CHANNELS || !config->artifact[0])
    return -1;
  bool padding = false;
  for (size_t i = 0; i < sizeof(config->artifact); ++i) {
    const unsigned char c = (unsigned char)config->artifact[i];
    if (!c)
      padding = true;
    else if (padding || c < 32 || c > 126)
      return -1;
  }
  memset(p, 0, sizeof(*p));
  p->config = *config;
  p->comm_timeout_us = comm_us;
  p->target_timeout_us = target_us;
  p->config.reset(p->config.context);
  return 0;
}
void JoshuaEthercatProfileFault(JoshuaEthercatProfile* p, uint16_t fault) {
  p->fault_latched = true;
  p->config.stop(p->config.context, fault);
}
static bool AnyEnabled(const JoshuaEthercatProfile* p) {
  for (uint8_t i = 0; i < p->config.identity.n_channels; ++i)
    if (p->config.enabled(p->config.context, i)) return true;
  return false;
}
void JoshuaEthercatProfileTick(JoshuaEthercatProfile* p, uint64_t now_us, bool operational) {
  if (now_us < p->now_us) JoshuaEthercatProfileFault(p, JOSHUA_ECAT_FAULT_STATE);
  p->now_us = now_us;
  p->operational = operational;
  if (!AnyEnabled(p)) return;
  uint16_t faults = 0;
  if (!operational) faults |= JOSHUA_ECAT_FAULT_STATE;
  if (now_us - p->last_progress_us >= p->comm_timeout_us) faults |= JOSHUA_ECAT_FAULT_COMM;
  for (uint8_t i = 0; i < p->config.identity.n_channels; ++i)
    if (p->config.enabled(p->config.context, i) &&
        now_us - p->last_target_us[i] >= p->target_timeout_us)
      faults |= JOSHUA_ECAT_FAULT_TARGET;
  if (faults) JoshuaEthercatProfileFault(p, faults);
}

static int Command(void* context, const jw_frame_t* frame, uint8_t* payload, size_t capacity) {
  JoshuaEthercatProfile* p = (JoshuaEthercatProfile*)context;
  jw_command_t command = {frame->cmd, frame->channel, frame->payload, frame->payload_len};
  if (frame->cmd == JW_CMD_IDENTIFY) {
    if (frame->channel != JW_CHANNEL_NONE || frame->payload_len)
      return jw_encode_status_payload(payload, capacity, JW_STATUS_ERROR);
    return jw_encode_identify_payload(payload, capacity, &p->config.identity);
  }
  if (frame->cmd != JW_CMD_ESTOP && frame->channel >= p->config.identity.n_channels)
    return jw_encode_status_payload(payload, capacity, JW_STATUS_ERROR);
  if (p->fault_latched && (frame->cmd == JW_CMD_ENABLE || frame->cmd == JW_CMD_SET_TARGET ||
                           frame->cmd == JW_CMD_CONFIGURE_CHANNEL))
    return jw_encode_status_payload(payload, capacity, JW_STATUS_ERROR);
  if (frame->cmd == JW_CMD_ENABLE && !p->operational)
    return jw_encode_status_payload(payload, capacity, JW_STATUS_ERROR);
  const bool channel_command = frame->channel < p->config.identity.n_channels;
  const bool was_enabled = channel_command && p->config.enabled(p->config.context, frame->channel);
  int length = p->config.command(p->config.context, &command, payload, capacity);
  if (length == 1 && payload[0] == JW_STATUS_OK) {
    if (channel_command &&
        ((!was_enabled && p->config.enabled(p->config.context, frame->channel)) ||
         frame->cmd == JW_CMD_SET_TARGET))
      p->last_target_us[frame->channel] = p->now_us;
    if (frame->cmd == JW_CMD_ESTOP) JoshuaEthercatProfileFault(p, 0);
  }
  return length;
}
static void ResetChannel(void* context) {
  JoshuaEthercatProfile* p = (JoshuaEthercatProfile*)context;
  p->config.reset(p->config.context);
  p->fault_latched = false;
}

// plane 0 is CoE, 1 is PDO. Completion is synchronous after snapshot validation:
// no unaccepted request is queued to execute after a generation-zero cancel.
static int Request(JoshuaEthercatProfile* p, const uint8_t* bytes, unsigned plane) {
  const size_t offset = plane ? JWEC_PDO_FRAME_OFFSET : JWEC_MAILBOX_FRAME_OFFSET;
  const uint32_t generation = U32(bytes + 4);
  const uint16_t length = U16(bytes + offset - 4);
  if (U32(bytes) != p->session.session_id || !p->session.session_id) return 0;
  if (!generation) return length == 0 && U16(bytes + offset - 2) == 0 ? 0 : -1;
  if (length < JW_FRAME_OVERHEAD || length > JW_MAX_FRAME_LEN || U16(bytes + offset - 2))
    return -1;
  for (size_t i = offset + length; i < offset + JW_MAX_FRAME_LEN; ++i)
    if (bytes[i]) return -1;
  jw_frame_t frame;
  if (jw_decode_frame(bytes + offset, length, &frame) != 0 ||
      frame.session_id != p->session.session_id)
    return -1;
  const bool cyclic = frame.cmd == JW_CMD_SET_TARGET || frame.cmd == JW_CMD_GET_FEEDBACK;
  const bool management = frame.cmd == JW_CMD_IDENTIFY || frame.cmd == JW_CMD_CONFIGURE_CHANNEL ||
                          frame.cmd == JW_CMD_ENABLE || frame.cmd == JW_CMD_DISABLE ||
                          frame.cmd == JW_CMD_ESTOP;
  if ((plane && !cyclic) || (!plane && !management)) return -1;
  if (generation < p->last_generation[plane]) return 0;
  if (generation == p->last_generation[plane]) {
    // Identical repeat leaves the retained response untouched, including after
    // acknowledgment. A changed frame cannot repurpose the same generation.
    return length == p->last_request_len[plane] &&
                   memcmp(bytes + offset, p->last_request[plane], length) == 0
               ? 0
               : -1;
  }
  uint8_t* response = plane ? p->input : p->mailbox;
  const size_t gen_offset = plane ? 8 : 4;
  if (U32(response + gen_offset)) return -1;  // Must acknowledge retained reply first.
  if (frame.message_id <= p->session.last_message_id) return -1;
  uint8_t frame_reply[JW_MAX_FRAME_LEN];
  const int reply_len = jw_firmware_session_process(&p->session,
                                                     bytes + offset,
                                                     length,
                                                     frame_reply,
                                                     sizeof(frame_reply),
                                                     Command,
                                                     ResetChannel,
                                                     p);
  if (reply_len <= 0) return -1;
  p->last_generation[plane] = generation;
  p->last_request_len[plane] = (uint8_t)length;
  memcpy(p->last_request[plane], bytes + offset, length);
  p->last_progress_us = p->now_us;
  memset(response, 0, plane ? JWEC_PDO_SIZE : JWEC_MAILBOX_SIZE);
  Put32(response, p->session.session_id);
  if (plane) Put32(response + 4, generation);
  Put16(response + offset - 4, (uint16_t)reply_len);
  memcpy(response + offset, frame_reply, (size_t)reply_len);
  Put32(response + gen_offset, generation);  // Publish only after complete frame.
  return 0;
}

int JoshuaEthercatProfileRead(JoshuaEthercatProfile* p, uint16_t index, uint8_t* out, size_t size) {
  if (!p || !out) return -1;
  if (index == JWEC_DESCRIPTOR_INDEX && size == JWEC_DESCRIPTOR_SIZE) {
    uint8_t descriptor[JWEC_DESCRIPTOR_SIZE] = {'J', 'W', 'E', 'C'};
    Put16(descriptor + 4, JWEC_DESCRIPTOR_VERSION);
    Put16(descriptor + 6, 2);
    Put16(descriptor + 8, 2);
    Put16(descriptor + 10, JWEC_LAYOUT_VERSION);
    Put16(descriptor + 12, JWEC_PDO_SIZE);
    Put16(descriptor + 14, JWEC_PDO_SIZE);
    Put16(descriptor + 16, JW_MAX_FRAME_LEN);
    Put32(descriptor + 18, JWEC_TRANSPORT_COE | JWEC_TRANSPORT_PDO);
    memcpy(descriptor + 22, p->config.artifact, sizeof(p->config.artifact));
    memcpy(out, descriptor, size);
    return 0;
  }
  if (index == JWEC_SESSION_INDEX && size == JWEC_SESSION_SIZE) {
    Put32(out, p->session.session_id ? JWEC_RESET_OPERATION : 0);
    Put32(out + 4, p->session.session_id);
    return 0;
  }
  if (index == JWEC_RESPONSE_INDEX && size == JWEC_MAILBOX_SIZE) {
    memcpy(out, p->mailbox, size);
    return 0;
  }
  return -1;
}
int JoshuaEthercatProfileWrite(JoshuaEthercatProfile* p,
                               uint16_t index,
                               const uint8_t* data,
                               size_t size) {
  if (!p || !data) return -1;
  if (index == JWEC_SESSION_INDEX && size == JWEC_SESSION_SIZE) {
    const uint32_t session = U32(data + 4);
    if (U32(data) != JWEC_RESET_OPERATION || !session) return -1;
    if (session == p->session.session_id)
      return 0;  // Idempotent object write, not a reset/replay hole.
    ResetChannel(p);
    jw_firmware_session_init(&p->session);
    memset(p->mailbox, 0, sizeof(p->mailbox));
    memset(p->input, 0, sizeof(p->input));
    memset(p->last_generation, 0, sizeof(p->last_generation));
    memset(p->last_request_len, 0, sizeof(p->last_request_len));
    p->last_progress_us = p->now_us;
    for (uint8_t i = 0; i < JW_MAX_CHANNELS; ++i) p->last_target_us[i] = p->now_us;
    p->session.session_id = session;  // Reset result published after safe state/history clear.
    Put32(p->input, session);
    return 0;
  }
  if (index == JWEC_ACK_INDEX && size == 4) {
    if (U32(data) && U32(data) == U32(p->mailbox + 4)) memset(p->mailbox, 0, sizeof(p->mailbox));
    return 0;
  }
  if (index == JWEC_REQUEST_INDEX && size == JWEC_MAILBOX_SIZE) return Request(p, data, 0);
  return -1;
}
int JoshuaEthercatProfilePdo(JoshuaEthercatProfile* p, const uint8_t* output, size_t size) {
  if (!p) return -1;
  if (!output || size != JWEC_PDO_SIZE) {
    JoshuaEthercatProfileFault(p, JOSHUA_ECAT_FAULT_PROTOCOL);
    return -1;
  }
  if (!p->operational) return 0;
  if (!U32(output) && !U32(output + 4)) {
    if (AnyEnabled(p)) JoshuaEthercatProfileFault(p, JOSHUA_ECAT_FAULT_STATE);
    return 0;  // Explicit host stop image. Ordinary cancellation retains session.
  }
  if (U32(output) != p->session.session_id) return 0;
  const uint32_t ack = U32(output + 8);
  if (ack && ack == U32(p->input + 8)) memset(p->input + 8, 0, sizeof(p->input) - 8);
  const int result = Request(p, output, 1);
  if (result != 0) {
    JoshuaEthercatProfileFault(p, JOSHUA_ECAT_FAULT_PROTOCOL);
    Put16(p->input + 14, 1);
  }
  return result;
}

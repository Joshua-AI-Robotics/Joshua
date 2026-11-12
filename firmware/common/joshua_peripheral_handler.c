#include "joshua_peripheral_handler.h"

#include <string.h>
static int reject(uint8_t* out, size_t cap, jw_status_t s) {
  return jw_encode_reply_payload(out, cap, s, JW_EXECUTION_REJECTED, NULL, 0);
}
void jw_peripheral_group_init(jw_peripheral_group_t* g,
                              jw_device_runtime_t* runtime,
                              jw_runtime_function_t* f,
                              jw_resource_claim_t* resources,
                              size_t cap,
                              jw_peripheral_ops_t ops) {
  memset(g, 0, sizeof(*g));
  g->runtime = runtime;
  g->resources = resources;
  g->capacity = cap;
  g->ops = ops;
  for (unsigned i = 0; i < JW_PERIPHERAL_SLOTS; ++i) {
    jw_peripheral_slot_t* s = &g->slots[i];
    s->group = g;
    s->function = &f[i];
    s->index = i;
    memset(&f[i], 0, sizeof(f[i]));
    f[i].descriptor.function_id = (uint8_t)(JW_PERIPHERAL_FIRST_ID + i);
    f[i].descriptor.device_id = (uint16_t)(JW_PERIPHERAL_FIRST_ID + i + 1);
    f[i].descriptor.profile_id = JW_PROFILE_PERIPHERAL;
    f[i].descriptor.profile_major = 1;
    f[i].descriptor.profile_minor = 0;
    f[i].descriptor.operations = 127;
    f[i].schema = jw_peripheral_schema;
    f[i].schema_len = JW_PERIPHERAL_SCHEMA_LEN;
    f[i].handler = jw_peripheral_handle;
    f[i].context = s;
  }
}
void jw_peripheral_group_reset(jw_peripheral_group_t* g) {
  // Stop every output before releasing any bus (UART end may drain its FIFO).
  for (unsigned i = 0; i < JW_PERIPHERAL_SLOTS; ++i) {
    jw_peripheral_slot_t* s = &g->slots[i];
    if (!s->configured) continue;
    uint8_t output[33], shutdown = (s->config.flags & 8) != 0;
    size_t size = 0;
    jw_status_t result = JW_STATUS_OK;
    if (s->config.kind == JW_PERIPHERAL_PWM)
      result = g->ops.execute(g->ops.context, i, JW_PERIPHERAL_DISABLE, NULL, 0, output, &size);
    else if (s->config.kind == JW_PERIPHERAL_DIGITAL && (s->config.flags & 1))
      result = g->ops.execute(g->ops.context, i, JW_PERIPHERAL_WRITE, &shutdown, 1, output, &size);
    if (result != JW_STATUS_OK) g->runtime->estopped = 1;
  }
  for (unsigned i = JW_PERIPHERAL_SLOTS; i > 0; --i) {
    jw_peripheral_slot_t* s = &g->slots[i - 1];
    if (s->configured && g->ops.release && g->ops.release(g->ops.context, i - 1) != JW_STATUS_OK)
      g->runtime->estopped = 1;
    s->configured = 0;
    s->enabled = 0;
    jw_resource_replace(
        g->resources, g->capacity, s->function->descriptor.function_id, NULL, 0, NULL);
  }
}
static jw_status_t claim(jw_peripheral_slot_t* s, const jw_peripheral_config_t* c) {
  jw_resource_claim_t q[7];
  size_t n = 0;
  const uint8_t owner = s->function->descriptor.function_id;
  const unsigned np = c->kind == JW_PERIPHERAL_SPI                                    ? 4
                      : c->kind == JW_PERIPHERAL_I2C || c->kind == JW_PERIPHERAL_UART ? 2
                                                                                      : 1;
  // A bus's native pin tuple is part of its compatibility key. Shared SPI
  // endpoints may choose different transaction speeds/modes, never new routes.
  uint32_t key = ((uint32_t)c->pins[0] << 24) | ((uint32_t)c->pins[1] << 16) |
                 ((uint32_t)c->pins[2] << 8) | c->bus;

  for (unsigned i = 0; i < np; ++i) {
    const int shared = (c->kind == JW_PERIPHERAL_SPI && i < 3) || c->kind == JW_PERIPHERAL_I2C;
    // Pin sharing checks both the bus and signal role.
    const uint32_t settings = ((uint32_t)c->kind << 24) | ((uint32_t)c->bus << 16) | i;
    q[n++] = (jw_resource_claim_t){
        owner, JW_RESOURCE_PIN, c->pins[i], (uint8_t)shared, shared ? settings : 0};
  }
  if (c->kind == JW_PERIPHERAL_PWM)
    q[n++] = (jw_resource_claim_t){owner, JW_RESOURCE_TIMER, 0, 1, c->value0};
  else if (c->kind != JW_PERIPHERAL_DIGITAL) {
    q[n++] = (jw_resource_claim_t){owner,
                                   JW_RESOURCE_BUS,
                                   (uint16_t)(c->kind * 256u + c->bus),
                                   (uint8_t)(c->kind != JW_PERIPHERAL_UART),
                                   key};
    if (c->kind == JW_PERIPHERAL_I2C) {
      q[n++] = (jw_resource_claim_t){owner,
                                     JW_RESOURCE_TIMER,
                                     (uint16_t)(0x100u + c->bus),
                                     1,
                                     (c->value0 << 3) | c->timeout_ms};
      q[n++] = (jw_resource_claim_t){
          owner, JW_RESOURCE_BUS, (uint16_t)(0x4000u + c->bus * 128u + c->value1), 0, 0};
    }
  }
  return jw_resource_replace(s->group->resources, s->group->capacity, owner, q, n, NULL);
}
int jw_peripheral_handle(void* context,
                         const jw_command_t* command,
                         const jw_profile_request_t* request,
                         uint8_t* out,
                         size_t cap) {
  jw_peripheral_slot_t* s = (jw_peripheral_slot_t*)context;
  if (!s || !command || !request || !out) return -1;
  jw_peripheral_group_t* g = s->group;
  if (cap < JW_REPLY_HEADER_LEN + JW_PERIPHERAL_MAX_TRANSFER + 1) return -1;
  if (command->channel != s->function->descriptor.function_id)
    return reject(out, cap, JW_STATUS_INVALID_CHANNEL);
  const uint8_t op = request->operation;
  const uint8_t* q = request->data;
  const size_t n = request->data_len;
  if (n && !q) return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);
  uint8_t data[JW_PERIPHERAL_MAX_TRANSFER + 1];
  size_t size = 0;
  jw_status_t status = JW_STATUS_OK;
  if (op == JW_PERIPHERAL_CONFIGURE) {
    jw_peripheral_config_t c;
    if (jw_decode_peripheral_config(q, n, &c) != JW_RESULT_OK)
      return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);
    if (g->runtime->estopped) return reject(out, cap, JW_STATUS_UNAVAILABLE);
    if (s->configured || (g->ops.bus_allowed && !g->ops.bus_allowed(g->ops.context)))
      return reject(out, cap, JW_STATUS_BUSY);
    status = g->ops.validate(g->ops.context, &c);
    if (status != JW_STATUS_OK) return reject(out, cap, status);
    status = claim(s, &c);
    if (status != JW_STATUS_OK) return reject(out, cap, status);
    status = g->ops.configure(g->ops.context, s->index, &c);
    if (status != JW_STATUS_OK) {
      g->ops.release(g->ops.context, s->index);
      jw_resource_replace(g->resources, g->capacity, command->channel, NULL, 0, NULL);
    } else {
      s->configured = 1;
      s->config = c;
      s->enabled = 0;
      memcpy(data, q, n);
      size = n;
    }
  } else {
    if (!s->configured || g->runtime->estopped) return reject(out, cap, JW_STATUS_UNAVAILABLE);
    if (op == JW_PERIPHERAL_TRANSFER) {
      if (n < 3 || q[0] != s->config.kind || n != 3u + q[1] || q[0] < 2 || q[0] > 4 ||
          (q[0] == 2
               ? (!q[1] || q[1] > 32 || q[1] != q[2])
               : (!((unsigned)q[1] + q[2]) || q[1] + q[2] > 32 || (q[0] == 4 && q[1] && q[2]))))
        return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);
    } else if (op == JW_PERIPHERAL_READ) {
      if (n || s->config.kind != JW_PERIPHERAL_DIGITAL)
        return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);
    } else if (op == JW_PERIPHERAL_WRITE) {
      if (s->config.kind == JW_PERIPHERAL_PWM ? n != 2
                                              : (s->config.kind != JW_PERIPHERAL_DIGITAL ||
                                                 !(s->config.flags & 1) || n != 1 || q[0] > 1))
        return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);
    } else if (op == JW_PERIPHERAL_ENABLE || op == JW_PERIPHERAL_DISABLE) {
      if (n || s->config.kind != JW_PERIPHERAL_PWM)
        return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);
    } else if (op != JW_PERIPHERAL_RELEASE || n)
      return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);
    if ((op == JW_PERIPHERAL_TRANSFER || op == JW_PERIPHERAL_RELEASE) && g->ops.bus_allowed &&
        !g->ops.bus_allowed(g->ops.context))
      return reject(out, cap, JW_STATUS_BUSY);
    if (op == JW_PERIPHERAL_RELEASE) {
      status = g->ops.release(g->ops.context, s->index);
      if (status == JW_STATUS_OK) {
        s->configured = 0;
        s->enabled = 0;
        jw_resource_replace(g->resources, g->capacity, command->channel, NULL, 0, NULL);
      }
    } else {
      status = g->ops.execute(g->ops.context, s->index, op, q, n, data, &size);
      if (status == JW_STATUS_BUSY || status == JW_STATUS_INVALID_PAYLOAD ||
          status == JW_STATUS_UNSUPPORTED)
        return reject(out, cap, status);  // Callbacks promise these have no side effects.
      if (status == JW_STATUS_OK && !jw_peripheral_reply_valid(op, q, n, data, size))
        status = JW_STATUS_OUTCOME_UNKNOWN;
      if (status == JW_STATUS_OK && (op == JW_PERIPHERAL_ENABLE || op == JW_PERIPHERAL_DISABLE))
        s->enabled = op == JW_PERIPHERAL_ENABLE;
    }
  }
  if (status != JW_STATUS_OK) {
    // After hardware was attempted, failure cannot safely be replayed as a new
    // action. The endpoint caches UNKNOWN, and the shared board safety stops all.
    jw_runtime_fault(g->runtime);
    status = JW_STATUS_OUTCOME_UNKNOWN;
    size = 0;
  }
  if (op != JW_PERIPHERAL_READ) {
    jw_device_status_t* state = &s->function->state;
    ++state->epoch;
    state->action_session_id = command->session_id;
    state->action_message_id = command->message_id;
    state->action_status = status;
    state->action_execution = status == JW_STATUS_OK ? JW_EXECUTION_COMPLETE : JW_EXECUTION_UNKNOWN;
    if (status == JW_STATUS_OK) {
      state->state = s->enabled ? JW_STATE_ACTIVE : JW_STATE_DISABLED;
      state->recovery = s->configured ? 0 : JW_RECOVERY_RECONFIGURE | JW_RECOVERY_EXPLICIT_ENABLE;
    }
  }
  return jw_encode_reply_payload(
      out,
      cap,
      status,
      status == JW_STATUS_OK ? JW_EXECUTION_COMPLETE : JW_EXECUTION_UNKNOWN,
      data,
      size);
}

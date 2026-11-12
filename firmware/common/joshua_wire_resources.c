#include "joshua_wire_resources.h"

#include <string.h>

void jw_resource_table_init(jw_resource_claim_t* table, size_t capacity) {
  if (table == NULL) return;
  memset(table, 0, capacity * sizeof(*table));
  for (size_t i = 0; i < capacity; ++i) table[i].owner = JW_CHANNEL_NONE;
}
jw_status_t jw_resource_replace(jw_resource_claim_t* table,
                                size_t capacity,
                                uint8_t owner,
                                const jw_resource_claim_t* candidates,
                                size_t count,
                                jw_resource_conflict_t* conflict) {
  if (table == NULL || owner == JW_CHANNEL_NONE || (count != 0 && candidates == NULL))
    return JW_STATUS_INVALID_PAYLOAD;
  if (count > capacity) return JW_STATUS_BUSY;
  size_t available = 0;
  for (size_t i = 0; i < capacity; ++i)
    if (table[i].owner == JW_CHANNEL_NONE || table[i].owner == owner) ++available;
  for (size_t i = 0; i < count; ++i) {
    const jw_resource_claim_t* c = &candidates[i];
    if (c->owner != owner || c->kind == 0 || c->shared > 1) return JW_STATUS_INVALID_PAYLOAD;
    for (size_t j = 0; j < i; ++j)
      if (c->kind == candidates[j].kind && c->id == candidates[j].id)
        return JW_STATUS_INVALID_PAYLOAD;
    for (size_t j = 0; j < capacity; ++j) {
      const jw_resource_claim_t* t = &table[j];
      if (t->owner == JW_CHANNEL_NONE || t->owner == owner || t->kind != c->kind || t->id != c->id)
        continue;
      if (t->shared && c->shared && t->settings == c->settings) continue;
      if (conflict != NULL) {
        conflict->owner = t->owner;
        conflict->kind = t->kind;
        conflict->id = t->id;
      }
      return JW_STATUS_RESOURCE_CONFLICT;
    }
  }
  if (count > available) return JW_STATUS_BUSY;
  for (size_t i = 0; i < capacity; ++i)
    if (table[i].owner == owner) table[i].owner = JW_CHANNEL_NONE;
  size_t next = 0;
  for (size_t i = 0; i < capacity && next < count; ++i)
    if (table[i].owner == JW_CHANNEL_NONE) table[i] = candidates[next++];
  return JW_STATUS_OK;
}

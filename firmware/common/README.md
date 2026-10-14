# Shared firmware protocol

The `joshua_wire` codec implements JoshuaWire 0.1.0 / wire revision 3:
CRC-protected complete frames carry session and message IDs. Pure C firmware
session and endpoint helpers enforce RESET, duplicate handling and command
dispatch without device I/O. Semantic payload codecs live in
`joshua_wire_commands`; actuator, sensor and I/O value/interface definitions
live in their respective `joshua_wire_{actuator,sensor,io}.h` headers.

IDENTIFY advertises generic channel descriptors rather than treating every
channel as a motor drive. Each descriptor contains a unique channel ID, kind,
32-bit capability bitmap and the interface selected by kind. Channel IDs may
be sparse; `0xff` remains reserved for board/session commands. Only the first
`n_channels` entries are sent; decoding clears unused entries. The explicit
wire layout is `[board:u8][name:16][count:u8]`, then `count` copies of
`[id:u8][kind:u8][capabilities:u32le][interface:u8]`. Its length is
`18 + 7 * count`, from 18 to 74 bytes. No C padding, enum sizes or union storage
is serialized. Unknown capability bits are preserved; unknown kinds/interfaces
and duplicate/reserved channel IDs are rejected.

| Kind | Interface | Operations and payloads |
| --- | --- | --- |
| Actuator | `jw_drive_t` | SET_TARGET / GET_FEEDBACK; STEP_DIR configuration |
| Sensor | SCALAR_F32 | READ_CHANNEL: empty request, one f32le sample in native units |
| I/O | DIGITAL / ANALOG_F32 | READ_CHANNEL / WRITE_CHANNEL: a 0/1 byte or f32le value in native units; writes return status |

Channel handlers resolve descriptors by ID and use `jw_channel_supports_command`
to check kind, interface and capabilities before decoding device payloads.
Configuration follows the advertised interface; only STEP_DIR configuration is
currently defined. Advertise CONFIGURE or STREAM only after adding matching
schemas and handlers. STREAM is reserved: this request-response endpoint does
not implement unsolicited samples. Target-mode support and device safety policy
remain handler responsibilities. IDENTIFY, ESTOP and RESET_SESSION remain shared
board/session commands.

This is an explicit compatibility break from wire revision 2's drive-only
IDENTIFY. Revision 3 frames allow 96 bytes, so all eight channel descriptors fit
in one response (89 bytes including framing). The EtherCAT contract uses layout
2 with 108-byte mailbox and 112-byte PDO envelopes, derived from the same frame
limit. Its compatibility descriptor retains schema/version 1 and advertises the
new protocol/layout/sizes. The endpoint rejects revision 1 and 2 frames and never
auto-downgrades. Existing hardware consumers still select the separate, unchanged
JW1 codec until the shared engine and firmware migration stages land. No new
hardware handlers or automatic firmware flashing are introduced.

The detailed transport/session contract is in
[the board/comm plan](../../docs/BOARD_COMM_SEPARATION_PLAN.md). Golden-frame,
mixed-channel discovery, payload and session tests do not open hardware.

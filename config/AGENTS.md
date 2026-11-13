# AGENTS.md — Config

Also follow the root [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). Read [README.md](README.md)
for schema, preset, and validation behavior. These rules apply throughout
`config/`.

## Validation ownership

- All static configuration validation rules belong in
  [validation.cc](validation.cc), exposed through `config::ValidateConfig` in
  [validation.h](validation.h). This includes field ranges, required fields,
  supported combinations, references, node assignments, topics, and resource
  ownership constraints. Keep focused private helpers in that same `.cc` file;
  do not distribute config validators across driver, factory, ROS 2, launcher,
  UI, or tool directories.
- Keep parsing, validation, and construction separate. `LoadConfig` parses the
  protobuf; `ValidateConfig` checks its semantics; factories and nodes consume
  validated configuration. Launchers, direct node entrypoints, CLIs, and external
  tools must use the same validator instead of defining their own acceptance
  rules.
- Validation must work without hardware. Do not construct drivers, open buses,
  initialize ROS, probe devices, start workers, or modify the config. Filesystem
  metadata reads for resource identity must preserve offline validation when
  the configured device is absent.
- Runtime checks still belong at their operating boundary: device availability,
  negotiated capabilities, command validity, and safe I/O cannot be established
  from configuration alone. Keep those defenses without turning them into a
  second static config-validation path.
- Reject invalid configuration explicitly. Do not silently clamp values, change
  resource assignments, or fall back to another device or implementation.
  Errors should identify the configured component, field, and failed constraint.

## Schema and presets

- Use the existing protobuf schema and `.pbtxt` presets. Reuse a field only when
  its meaning matches; add a field when the requirement introduces a distinct
  concept. Do not introduce a parallel JSON/YAML config, hidden code defaults
  for a particular robot, or tool-specific copies of hardware declarations.
- Define units, valid ranges, and omitted-value behavior in schema comments.
  Use explicit presence when omitted differs from zero or false, and `oneof`
  for mutually exclusive choices. Keep IDs, names, and references unambiguous.
- Preserve protobuf compatibility. Never reuse field numbers or enum values;
  reserve removed numbers and names. Explain changes to defaults or semantics
  and migrate the affected presets and consumers together.
- Keep presets focused and reviewable. Start from the nearest relevant preset,
  keep hardware endpoints and operational limits explicit, and avoid unrelated
  formatting or configuration changes. Validation does not establish hardware
  readiness or authorize running a preset.

## Verification

- Cover every new validation rule in [validation_test.cc](validation_test.cc)
  with a valid case, the relevant invalid case, and meaningful boundaries.
  Update affected preset-validation and CLI tests when their contracts change.
  Validate checked-in presets affected by schema or default changes without
  launching Joshua.

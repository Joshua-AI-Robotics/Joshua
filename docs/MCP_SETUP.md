# Local MCP setup

From the repository root:

```bash
./scripts/joshua_mcp.sh setup
```

Setup checks your computer, prepares the read-only server inside Docker,
verifies component discovery, and registers a detected AI IDE at user scope.
If several clients are installed, choose one. If yours has no adapter, setup
prints standard stdio settings to enter in its MCP settings. Reconnect the IDE
and complete any approval it requires, then ask it to list supported components.
Saved registration alone does not establish an active IDE connection.

## Requirements

The initial preflight accepts x86-64 Ubuntu 22.04 and 24.04 hosts with Python 3,
Docker Engine and Compose v2 or later. Other Ubuntu releases are unqualified;
other operating systems and ARM64 are outside this initial path. Container
`u22`/`u24` selects ROS Humble/Jazzy, independently of the host release. Local
execution evidence must name the host release; passing both container stacks
does not qualify two host releases.

No GPU, host ROS, Bazel, pip dependencies or host CUDA toolkit is needed for MCP
inspection. AI inference and simulators have separate workload-specific GPU
requirements; the current accelerated path uses NVIDIA. See
[Getting started](GETTING_STARTED.md) for those environments.

Working Docker installations are preserved. Missing tooling can lead to an
explicit offer to run the existing CPU host bootstrap. It can remove conflicting
Docker packages, enable the daemon, change group membership and install a PTP
udev rule; setup explains these changes before asking. You may need to log out
and back in. Permission errors or a stopped daemon do not trigger a reinstall.
Rerun the same command after resolving a prerequisite.

## Daily use and updates

`setup` reuses a current prepared generation and matching registration.
The first preparation can download large images and build dependencies; its
duration is reported. `./scripts/joshua_mcp.sh doctor` diagnoses host, Docker,
prepared artifacts and source freshness without installing anything.

Use `./scripts/joshua_mcp.sh prepare --stack u24` to prepare Jazzy. Supply the
same `--stack u24` to setup and other commands. After source changes, run
`prepare` and reconnect the IDE. After changing Dockerfiles or image dependencies,
use `prepare --rebuild-image`. Preparation checks the protocol before activating
new artifacts and preserves the previous generation on failure.

The prepared generation pins its image ID, local Unix Docker endpoint and daemon
identity. Remote Docker endpoints are rejected. Moving the checkout or replacing
the daemon requires preparing and registering that location again. Conflicting
existing registrations are reported, never silently replaced. Resolve the
conflict in the IDE's MCP settings before retrying. Setup checks the selected
client for conflicts before preparing a replacement generation, so a rejected
registration leaves the current prepared configuration intact. This applies
across AI IDEs and manual setup: setup does not implicitly replace a different
prepared config root shared by existing clients. For a deliberate change,
coordinate all affected registrations and run `prepare` with the new settings
before rerunning setup. Doctor compares
saved registrations with the prepared checkout, stack and config root as well
as the current client settings; a mismatch requires attention.

To inspect configurations elsewhere, pass an absolute `--config-root` directory
to `setup` and `prepare`. That actual directory is mounted read-only; MCP paths
are relative to it. For example, `check --config so100/teleoperate.pbtxt` validates
that candidate without opening its configured devices. A stale inspector keeps
its prepared behavior until you prepare again; doctor reports its source revision.

## Other AI applications

Use `./scripts/joshua_mcp.sh setup --client manual` to prepare and print a
client-neutral `mcpServers` JSON example. It exits with status 2 while registration
is pending. The command is an absolute path to this checkout's launcher with
`serve` and explicit workspace, stack and config-root arguments; no working
directory dependency is needed. Map that entry to your application's stdio
settings. Applications with workspace editing can follow the
[guided integration skill](skills/joshua-guided-integration/SKILL.md); other MCP
clients can inspect and receive guidance.

Setup uses the existing skill installer without forcing replacements. If a
skill link conflicts or your IDE does not index repo skills, ask the assistant
to read that same guide directly. Client trust, organization policy and
project-level overrides still apply. After switching projects, verify discovery
again; project settings may shadow the user registration.

## Recovery and limits

Use `check` for a bounded initialize → tool discovery → component inspection
smoke test. `serve` only starts the retained MCP artifacts: it never installs,
compiles or launches Joshua. Containers have no GPU reservation, physical device
mount, host network or privileged mode. Successful inspection or config
validation does not demonstrate physical connectivity or readiness.

Ordinary EOF and termination clean up the session container. Abrupt kills or
daemon outages can leave containers behind. Doctor lists containers belonging to
this checkout for inspection; they may belong to another live client session.
Remove only a confirmed abandoned container by its exact ID, never all Docker
containers. Old artifact generations remain available and are not pruned while
clients might still use them.

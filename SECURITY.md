# Security Policy

## Supported versions

Joshua is under active development on the `develop` branch. Security fixes are
applied to `develop` first. We do not maintain long-term support for older
commits unless tagged as a release.

| Version / branch | Supported          |
|------------------|--------------------|
| Latest release ([`VERSION`](VERSION)) | Yes |
| `develop`        | Yes (active development) |
| Older tagged releases | Best effort only |
| Untagged commits | No                 |

## Reporting a vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

Report privately using:

- **GitHub (preferred):** [Report a vulnerability](https://github.com/Joshua-AI-Robotics/Joshua/security/advisories/new)

Include:

- Description of the issue and potential impact
- Steps to reproduce (config preset, commands, environment)
- Whether you tested in simulation, Docker, or on physical hardware
- Your suggested severity (if known)

Reports that could cause unintended robot motion or physical harm are treated as
**high priority**.

We will acknowledge receipt within **7 days** and aim to provide an initial
assessment within **14 days**.

## Safe testing

- Test only on systems and robots you own or have explicit permission to use.
- Prefer **simulation or Docker** over live hardware when validating exploits.
- Do not disrupt other users, lab equipment, or production deployments.

## Scope

### In scope

- Vulnerabilities in Joshua source code, configs, launcher, node generator,
  ROS 2 integration, web UI (`ui/`), Docker/setup scripts, and official presets
- Unsafe network exposure or missing authentication on control interfaces
- Memory safety or parsing issues in protobuf / packet handling
- Exploitable issues in dependencies when used by Joshua as shipped

### Out of scope

- General hardening advice without a demonstrable vulnerability
- Physical robot safety (guarding, e-stop wiring, workspace design)
- Vulnerabilities in upstream projects (ROS 2, Bazel, Isaac Lab, etc.) unless
  Joshua's integration creates a new exploitable condition
- Issues requiring physical access to an already secured machine

## Disclosure

We follow coordinated disclosure:

1. Reporter submits privately
2. Maintainers confirm and develop a fix
3. Fix is merged and released (or a documented mitigation is issued)
4. Public disclosure after a patch is available, typically within **90 days**
   (sooner for critical issues)

We credit reporters in release notes or advisories unless they prefer to stay
anonymous.

## Security hardening (operators)

Joshua can control real hardware. Operators should:

- Run the web UI and ROS graph on trusted networks only
- Do not expose control ports to the public internet without authentication
- Review `.pbtxt` presets before deployment
- Keep Docker images and system packages updated

See [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) for deployment guidance.

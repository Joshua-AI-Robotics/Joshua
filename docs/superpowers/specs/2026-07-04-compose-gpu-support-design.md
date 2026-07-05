# GPU-First Docker Development

## Goal

Make NVIDIA GPU access the default for Joshua developer shells and launcher runs while preserving explicit CPU fallback and GPU-independent tests, builds, UI, and CI.

## Design

- Request all available NVIDIA GPUs for `joshua-u22`, `joshua-u24`, `run-u22`, and `run-u24` directly in `docker-compose.yml`.
- Explicitly clear inherited GPU reservations from test and package-build services so GitHub Actions and CPU-only builders remain portable.
- Add `docker-compose.cpu.yml` as the opt-in CPU override for developer shells and launcher task services.
- Keep CUDA user-space libraries in the model's Python environment. The host supplies only the NVIDIA driver and NVIDIA Container Toolkit.
- Make `scripts/setup.sh` install or validate NVIDIA Container Toolkit by default after Docker is available. Add `--cpu` to skip NVIDIA setup, and do not install the host CUDA toolkit.
- Document exact shell and launcher commands and explain the host/container responsibility split.

## Commands

```bash
sudo ./scripts/setup.sh
docker compose run --rm joshua-u22 nvidia-smi
CONFIG=config/config_preset/so100/random_noise.pbtxt docker compose run --rm run-u22

# Explicit CPU fallback
sudo ./scripts/setup.sh --cpu
docker compose -f docker-compose.yml -f docker-compose.cpu.yml run --rm joshua-u22
```

## Failure Behavior

- Default developer shell and launcher commands fail clearly when the host lacks a working NVIDIA driver or NVIDIA Container Toolkit.
- CPU-only hosts use `scripts/setup.sh --cpu` and the CPU Compose overlay.
- Test, build, UI, and CI services do not request a GPU.
- Default `scripts/setup.sh` rejects systems where `nvidia-smi` cannot detect a GPU and points CPU-only users to `--cpu`.

## Verification

- A static test requires GPU reservations on default developer services, no reservations on tests/builds, a working CPU override, documented commands, and setup support.
- Base, development, Isaac, and CPU Compose configurations must validate.
- Existing u22 and u24 Bazel test services must continue to pass without a GPU.
- Default developer services must expose the device to `nvidia-smi`.
- PyTorch must report CUDA available and complete a real tensor operation on the GPU.

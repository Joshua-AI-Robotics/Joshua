SHELL := /bin/bash

OS ?= u22
CPU ?= x86
TARGET ?= //launcher:joshua_main_pkg
CONFIG ?=

COMPOSE := docker compose
COMPOSE_DEV := docker compose -f docker-compose.yml -f docker-compose.dev.yml
COMPOSE_ISAAC := docker compose -f docker-compose.yml -f docker-compose.isaac.yml

SERVICE_u22 := joshua-u22
SERVICE_u24 := joshua-u24
SERVICE := $(SERVICE_$(OS))

ifeq ($(SERVICE),)
$(error Unsupported OS "$(OS)". Use OS=u22 or OS=u24)
endif

.PHONY: help shell-u22 shell-u24 test-u22 test-u24 run-u22 run-u24 run-isaac-u22 run-isaac-u24 build ui ui-dev down compose-config

help:
	@printf '%s\n' 'Joshua Docker-first entrypoints'
	@printf '%s\n' ''
	@printf '%s\n' 'Development shells:'
	@printf '%s\n' '  make shell-u22                       Open Ubuntu 22.04 / ROS2 Humble shell'
	@printf '%s\n' '  make shell-u24                       Open Ubuntu 24.04 / ROS2 Jazzy shell'
	@printf '%s\n' ''
	@printf '%s\n' 'Run and test:'
	@printf '%s\n' '  make test-u22                        Run discovered Bazel tests in u22 container'
	@printf '%s\n' '  make test-u24                        Run discovered Bazel tests in u24 container'
	@printf '%s\n' '  make run-u22 CONFIG=path/to.pbtxt     Run joshua_main in u22 container'
	@printf '%s\n' '  make run-u24 CONFIG=path/to.pbtxt     Run joshua_main in u24 container'
	@printf '%s\n' '  make run-isaac-u24 CONFIG=path.pbtxt  Run Isaac preset with mounted Isaac Lab paths'
	@printf '%s\n' ''
	@printf '%s\n' 'Build artifacts:'
	@printf '%s\n' '  make build OS=u22 CPU=x86 TARGET=//launcher:joshua_main_pkg'
	@printf '%s\n' '  make build OS=u24 CPU=arm64 TARGET=//launcher:joshua_main_pkg'
	@printf '%s\n' ''
	@printf '%s\n' 'UI and services:'
	@printf '%s\n' '  make ui                              Build and run production UI'
	@printf '%s\n' '  make ui-dev                          Run UI dev server and Zenoh bridge'
	@printf '%s\n' '  make down                            Stop Docker Compose services'
	@printf '%s\n' ''
	@printf '%s\n' 'Host bootstrap:'
	@printf '%s\n' '  sudo ./scripts/setup.sh               Install/check Docker host tooling only'

shell-u22:
	$(COMPOSE) run --rm joshua-u22

shell-u24:
	$(COMPOSE) --profile u24 run --rm joshua-u24

test-u22:
	$(COMPOSE) run --rm joshua-u22 bash -lc 'tests=$$(bazel query "kind(\".*_test rule\", //...)"); if [[ -z "$$tests" ]]; then echo "No Bazel test targets found."; exit 0; fi; bazel test --config=u22 --config=x86-base --@rules_python//python/config_settings:python_version=3.10 $$tests'

test-u24:
	$(COMPOSE) --profile u24 run --rm joshua-u24 bash -lc 'tests=$$(bazel query "kind(\".*_test rule\", //...)"); if [[ -z "$$tests" ]]; then echo "No Bazel test targets found."; exit 0; fi; bazel test --config=u24 --config=x86-base --@rules_python//python/config_settings:python_version=3.12 $$tests'

run-u22:
	$(COMPOSE) run --rm joshua-u22 bazel run --config=u22 --config=x86-base --@rules_python//python/config_settings:python_version=3.10 //launcher:joshua_main -- $(if $(CONFIG),--config $(CONFIG),)

run-u24:
	$(COMPOSE) --profile u24 run --rm joshua-u24 bazel run --config=u24 --config=x86-base --@rules_python//python/config_settings:python_version=3.12 //launcher:joshua_main -- $(if $(CONFIG),--config $(CONFIG),)

run-isaac-u22:
	$(COMPOSE_ISAAC) run --rm joshua-u22 bazel run --config=u22 --config=x86-base --@rules_python//python/config_settings:python_version=3.10 //launcher:joshua_main -- $(if $(CONFIG),--config $(CONFIG),)

run-isaac-u24:
	$(COMPOSE_ISAAC) --profile u24 run --rm joshua-u24 bazel run --config=u24 --config=x86-base --@rules_python//python/config_settings:python_version=3.12 //launcher:joshua_main -- $(if $(CONFIG),--config $(CONFIG),)

build:
	./scripts/build.py --os=$(OS) --cpu=$(CPU) $(TARGET)

ui:
	$(COMPOSE) --profile production up --build joshua-ui

ui-dev:
	$(COMPOSE_DEV) up zenoh-bridge-ros2dds joshua-ui-dev

down:
	$(COMPOSE_DEV) --profile production --profile u24 --profile arm64 --profile mac down

compose-config:
	$(COMPOSE) --profile u24 --profile arm64 --profile production config >/dev/null
	$(COMPOSE_DEV) --profile u24 --profile arm64 --profile production config >/dev/null

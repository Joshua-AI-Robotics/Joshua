SHELL := /bin/bash

OS ?= u22
CPU ?= x86
TARGET ?= //launcher:joshua_main_pkg
CONFIG ?=

COMPOSE := docker compose
COMPOSE_DEV := docker compose -f docker-compose.yml -f docker-compose.dev.yml
COMPOSE_ISAAC := docker compose -f docker-compose.yml -f docker-compose.isaac.yml

.PHONY: help shell-u22 shell-u24 test-u22 test-u24 run-u22 run-u24 run-isaac-u22 run-isaac-u24 build ui ui-dev down compose-config

help:
	@printf '%s\n' 'Joshua Docker-first entrypoints (optional Make shorthand)'
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
	$(COMPOSE) run --rm test-u22

test-u24:
	$(COMPOSE) run --rm test-u24

run-u22:
	CONFIG="$(CONFIG)" $(COMPOSE) run --rm run-u22

run-u24:
	CONFIG="$(CONFIG)" $(COMPOSE) run --rm run-u24

run-isaac-u22:
	CONFIG="$(CONFIG)" $(COMPOSE_ISAAC) run --rm run-u22

run-isaac-u24:
	CONFIG="$(CONFIG)" $(COMPOSE_ISAAC) run --rm run-u24

build:
	TARGET="$(TARGET)" $(COMPOSE) run --rm build-$(OS)-$(CPU)

ui:
	$(COMPOSE) --profile production up --build joshua-ui

ui-dev:
	$(COMPOSE_DEV) up zenoh-bridge-ros2dds joshua-ui-dev

down:
	$(COMPOSE_DEV) --profile production --profile u24 --profile arm64 --profile mac down

compose-config:
	$(COMPOSE) --profile u24 --profile arm64 --profile production --profile tasks config >/dev/null
	$(COMPOSE_DEV) --profile u24 --profile arm64 --profile production --profile tasks config >/dev/null
	$(COMPOSE_ISAAC) --profile u24 --profile arm64 --profile tasks config >/dev/null

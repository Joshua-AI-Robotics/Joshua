# <Subsystem>

Describe its purpose, how it fits Joshua, and the context an agent needs. Link
ancestor instructions and neighboring subsystem guides relative to this file.

## Architecture contract

Define owned responsibilities, allowed dependencies, resource/state/lifecycle
ownership, and boundaries owned elsewhere. Link existing shared contracts.

## Implementation rules

List local invariants and recurring failure modes. Keep robot setup in protobuf
configuration and extend the existing interfaces and factories.

## Contents

Map the important files and identify where to make common changes.

## Usage

Give concrete supported commands and examples. Explain prerequisites and limits.
State whether any command opens physical devices or moves actuators, and link
the root authorization rules. Never imply a hardware path is simulation-only.

## Verification

List focused tests, container entrypoints, failure cases, and coverage limits.
Separate software evidence from physical qualification.

## Directory navigation

Link child `AGENTS.md` files when they have distinct local context. Remove this
section when there are no child guides.

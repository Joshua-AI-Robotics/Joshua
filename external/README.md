# External dependencies (submodules)

This directory contains external dependencies vendored as Git submodules. We pin them to exact commits for reproducible builds across native (x86_64) and ARM64 (Jetson) configurations.

## rules_ros2 submodule (pinned)

- Path: `external/rules_ros2`
- Remote (fork): `git@github.com:hsmoon5458/rules_ros2.git`
- Branch: `joshua-pinned-commit-for-cross-compile`
- Pinned commit: `533bcec9ff9ec425a7330f2df38cb7a62636e8d8`

### Why it is pinned

We require a known-good snapshot with fixes for:
- Python generator None-guard in `ros2/interfaces.bzl` (avoids depset errors on ARM64)
- CMake try-compile setting in `repositories/cyclonedds.BUILD.bazel`:
  `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` (prevents toolchain link failures during cross-compile)

These changes are recorded in the fork and the parent repo points the submodule to that exact commit.

### How collaborators sync submodules

After cloning or switching branches:

```bash
git submodule update --init --recursive
```

### How to verify the pinned pointer

From the parent repo (shows gitlink entry):

```bash
git ls-tree HEAD external/rules_ros2
git rev-parse :external/rules_ros2
```

From inside the submodule (shows worktree HEAD):

```bash
cd external/rules_ros2
git rev-parse HEAD
```

These SHAs should match the pinned commit listed above.

### How to update the pinned commit (maintainers)

1) Make and push changes in the submodule (to the fork):

```bash
cd external/rules_ros2
git checkout -b joshua-update-$(date +%Y%m%d)
# edit files, then:
git add <paths>
git commit -m "Update rules_ros2 snapshot for Joshua"
git push -u fork HEAD  # remote "fork" should point to hsmoon5458/rules_ros2
NEW_SHA=$(git rev-parse HEAD)
cd -
```

2) Update the parent repo pointer to the new commit:

```bash
git add external/rules_ros2
git commit -m "Pin rules_ros2 to ${NEW_SHA} (fork update)"
git push
```

3) Confirm `.gitmodules` points at the fork (not upstream):

```bash
git config -f .gitmodules --get submodule.external/rules_ros2.url
```

### Ensuring access and stability

- The fork (`hsmoon5458/rules_ros2`) should be accessible to collaborators/CI (public or with proper access). To check via GitHub CLI:

```bash
gh repo view hsmoon5458/rules_ros2 --json visibility,isPrivate
```

- The parent repo commits the submodule pointer, so branches/PRs cannot change the submodule unless they explicitly change the gitlink or `.gitmodules`.

### Do not ignore this directory

Ensure `.gitignore` does NOT ignore `external/` so the submodule pointer is tracked. The repository is configured accordingly.



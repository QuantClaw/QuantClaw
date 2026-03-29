# Scripts

Run helper scripts from the repository root.

## `scripts/build.sh`

- Cross-platform CMake/Ninja build wrapper
- Supports `--debug`, `--tests`, `--asan`, `--tsan`, `--ubsan`, and `--no-sidecar`
- On macOS, installs missing Homebrew dependencies and exports the Homebrew prefixes required for OpenSSL and curl discovery

## `scripts/install.sh`

- `--user` installs `quantclaw` into `~/.quantclaw/bin` and is the default on macOS
- `--system` installs into `/usr/local/bin` and is the default on Linux
- `--binary PATH` reuses an existing binary instead of rebuilding
- Runs `onboard --quick` after installation and installs the platform service definition unless `--skip-service` is provided

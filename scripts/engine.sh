# Sourced by scripts that need a container engine. Podman and Docker are
# both supported: set CONTAINER_ENGINE=podman|docker to force one, otherwise
# auto-detect prefers podman (the project direction) and falls back to
# docker. "Running" means the engine's daemon/machine answers `info`.
#
# Compose note: `podman compose` delegates to an external provider; install
# docker-compose (brew install docker-compose) rather than podman-compose,
# which mishandles the healthcheck conditions and profiles this repo uses.

# The macOS pkg installs to /opt/podman/bin and only login shells pick up
# its /etc/paths.d entry; non-login contexts (cron, editor tasks) need it
# added at source time — callers use the engine name outside this file.
if [[ -d /opt/podman/bin ]] && ! command -v podman >/dev/null 2>&1; then
  PATH="/opt/podman/bin:$PATH"
fi

container_engine() {
  if [[ -n "${CONTAINER_ENGINE:-}" ]]; then
    echo "${CONTAINER_ENGINE}"
    return 0
  fi
  if command -v podman >/dev/null 2>&1 && podman info >/dev/null 2>&1; then
    echo podman
    return 0
  fi
  if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
    echo docker
    return 0
  fi
  echo "error: no running container engine found (start podman machine or docker)" >&2
  return 1
}

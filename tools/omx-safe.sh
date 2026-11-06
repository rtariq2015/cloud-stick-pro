#!/usr/bin/env bash
# Project entrypoint: preserve Codex approvals and sandboxing for OMX workers.
set -euo pipefail
umask 077
omx_project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd -- "$omx_project_root"
export OMX_AUTO_UPDATE=0
export OMX_TEAM_WORKER_CLI=codex
export OMX_TEAM_WORKER_CLI_MAP=codex
export OMX_TEAM_INHERIT_LEADER_FLAGS=0
export OMX_TEAM_WORKER_LAUNCH_ARGS='--ask-for-approval on-request --sandbox workspace-write'
for omx_arg in "$@"; do
  case "$omx_arg" in
    --madmax|--madmax-spark|--yolo|--dangerously-bypass-approvals-and-sandbox)
      printf '%s\n' 'This project entrypoint preserves approvals and sandboxing; bypass flags are disabled.' >&2
      exit 2
      ;;
  esac
done
exec omx "$@"

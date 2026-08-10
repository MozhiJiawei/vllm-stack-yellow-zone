#!/usr/bin/env bash
set -Eeuo pipefail

workspace=/root/l00933108
target="$workspace/vllm-stack-yellow-zone"
origin_url=https://github.com/MozhiJiawei/vllm-stack-yellow-zone.git
proxy_profile=/etc/profile.d/vllm-stack-proxy.sh

if [[ -r "$proxy_profile" ]]; then
  # shellcheck source=/dev/null
  source "$proxy_profile"
fi

if ! command -v git >/dev/null 2>&1; then
  echo 'ERROR: git is not installed.' >&2
  exit 1
fi

IFS= read -r signed_url
if [[ "$signed_url" != https://* ]]; then
  echo 'ERROR: expected a signed HTTPS OSS URL on stdin.' >&2
  exit 1
fi

install -d -m 0700 "$workspace/.sync"
bundle=$(mktemp "$workspace/.sync/repository.XXXXXX.bundle")
incoming=''
verification_repo=''

cleanup() {
  rm -f -- "$bundle"
  if [[ -n "$incoming" && "$incoming" == "$workspace"/.incoming.* ]]; then
    rm -rf -- "$incoming"
  fi
  if [[ -n "$verification_repo" && "$verification_repo" == "$workspace"/.sync/verify.*.git ]]; then
    rm -rf -- "$verification_repo"
  fi
}
trap cleanup EXIT

echo '=== DOWNLOAD GIT BUNDLE FROM PRIVATE OSS ==='
curl --http1.1 -L --fail --show-error \
  --retry 8 --retry-delay 2 --retry-all-errors \
  --connect-timeout 30 --max-time 1800 \
  -o "$bundle" "$signed_url"

echo '=== VERIFY GIT BUNDLE ==='
verification_repo=$(mktemp -d "$workspace/.sync/verify.XXXXXX.git")
git init --bare "$verification_repo" >/dev/null
git -C "$verification_repo" bundle verify "$bundle"

if [[ ! -e "$target" ]]; then
  incoming=$(mktemp -d "$workspace/.incoming.XXXXXX")
  git -C "$incoming" init
  git -C "$incoming" fetch "$bundle" refs/heads/main:refs/heads/main
  git -C "$incoming" symbolic-ref HEAD refs/heads/main
  git -C "$incoming" reset --hard refs/heads/main
  git -C "$incoming" remote add origin "$origin_url"
  mv "$incoming" "$target"
  incoming=''
  action=created
else
  if [[ ! -d "$target/.git" ]]; then
    echo "ERROR: target exists but is not a Git repository: $target" >&2
    exit 1
  fi

  if [[ -n "$(git -C "$target" status --porcelain --untracked-files=all)" ]]; then
    echo 'ERROR: remote worktree is dirty; refusing to overwrite it.' >&2
    git -C "$target" status --short >&2
    exit 1
  fi

  git -C "$target" fetch "$bundle" refs/heads/main:refs/remotes/bundle/main
  git -C "$target" checkout main
  git -C "$target" merge --ff-only refs/remotes/bundle/main
  if git -C "$target" remote get-url origin >/dev/null 2>&1; then
    git -C "$target" remote set-url origin "$origin_url"
  else
    git -C "$target" remote add origin "$origin_url"
  fi
  action=updated
fi

echo '=== VERIFY WORKTREE ==='
git -C "$target" fsck --connectivity-only
git -C "$target" status --short --branch
git -C "$target" log -1 --format='HEAD=%H%nCOMMIT_DATE=%cI%nSUBJECT=%s'
echo "REMOTE_CODE_READY action=$action path=$target"

#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: apply_site_packages_patch.sh [--patch FILE] [--reverse]

Locate the installed vllm_ascend package, validate the standalone diagnostics
patch with a dry run, then apply it. Use --reverse to remove the patch.
The affected vLLM services must be restarted after either operation.
EOF
}

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
patch_file=$script_dir/../../patches/vllm-ascend-v0.19.1rc1-deadlock-diagnostics.patch
reverse=0
while (($#)); do
  case "$1" in
    --patch) patch_file=$2; shift 2 ;;
    --reverse) reverse=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ ! -f $patch_file ]]; then
  echo "patch not found: $patch_file" >&2
  exit 2
fi
if ! command -v patch >/dev/null 2>&1; then
  echo "the 'patch' command is required" >&2
  exit 2
fi

package_info=$(python3 -c 'import importlib.metadata, pathlib, vllm_ascend; print(importlib.metadata.version("vllm-ascend")); print(pathlib.Path(vllm_ascend.__file__).resolve().parent.parent)')
version=$(printf '%s\n' "$package_info" | sed -n '1p')
site_root=$(printf '%s\n' "$package_info" | sed -n '2p')

echo "vllm-ascend version: $version"
echo "site-packages root: $site_root"
echo "patch: $patch_file"
case "$version" in
  0.19.1rc1|0.19.1rc1+*) ;;
  *)
    echo "unsupported vllm-ascend version: $version (expected 0.19.1rc1)" >&2
    exit 2
    ;;
esac

patch_args=(-p1 -d "$site_root" --batch --forward)
operation=apply
if ((reverse)); then
  patch_args=(-p1 -d "$site_root" --batch --reverse)
  operation=reverse
fi

if ! patch "${patch_args[@]}" --dry-run <"$patch_file"; then
  if ((! reverse)) && patch -p1 -d "$site_root" --batch --reverse --dry-run <"$patch_file" >/dev/null 2>&1; then
    echo "patch is already applied"
    exit 0
  fi
  echo "patch dry run failed; no files were changed" >&2
  exit 2
fi

patch "${patch_args[@]}" <"$patch_file"
if ((! reverse)); then
  python3 -m compileall -q \
    "$site_root/vllm_ascend/diagnostics" \
    "$site_root/vllm_ascend/xlite/xlite_worker.py"
  python3 -c 'from vllm_ascend.diagnostics.deadlock_dump import initialize_deadlock_diagnostics; assert callable(initialize_deadlock_diagnostics)'
else
  python3 -m compileall -q "$site_root/vllm_ascend/xlite/xlite_worker.py"
fi
echo "patch $operation completed successfully; restart both vLLM services"

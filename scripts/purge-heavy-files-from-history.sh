#!/usr/bin/env bash
# Remove large / generated paths from the entire Git history so pushes stay small.
# Uses git-filter-repo (NOT filter-branch). Comments in English.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

usage() {
    echo "Rewrites Git history to drop vendor/, piper/, build/, dist/, models/*.onnx, models/*.bin."
    echo "Usage: $0 [--yes|-y] [--dry-run]"
    echo "  --yes|-y     Skip confirmation (must type YES otherwise)"
    echo "  --dry-run    Preview with git-filter-repo --dry-run"
    exit "${1:-0}"
}

YES=0
DRY=()
while [ $# -gt 0 ]; do
    case "$1" in
        --yes | -y) YES=1 ;;
        --dry-run) DRY=(--dry-run) ;;
        -h | --help) usage 0 ;;
        *) echo "Unknown option: $1" >&2; usage 1 ;;
    esac
    shift
done

# --- What this script does (read before running)
#
# 1. Rewrites ALL commits: removes these paths from history (every revision):
#    - vendor/   piper/   build/   dist/
#    - models/*.onnx   models/*.bin
# 2. You must then: git push --force-with-lease (or --force) to the remote.
# 3. Everyone else must re-clone or reset hard to the new history.
# 4. Backup the repo (zip or extra clone) before running.

if ! command -v git-filter-repo >/dev/null 2>&1; then
    echo "ERROR: git-filter-repo is not installed." >&2
    echo "  Debian/Ubuntu: sudo apt install git-filter-repo" >&2
    echo "  Fedora:        sudo dnf install git-filter-repo" >&2
    echo "  pip:           pip install --user git-filter-repo  (ensure ~/.local/bin on PATH)" >&2
    exit 1
fi

if [ -n "$(git status --porcelain 2>/dev/null)" ]; then
    echo "ERROR: Working tree is not clean. Commit or stash changes first." >&2
    git status -s
    exit 1
fi

ORIGIN_URL=""
if git remote get-url origin >/dev/null 2>&1; then
    ORIGIN_URL="$(git remote get-url origin)"
fi

echo "=== Purge heavy paths from Git history ==="
echo "Repository: $REPO_ROOT"
echo "Will REMOVE from all commits:"
echo "  vendor/  piper/  build/  dist/"
echo "  models/*.onnx  models/*.bin"
echo ""
if [ -n "$ORIGIN_URL" ]; then
    echo "Remote origin (will be re-added after rewrite): $ORIGIN_URL"
else
    echo "No 'origin' remote (OK for local-only cleanup)."
fi
echo ""

if [ "$YES" != 1 ]; then
    echo "This rewrites history. Type YES to continue:"
    read -r line
    if [ "$line" != "YES" ]; then
        echo "Aborted."
        exit 1
    fi
fi

# filter-repo drops remotes by default; save URL and restore after.
git filter-repo "${DRY[@]}" \
    --invert-paths \
    --path vendor/ \
    --path piper/ \
    --path build/ \
    --path dist/ \
    --path-glob 'models/*.onnx' \
    --path-glob 'models/*.bin'

if [ ${#DRY[@]} -gt 0 ]; then
    echo "Dry-run finished; no changes written."
    exit 0
fi

if [ -n "$ORIGIN_URL" ]; then
    git remote add origin "$ORIGIN_URL" 2>/dev/null || git remote set-url origin "$ORIGIN_URL"
fi

echo ""
echo "=== Done ==="
echo "Next steps:"
echo "  1. Inspect: git log --oneline -5"
echo "  2. Push rewritten history (destructive for remote):"
echo "       git push --force-with-lease origin \"\$(git branch --show-current)\""
echo "  3. If you use tags with large files:"
echo "       git push --force-with-lease origin --tags"
echo "  4. Tell collaborators to re-clone or:"
echo "       git fetch origin && git reset --hard origin/main"

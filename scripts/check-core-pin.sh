#!/usr/bin/env bash
# The pxs-core submodule must be pinned to a released tag of pxs-core (vX.Y.Z), never to a branch or to a commit that is
# not a tag. A commit pinned from a pull request branch becomes unreachable the day that branch is squash-merged or
# deleted, and every checkout of this repository then fails to fetch the submodule.
set -euo pipefail

repo_url="${PXS_CORE_URL:-https://github.com/PixelStereo/pxs-core.git}"
sha="$(git ls-tree HEAD third_party/pxs-core | awk '{print $3}')"
[ -n "$sha" ] || { echo "third_party/pxs-core is not a submodule of this commit" >&2; exit 1; }

# annotated tags list the tag object and, with ^{}, the commit it points to; lightweight tags list the commit directly
tags="$(git ls-remote --tags "$repo_url" 'v*' | awk -v sha="$sha" '{ref=$2; sub("refs/tags/","",ref); sub(/\^\{\}$/,"",ref)} $1==sha {print ref}' | sort -u)"
if [ -z "$tags" ]; then
    echo "third_party/pxs-core is pinned to $sha, which is not a tag of $repo_url." >&2
    echo "Release pxs-core first (see its RELEASING.md), then pin the tag:" >&2
    echo "  git -C third_party/pxs-core fetch --tags && git -C third_party/pxs-core checkout vX.Y.Z && git add third_party/pxs-core" >&2
    exit 1
fi
echo "third_party/pxs-core is pinned to $sha ($(echo "$tags" | tr '\n' ' '))"

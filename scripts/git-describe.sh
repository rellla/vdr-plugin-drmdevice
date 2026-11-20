#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Print the git suffix of the plugin version:
#   (nothing)        the checkout is a tag without changes, git isn't installed or it
#                    isn't a git checkout of the plugin (e.g. an archive, which is built
#                    inside another repository)
#   -<hash>          there are commits after the last tag
#   -<hash>-dirty    there are uncommitted changes

cd "$(dirname "$0")/.." || exit 0

command -v git >/dev/null || exit 0

[ "$(git rev-parse --show-toplevel 2>/dev/null)" = "$(pwd -P)" ] || exit 0

if git describe --tags --exact-match HEAD >/dev/null 2>&1 && git diff --quiet HEAD; then
	exit 0
fi

echo "-$(git describe --always --dirty --exclude '*')"

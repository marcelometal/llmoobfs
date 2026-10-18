#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Every tracked source starts with an SPDX line (CONTRIBUTING.md): C on the first line; shell,
# Python and vm/init on the first or the second, after a shebang (issue #15). Vendored code
# (tests/third_party/) and archived result scripts (bench/results/) keep their own headers.
set -eu
missing=0
# Tracked paths contain no spaces, so word splitting of the lists is intended.
# shellcheck disable=SC2046
for f in $(git ls-files '*.c' '*.h' ':!:tests/third_party/*' ':!:bench/results/*'); do
	if ! head -n 1 "$f" | grep -q 'SPDX-License-Identifier:'; then
		echo "missing SPDX header: $f"
		missing=1
	fi
done
# shellcheck disable=SC2046
for f in $(git ls-files '*.sh' '*.py' 'vm/init' ':!:tests/third_party/*' ':!:bench/results/*'); do
	if ! head -n 2 "$f" | grep -q 'SPDX-License-Identifier:'; then
		echo "missing SPDX header: $f"
		missing=1
	fi
done
[ "$missing" -eq 0 ]

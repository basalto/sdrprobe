#!/usr/bin/env bash
set -eo pipefail

BUILD="build"
TALLY="${CHECK_TALLY:-$BUILD/check.tally}"
CHECK_JOBS="${CHECK_JOBS:-$(nproc 2>/dev/null || echo 4)}"
CHECK_LOG="$BUILD/check.log"
CHECK_STAMP="$BUILD/.check-stamp"
TMP_INDEX="$BUILD/.check_tmp_index"

mkdir -p "$BUILD"
rm -f "$TALLY" "$CHECK_STAMP"

printf '\nsdrprobe checks -- no window, no receiver, nobody watching\n\n'

start_time=$(date -u +'%Y-%m-%d %H:%M:%S UTC')
t0=$(date +%s)

# Run make check targets, stream to stdout and capture to temporary file
set +e
CHECK_TALLY="$TALLY" make --no-print-directory -j"$CHECK_JOBS" \
    --output-sync=target check-pipelines "$@" 2>&1 | tee "$BUILD/check_current.tmp"
rc=${PIPESTATUS[0]}
set -e

t1=$(date +%s)
dt=$(( t1 - t0 ))

if [ $rc -eq 0 ]; then
    summary=$(awk -v dt="$dt" '{checks += $1; bad += $2} END { printf \
        "%d checks in %d suites, no failures (took %ds)", checks, NR, dt}' "$TALLY")
    printf "\n%s\n\n" "$summary"

    # Compute working tree hash of tracked files in working directory
    rm -f "$TMP_INDEX"
    work_tree=$(GIT_INDEX_FILE="$TMP_INDEX" git --work-tree=. add -A 2>/dev/null && \
                GIT_INDEX_FILE="$TMP_INDEX" git write-tree 2>/dev/null || echo "")
    rm -f "$TMP_INDEX"

    head_commit=$(git rev-parse HEAD 2>/dev/null || echo "")
    head_tree=$(git rev-parse HEAD^{tree} 2>/dev/null || echo "")

    # Output CI logs with timestamp to build/check.log
    {
        printf "================================================================================\n"
        printf "CI CHECK RUN: %s (took %ds)\n" "$start_time" "$dt"
        printf "COMMIT:       %s\n" "$head_commit"
        printf "HEAD_TREE:    %s\n" "$head_tree"
        printf "WORK_TREE:    %s\n" "$work_tree"
        printf "RESULT:       %s\n" "$summary"
        printf "================================================================================\n\n"
        cat "$BUILD/check_current.tmp"
        printf "\n"
    } > "$CHECK_LOG"

    # Write state stamp for pre-push hook
    cat <<EOF > "$CHECK_STAMP"
TIMESTAMP=$start_time
COMMIT=$head_commit
HEAD_TREE=$head_tree
WORK_TREE=$work_tree
SUMMARY=$summary
EOF
else
    rm -f "$CHECK_STAMP"
    printf "\nChecks FAILED with exit code %d (took %ds)\n\n" "$rc" "$dt"
fi

rm -f "$BUILD/check_current.tmp"
exit $rc

#!/bin/sh
# Compile the PLATO FPGA core with Quartus 17.0.2 Lite in Docker (x86 image,
# runs under Rosetta on Apple Silicon).  Output: core/output_files/PLATO.rbf
#
# Synthesis (quartus_map) runs on one processor: in parallel mode it hangs
# under Rosetta.  The fitter, the slowest step, runs on 8 processors.
set -e
cd "$(dirname "$0")/../core"
docker run --rm --platform linux/amd64 -v "$PWD":/build -w /build \
    --entrypoint bash theypsilon/quartus-lite-c5:17.0.2-heavy -c '
set -e
t0=$(date +%s)
quartus_sh -t sys/build_id.tcl > build.log 2>&1
quartus_map PLATO -c PLATO >> build.log 2>&1
quartus_fit --parallel=8 PLATO -c PLATO >> build.log 2>&1
quartus_asm PLATO -c PLATO >> build.log 2>&1
quartus_sta PLATO -c PLATO >> build.log 2>&1
echo "build time: $(( $(date +%s) - t0 ))s"'
grep -E "Worst-case setup slack|Error \(" build.log | head -3
ls -la output_files/PLATO.rbf

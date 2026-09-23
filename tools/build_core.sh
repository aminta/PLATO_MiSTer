#!/bin/sh
# Compile the PLATO FPGA core with Quartus 17.0.2 Lite in Docker (x86 image,
# runs under Rosetta on Apple Silicon).  Output: core/output_files/PLATO.rbf
set -e
cd "$(dirname "$0")/../core"
docker run --rm --platform linux/amd64 -v "$PWD":/build -w /build \
    --entrypoint bash theypsilon/quartus-lite-c5:17.0.2-heavy \
    -c 'quartus_sh --flow compile PLATO.qpf > build.log 2>&1'
grep -E "was successful|Error \(" build.log | tail -5
ls -la output_files/PLATO.rbf

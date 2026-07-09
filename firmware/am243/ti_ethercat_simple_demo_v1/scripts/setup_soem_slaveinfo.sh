#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
THIRD_PARTY="$ROOT/third_party"
SOEM_ZIP="$THIRD_PARTY/soem-master.zip"
SOEM_SRC="$THIRD_PARTY/SOEM-master"
SOEM_BUILD="$THIRD_PARTY/SOEM-manual-build"
SLAVEINFO="$SOEM_BUILD/bin/slaveinfo"
SIMPLE_NG="$SOEM_BUILD/bin/simple_ng"
AM243_SIMPLE_NG="$SOEM_BUILD/bin/am243_simple_ng"
AM243_SIMPLE_NG_SRC="$SOEM_BUILD/am243_simple_ng.c"
AM243_LRD_LWR="$SOEM_BUILD/bin/am243_lrd_lwr"
AM243_LRD_LWR_SRC="$SOEM_BUILD/am243_lrd_lwr.c"
AM243_SPLIT="$SOEM_BUILD/bin/am243_split_ng"
AM243_SPLIT_SRC="$SOEM_BUILD/am243_split_ng.c"
AM243_PDO_WALK="$SOEM_BUILD/bin/am243_pdo_walk_ng"
AM243_PDO_WALK_SRC="$SOEM_BUILD/am243_pdo_walk_ng.c"
AM243_NO_DC="$SOEM_BUILD/bin/am243_no_dc"
AM243_NO_DC_SRC="$SOEM_BUILD/am243_no_dc.c"

mkdir -p "$THIRD_PARTY" "$SOEM_BUILD/include/soem" "$SOEM_BUILD/bin"

if [[ ! -d "$SOEM_SRC" ]]; then
    if [[ ! -f "$SOEM_ZIP" ]]; then
        wget -O "$SOEM_ZIP" https://github.com/OpenEtherCATsociety/SOEM/archive/refs/heads/master.zip
    fi
    unzip -q "$SOEM_ZIP" -d "$THIRD_PARTY"
fi

cat > "$SOEM_BUILD/include/soem/ec_options.h" <<'EOF'
#ifndef _ec_options_
#define _ec_options_

#ifdef __cplusplus
extern "C" {
#endif

#define EC_BUFSIZE (EC_MAXECATFRAME)
#define EC_MAXBUF (16)
#define EC_MAXEEPBITMAP (128)
#define EC_MAXEEPBUF (EC_MAXEEPBITMAP << 5)
#define EC_LOGGROUPOFFSET (16)
#define EC_MAXELIST (64)
#define EC_MAXNAME (40)
#define EC_MAXSLAVE (200)
#define EC_MAXGROUP (2)
#define EC_MAXIOSEGMENTS (64)
#define EC_MAXMBX (1486)
#define EC_MBXPOOLSIZE (32)
#define EC_MAXEEPDO (0x200)
#define EC_MAXSM (8)
#define EC_MAXFMMU (4)
#define EC_MAXLEN_ADAPTERNAME (128)
#define EC_MAX_MAPT (1)
#define EC_MAXODLIST (1024)
#define EC_MAXOELIST (256)
#define EC_SOE_MAXNAME (60)
#define EC_SOE_MAXMAPPING (64)

#define EC_TIMEOUTRET (10000)
#define EC_TIMEOUTRET3 (EC_TIMEOUTRET * 3)
#define EC_TIMEOUTSAFE (20000)
#define EC_TIMEOUTEEP (20000)
#define EC_TIMEOUTTXM (20000)
#define EC_TIMEOUTRXM (700000)
#define EC_TIMEOUTSTATE (2000000)
#define EC_DEFAULTRETRIES (3)

#define EC_PRIMARY_MAC_ARRAY {0x0101, 0x0101, 0x0101}
#define EC_SECONDARY_MAC_ARRAY {0x0404, 0x0404, 0x0404}

#ifdef __cplusplus
}
#endif

#endif
EOF

gcc -Wall -Wextra -O2 -g \
    -I"$SOEM_SRC/include" \
    -I"$SOEM_BUILD/include" \
    -I"$SOEM_SRC/osal" \
    -I"$SOEM_SRC/osal/linux" \
    -I"$SOEM_SRC/oshw/linux" \
    "$SOEM_SRC/src/ec_base.c" \
    "$SOEM_SRC/src/ec_coe.c" \
    "$SOEM_SRC/src/ec_config.c" \
    "$SOEM_SRC/src/ec_dc.c" \
    "$SOEM_SRC/src/ec_eoe.c" \
    "$SOEM_SRC/src/ec_foe.c" \
    "$SOEM_SRC/src/ec_main.c" \
    "$SOEM_SRC/src/ec_print.c" \
    "$SOEM_SRC/src/ec_soe.c" \
    "$SOEM_SRC/osal/linux/osal.c" \
    "$SOEM_SRC/oshw/linux/oshw.c" \
    "$SOEM_SRC/oshw/linux/nicdrv.c" \
    "$SOEM_SRC/samples/slaveinfo/slaveinfo.c" \
    -lpthread -lrt \
    -o "$SLAVEINFO"

echo "Built $SLAVEINFO"

gcc -Wall -Wextra -O2 -g \
    -I"$SOEM_SRC/include" \
    -I"$SOEM_BUILD/include" \
    -I"$SOEM_SRC/osal" \
    -I"$SOEM_SRC/osal/linux" \
    -I"$SOEM_SRC/oshw/linux" \
    "$SOEM_SRC/src/ec_base.c" \
    "$SOEM_SRC/src/ec_coe.c" \
    "$SOEM_SRC/src/ec_config.c" \
    "$SOEM_SRC/src/ec_dc.c" \
    "$SOEM_SRC/src/ec_eoe.c" \
    "$SOEM_SRC/src/ec_foe.c" \
    "$SOEM_SRC/src/ec_main.c" \
    "$SOEM_SRC/src/ec_print.c" \
    "$SOEM_SRC/src/ec_soe.c" \
    "$SOEM_SRC/osal/linux/osal.c" \
    "$SOEM_SRC/oshw/linux/oshw.c" \
    "$SOEM_SRC/oshw/linux/nicdrv.c" \
    "$SOEM_SRC/samples/simple_ng/simple_ng.c" \
    -lpthread -lrt \
    -o "$SIMPLE_NG"

echo "Built $SIMPLE_NG"

awk '
{
    if ($0 ~ /printf\("Sequential mapping of I\/O\.\.\. "\);/) {
        sub(/Sequential mapping of I\/O/, "Overlapped TI ESC mapping of I/O")
    }
    if ($0 ~ /ecx_config_map_group\(context, fieldbus->map, fieldbus->group\);/) {
        print "   context->overlappedMode = TRUE;"
    }
    print
}
' "$SOEM_SRC/samples/simple_ng/simple_ng.c" > "$AM243_SIMPLE_NG_SRC"

gcc -Wall -Wextra -O2 -g \
    -I"$SOEM_SRC/include" \
    -I"$SOEM_BUILD/include" \
    -I"$SOEM_SRC/osal" \
    -I"$SOEM_SRC/osal/linux" \
    -I"$SOEM_SRC/oshw/linux" \
    "$SOEM_SRC/src/ec_base.c" \
    "$SOEM_SRC/src/ec_coe.c" \
    "$SOEM_SRC/src/ec_config.c" \
    "$SOEM_SRC/src/ec_dc.c" \
    "$SOEM_SRC/src/ec_eoe.c" \
    "$SOEM_SRC/src/ec_foe.c" \
    "$SOEM_SRC/src/ec_main.c" \
    "$SOEM_SRC/src/ec_print.c" \
    "$SOEM_SRC/src/ec_soe.c" \
    "$SOEM_SRC/osal/linux/osal.c" \
    "$SOEM_SRC/oshw/linux/oshw.c" \
    "$SOEM_SRC/oshw/linux/nicdrv.c" \
    "$AM243_SIMPLE_NG_SRC" \
    -lpthread -lrt \
    -o "$AM243_SIMPLE_NG"

echo "Built $AM243_SIMPLE_NG"

awk '
{
    if ($0 ~ /printf\("Sequential mapping of I\/O\.\.\. "\);/) {
        sub(/Sequential mapping of I\/O/, "Overlapped TI ESC mapping with forced LRD/LWR")
    }
    if ($0 ~ /ecx_config_map_group\(context, fieldbus->map, fieldbus->group\);/) {
        print "   context->overlappedMode = TRUE;"
    }
    print
    if ($0 ~ /ecx_config_map_group\(context, fieldbus->map, fieldbus->group\);/) {
        print "   context->grouplist[fieldbus->group].blockLRW = 1;"
    }
}
' "$SOEM_SRC/samples/simple_ng/simple_ng.c" > "$AM243_LRD_LWR_SRC"

gcc -Wall -Wextra -O2 -g \
    -I"$SOEM_SRC/include" \
    -I"$SOEM_BUILD/include" \
    -I"$SOEM_SRC/osal" \
    -I"$SOEM_SRC/osal/linux" \
    -I"$SOEM_SRC/oshw/linux" \
    "$SOEM_SRC/src/ec_base.c" \
    "$SOEM_SRC/src/ec_coe.c" \
    "$SOEM_SRC/src/ec_config.c" \
    "$SOEM_SRC/src/ec_dc.c" \
    "$SOEM_SRC/src/ec_eoe.c" \
    "$SOEM_SRC/src/ec_foe.c" \
    "$SOEM_SRC/src/ec_main.c" \
    "$SOEM_SRC/src/ec_print.c" \
    "$SOEM_SRC/src/ec_soe.c" \
    "$SOEM_SRC/osal/linux/osal.c" \
    "$SOEM_SRC/oshw/linux/oshw.c" \
    "$SOEM_SRC/oshw/linux/nicdrv.c" \
    "$AM243_LRD_LWR_SRC" \
    -lpthread -lrt \
    -o "$AM243_LRD_LWR"

echo "Built $AM243_LRD_LWR"

awk '
{
    if ($0 ~ /printf\("Sequential mapping of I\/O\.\.\. "\);/) {
        sub(/Sequential mapping of I\/O/, "Sequential split LRD/LWR mapping")
    }
    if ($0 ~ /printf\("%d slaves found\\n", context->slavecount\);/) {
        print
        print "   for (i = 1; i <= context->slavecount; ++i)"
        print "   {"
        print "      context->slavelist[i].blockLRW = 1;"
        print "      context->slavelist[0].blockLRW++;"
        print "   }"
        next
    }
    print
}
' "$SOEM_SRC/samples/simple_ng/simple_ng.c" > "$AM243_SPLIT_SRC"

gcc -Wall -Wextra -O2 -g \
    -I"$SOEM_SRC/include" \
    -I"$SOEM_BUILD/include" \
    -I"$SOEM_SRC/osal" \
    -I"$SOEM_SRC/osal/linux" \
    -I"$SOEM_SRC/oshw/linux" \
    "$SOEM_SRC/src/ec_base.c" \
    "$SOEM_SRC/src/ec_coe.c" \
    "$SOEM_SRC/src/ec_config.c" \
    "$SOEM_SRC/src/ec_dc.c" \
    "$SOEM_SRC/src/ec_eoe.c" \
    "$SOEM_SRC/src/ec_foe.c" \
    "$SOEM_SRC/src/ec_main.c" \
    "$SOEM_SRC/src/ec_print.c" \
    "$SOEM_SRC/src/ec_soe.c" \
    "$SOEM_SRC/osal/linux/osal.c" \
    "$SOEM_SRC/oshw/linux/oshw.c" \
    "$SOEM_SRC/oshw/linux/nicdrv.c" \
    "$AM243_SPLIT_SRC" \
    -lpthread -lrt \
    -o "$AM243_SPLIT"

echo "Built $AM243_SPLIT"

awk '
{
    if ($0 ~ /printf\("Sequential mapping of I\/O\.\.\. "\);/) {
        sub(/Sequential mapping of I\/O/, "Sequential split LRD/LWR PDO walk mapping")
    }
    if ($0 ~ /printf\("%d slaves found\\n", context->slavecount\);/) {
        print
        print "   for (i = 1; i <= context->slavecount; ++i)"
        print "   {"
        print "      context->slavelist[i].blockLRW = 1;"
        print "      context->slavelist[0].blockLRW++;"
        print "   }"
        next
    }
    if ($0 ~ /int wkc, expected_wkc;/) {
        print
        print "   static uint8 output_seed = 0;"
        next
    }
    if ($0 ~ /wkc = fieldbus_roundtrip\(fieldbus\);/) {
        print "   for (n = 0; n < grp->Obytes; ++n)"
        print "   {"
        print "      grp->outputs[n] = (uint8)(output_seed + n);"
        print "   }"
        print "   ++output_seed;"
    }
    if ($0 ~ /printf\(\"  T: %lld\\r\", \(long long\)context->DCtime\);/) {
        print "   printf(\"  T: %lld\\n\", (long long)context->DCtime);"
        print "   fflush(stdout);"
        next
    }
    print
}
' "$SOEM_SRC/samples/simple_ng/simple_ng.c" > "$AM243_PDO_WALK_SRC"

gcc -Wall -Wextra -O2 -g \
    -I"$SOEM_SRC/include" \
    -I"$SOEM_BUILD/include" \
    -I"$SOEM_SRC/osal" \
    -I"$SOEM_SRC/osal/linux" \
    -I"$SOEM_SRC/oshw/linux" \
    "$SOEM_SRC/src/ec_base.c" \
    "$SOEM_SRC/src/ec_coe.c" \
    "$SOEM_SRC/src/ec_config.c" \
    "$SOEM_SRC/src/ec_dc.c" \
    "$SOEM_SRC/src/ec_eoe.c" \
    "$SOEM_SRC/src/ec_foe.c" \
    "$SOEM_SRC/src/ec_main.c" \
    "$SOEM_SRC/src/ec_print.c" \
    "$SOEM_SRC/src/ec_soe.c" \
    "$SOEM_SRC/osal/linux/osal.c" \
    "$SOEM_SRC/oshw/linux/oshw.c" \
    "$SOEM_SRC/oshw/linux/nicdrv.c" \
    "$AM243_PDO_WALK_SRC" \
    -lpthread -lrt \
    -o "$AM243_PDO_WALK"

echo "Built $AM243_PDO_WALK"

awk '
{
    if ($0 ~ /printf\("Sequential mapping of I\/O\.\.\. "\);/) {
        sub(/Sequential mapping of I\/O/, "Overlapped TI ESC mapping with process DC disabled")
    }
    if ($0 ~ /ecx_config_map_group\(context, fieldbus->map, fieldbus->group\);/) {
        print "   context->overlappedMode = TRUE;"
    }
    if ($0 ~ /printf\("Configuring distributed clock\.\.\. "\);/) {
        print "   printf(\"Leaving distributed clock config off for process-data diagnostic... \");"
        getline
        if ($0 ~ /ecx_configdc\(context\);/) {
            print "   context->grouplist[fieldbus->group].hasdc = FALSE;"
            next
        }
    }
    print
}
' "$SOEM_SRC/samples/simple_ng/simple_ng.c" > "$AM243_NO_DC_SRC"

gcc -Wall -Wextra -O2 -g \
    -I"$SOEM_SRC/include" \
    -I"$SOEM_BUILD/include" \
    -I"$SOEM_SRC/osal" \
    -I"$SOEM_SRC/osal/linux" \
    -I"$SOEM_SRC/oshw/linux" \
    "$SOEM_SRC/src/ec_base.c" \
    "$SOEM_SRC/src/ec_coe.c" \
    "$SOEM_SRC/src/ec_config.c" \
    "$SOEM_SRC/src/ec_dc.c" \
    "$SOEM_SRC/src/ec_eoe.c" \
    "$SOEM_SRC/src/ec_foe.c" \
    "$SOEM_SRC/src/ec_main.c" \
    "$SOEM_SRC/src/ec_print.c" \
    "$SOEM_SRC/src/ec_soe.c" \
    "$SOEM_SRC/osal/linux/osal.c" \
    "$SOEM_SRC/oshw/linux/oshw.c" \
    "$SOEM_SRC/oshw/linux/nicdrv.c" \
    "$AM243_NO_DC_SRC" \
    -lpthread -lrt \
    -o "$AM243_NO_DC"

echo "Built $AM243_NO_DC"

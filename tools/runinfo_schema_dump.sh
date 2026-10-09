#!/bin/bash
# Dump the run_info database schema topology for one schema (default: online).
# Usage:  runinfo_schema_dump.sh [schema] [run_number]
#   schema      defaults to $OTSDAQ_RUNINFO_DATABASE_SCHEMA or 'online'
#   run_number  optional; shows the config/subsystem rows for that run

SCHEMA="${1:-${OTSDAQ_RUNINFO_DATABASE_SCHEMA:-online}}"
RUN="${2:-}"

for v in OTSDAQ_RUNINFO_DATABASE OTSDAQ_RUNINFO_DATABASE_HOST OTSDAQ_RUNINFO_DATABASE_PORT \
         OTSDAQ_RUNINFO_DATABASE_USER OTSDAQ_RUNINFO_DATABASE_PWD; do
	if [ -z "${!v}" ]; then
		echo "ERROR: $v is not set. Source setup_ots.sh (or db_setup_ots.sh) first." >&2
		exit 1
	fi
done

if ! command -v psql >/dev/null; then
	echo "ERROR: psql not in PATH. Source setup_ots.sh first (spack view provides psql)." >&2
	exit 1
fi

export PGPASSWORD="$OTSDAQ_RUNINFO_DATABASE_PWD"
PSQL="psql -h $OTSDAQ_RUNINFO_DATABASE_HOST -p $OTSDAQ_RUNINFO_DATABASE_PORT \
      -U $OTSDAQ_RUNINFO_DATABASE_USER -d $OTSDAQ_RUNINFO_DATABASE -X -v ON_ERROR_STOP=0"

echo "=== run_info @ $OTSDAQ_RUNINFO_DATABASE_HOST:$OTSDAQ_RUNINFO_DATABASE_PORT  schema=$SCHEMA ==="
echo

echo "--- schemas present ---"
$PSQL -c "SELECT schema_name FROM information_schema.schemata
          WHERE schema_name NOT IN ('pg_catalog','information_schema','pg_toast')
          ORDER BY 1;"

echo "--- tables and row counts ---"
$PSQL -At -c "SELECT tablename FROM pg_tables WHERE schemaname='$SCHEMA' ORDER BY 1;" |
while read -r t; do
	n=$($PSQL -At -c "SELECT count(*) FROM $SCHEMA.\"$t\";")
	printf "  %-32s %s\n" "$t" "$n"
done
echo

echo "--- views ---"
$PSQL -At -c "SELECT viewname FROM pg_views WHERE schemaname='$SCHEMA' ORDER BY 1;"
echo

echo "--- columns per table/view ---"
$PSQL -c "SELECT table_name, ordinal_position AS pos, column_name, data_type, is_nullable
          FROM information_schema.columns
          WHERE table_schema='$SCHEMA'
          ORDER BY table_name, ordinal_position;"

echo "--- foreign keys (how tables link) ---"
$PSQL -c "SELECT tc.table_name, kcu.column_name,
                 ccu.table_name AS ref_table, ccu.column_name AS ref_column
          FROM information_schema.table_constraints tc
          JOIN information_schema.key_column_usage kcu
            ON tc.constraint_name = kcu.constraint_name AND tc.table_schema = kcu.table_schema
          JOIN information_schema.constraint_column_usage ccu
            ON ccu.constraint_name = tc.constraint_name AND ccu.table_schema = tc.table_schema
          WHERE tc.constraint_type='FOREIGN KEY' AND tc.table_schema='$SCHEMA'
          ORDER BY tc.table_name, kcu.column_name;"

echo "--- view definitions ---"
$PSQL -At -c "SELECT viewname FROM pg_views WHERE schemaname='$SCHEMA' ORDER BY 1;" |
while read -r v; do
	echo "----- $SCHEMA.$v -----"
	$PSQL -At -c "SELECT pg_get_viewdef('$SCHEMA.\"$v\"'::regclass, true);"
	echo
done

echo "--- lookup tables ---"
for t in run_type run_transition_type; do
	echo "----- $SCHEMA.$t -----"
	$PSQL -c "SELECT * FROM $SCHEMA.$t ORDER BY 1;" 2>/dev/null || echo "  (no table $t)"
done

echo "--- latest 5 runs from view_run_summary ---"
$PSQL -c "SELECT * FROM $SCHEMA.view_run_summary ORDER BY run_number DESC LIMIT 5;"

echo "--- run_status distribution (last 30 days) ---"
$PSQL -c "SELECT run_status, count(*) FROM $SCHEMA.view_run_summary
          WHERE start_time > now() - interval '30 days' GROUP BY 1 ORDER BY 2 DESC;"

if [ -n "$RUN" ]; then
	echo "=== run $RUN ==="
	echo "----- run row -----"
	$PSQL -x -c "SELECT * FROM $SCHEMA.run WHERE run_number=$RUN;"
	echo "----- transitions (with type names) -----"
	$PSQL -c "SELECT t.type_id, tt.name, tt.description, t.cause_id, t.transition_time
	          FROM $SCHEMA.run_transition t
	          LEFT JOIN $SCHEMA.run_transition_type tt ON tt.id = t.type_id
	          WHERE t.run_number=$RUN ORDER BY t.transition_time;"
	echo "----- config rows: one per subsystem -----"
	$PSQL -c "SELECT subsystem, version, create_time, pg_column_size(settings) AS settings_bytes
	          FROM $SCHEMA.config WHERE run_number=$RUN ORDER BY subsystem;"
	echo "----- settings: top-level keys per subsystem -----"
	$PSQL -c "SELECT subsystem, string_agg(k, ', ' ORDER BY k) AS top_keys
	          FROM $SCHEMA.config, jsonb_object_keys(settings) k
	          WHERE run_number=$RUN GROUP BY subsystem ORDER BY subsystem;"
	echo "----- settings.config keys per subsystem -----"
	$PSQL -c "SELECT subsystem, string_agg(k, ', ' ORDER BY k) AS config_keys
	          FROM $SCHEMA.config, jsonb_object_keys(settings->'config') k
	          WHERE run_number=$RUN AND jsonb_typeof(settings->'config')='object'
	          GROUP BY subsystem ORDER BY subsystem;"
	echo "----- settings.config.groups: full content per subsystem -----"
	$PSQL -c "SELECT subsystem, jsonb_pretty(settings#>'{config,groups}') AS groups
	          FROM $SCHEMA.config WHERE run_number=$RUN ORDER BY subsystem;"
	echo "----- settings.config.custom keys per subsystem -----"
	$PSQL -c "SELECT subsystem, string_agg(k, ', ' ORDER BY k) AS custom_keys
	          FROM $SCHEMA.config, jsonb_object_keys(settings#>'{config,custom}') k
	          WHERE run_number=$RUN AND jsonb_typeof(settings#>'{config,custom}')='object'
	          GROUP BY subsystem ORDER BY subsystem;"
	echo "----- settings.env keys per subsystem -----"
	$PSQL -c "SELECT subsystem, string_agg(k, ', ' ORDER BY k) AS env_keys
	          FROM $SCHEMA.config, jsonb_object_keys(settings->'env') k
	          WHERE run_number=$RUN AND jsonb_typeof(settings->'env')='object'
	          GROUP BY subsystem ORDER BY subsystem;"
	echo "----- settings.dump keys per subsystem -----"
	$PSQL -c "SELECT subsystem, string_agg(k, ', ' ORDER BY k) AS dump_keys
	          FROM $SCHEMA.config, jsonb_object_keys(settings->'dump') k
	          WHERE run_number=$RUN AND jsonb_typeof(settings->'dump')='object'
	          GROUP BY subsystem ORDER BY subsystem;"
	echo "----- Gateway settings (first 3000 chars, pretty) -----"
	$PSQL -At -c "SELECT left(jsonb_pretty(settings), 3000) FROM $SCHEMA.config
	              WHERE run_number=$RUN AND subsystem='Gateway';"
	echo "----- view_run_config row -----"
	$PSQL -x -c "SELECT * FROM $SCHEMA.view_run_config WHERE run_number=$RUN;"
fi

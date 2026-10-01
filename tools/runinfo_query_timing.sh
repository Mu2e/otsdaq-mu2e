#!/bin/bash
# Time the RunDbViewer run-list queries against the run_info database.
# Usage:  runinfo_query_timing.sh [schema] [days]
#   schema  defaults to $OTSDAQ_RUNINFO_DATABASE_SCHEMA or 'online'
#   days    time window, default 2

SCHEMA="${1:-${OTSDAQ_RUNINFO_DATABASE_SCHEMA:-online}}"
DAYS="${2:-2}"

for v in OTSDAQ_RUNINFO_DATABASE OTSDAQ_RUNINFO_DATABASE_HOST OTSDAQ_RUNINFO_DATABASE_PORT \
         OTSDAQ_RUNINFO_DATABASE_USER OTSDAQ_RUNINFO_DATABASE_PWD; do
	if [ -z "${!v}" ]; then
		echo "ERROR: $v is not set. Source setup_ots.sh first." >&2
		exit 1
	fi
done
command -v psql >/dev/null || { echo "ERROR: psql not in PATH" >&2; exit 1; }

export PGPASSWORD="$OTSDAQ_RUNINFO_DATABASE_PWD"
PSQL="psql -h $OTSDAQ_RUNINFO_DATABASE_HOST -p $OTSDAQ_RUNINFO_DATABASE_PORT \
      -U $OTSDAQ_RUNINFO_DATABASE_USER -d $OTSDAQ_RUNINFO_DATABASE -X -v ON_ERROR_STOP=0"
S="$SCHEMA"
WIN="BETWEEN now() - interval '$DAYS days' AND now()"

echo "=== run_info @ $OTSDAQ_RUNINFO_DATABASE_HOST:$OTSDAQ_RUNINFO_DATABASE_PORT schema=$S window=${DAYS}d ==="
$PSQL -c "SELECT version();"

echo "--- table sizes ---"
$PSQL -c "SELECT relname, pg_size_pretty(pg_total_relation_size(c.oid)) AS total,
                 pg_size_pretty(pg_relation_size(c.oid)) AS heap,
                 pg_size_pretty(COALESCE(pg_total_relation_size(c.reltoastrelid),0)) AS toast
          FROM pg_class c JOIN pg_namespace n ON n.oid=c.relnamespace
          WHERE n.nspname='$S' AND c.relkind='r' ORDER BY pg_total_relation_size(c.oid) DESC;"

echo "--- indexes ---"
$PSQL -c "SELECT tablename, indexname, indexdef FROM pg_indexes WHERE schemaname='$S' ORDER BY 1,2;"

echo "--- config rows / settings bytes in window ---"
$PSQL -c "SELECT count(*) AS config_rows, pg_size_pretty(sum(pg_column_size(c.settings))::bigint) AS settings_total,
                 pg_size_pretty(avg(pg_column_size(c.settings))::bigint) AS settings_avg
          FROM $S.config c
          WHERE c.run_number IN (SELECT run_number FROM $S.run_transition WHERE type_id=5 AND transition_time $WIN);"

echo
echo "=== A. OLD: view_run_summary filtered by start_time ==="
$PSQL -c "EXPLAIN (ANALYZE, BUFFERS, TIMING, SUMMARY)
          SELECT * FROM $S.view_run_summary v WHERE v.start_time $WIN ORDER BY run_number DESC;"

RUNS_CTE="WITH runs AS (
  SELECT r.run_number, r.comment, rt.name AS run_type_name, st.transition_time AS start_time,
    (SELECT min(sp.transition_time) FROM $S.run_transition sp WHERE sp.run_number=r.run_number AND sp.type_id=1) AS stop_time,
    (SELECT lt.type_id FROM $S.run_transition lt WHERE lt.run_number=r.run_number ORDER BY lt.transition_time DESC LIMIT 1) AS latest_type,
    rei.comment AS end_comment
  FROM $S.run r
  JOIN $S.run_transition st ON st.run_number=r.run_number AND st.type_id=5
  LEFT JOIN $S.run_type rt ON rt.id=r.run_type_id
  LEFT JOIN $S.run_end_info rei ON rei.run_number=r.run_number
  WHERE st.transition_time $WIN
)"
GROUPS_EXPR="x.subsystem
    || '|' || COALESCE(x.g->>'Configuration_alias','')
    || '|' || COALESCE(x.g->>'Configuration_group_name','')
    || '|' || COALESCE(x.g->>'Configuration_group_key','')
    || '|' || COALESCE(x.g->>'Context_group_name','')
    || '|' || COALESCE(x.g->>'Context_group_key','')
    || '|' || COALESCE(x.g->>'Backbone_group_name','')
    || '|' || COALESCE(x.g->>'Backbone_group_key','')"

echo
echo "=== B. CURRENT CODE: direct query, jsonb detoasted once per row (OFFSET 0 fences) ==="
$PSQL -c "EXPLAIN (ANALYZE, BUFFERS, TIMING, SUMMARY)
$RUNS_CTE
SELECT runs.run_number, runs.start_time, runs.run_type_name,
  COALESCE(gw.artdaq_partition,''), COALESCE(gw.host_name,''), COALESCE(gw.config_alias,''),
  COALESCE(runs.comment,''), runs.start_time, runs.stop_time, runs.latest_type,
  COALESCE(sg.subsystem_groups,''), COALESCE(runs.end_comment,'')
FROM runs
LEFT JOIN LATERAL (
  SELECT y.e->>'HOSTNAME' AS host_name, y.e->>'ARTDAQ_PARTITION' AS artdaq_partition, y.alias AS config_alias
  FROM (SELECT c.settings->'env' AS e, c.settings->>'config_alias' AS alias
        FROM $S.config c WHERE c.run_number=runs.run_number AND c.subsystem='Gateway'
        ORDER BY c.create_time DESC LIMIT 1 OFFSET 0) y) gw ON true
LEFT JOIN LATERAL (
  SELECT string_agg($GROUPS_EXPR, ';' ORDER BY x.subsystem) AS subsystem_groups
  FROM (SELECT c.subsystem, c.settings #> '{config,groups}' AS g
        FROM $S.config c WHERE c.run_number=runs.run_number OFFSET 0) x) sg ON true
ORDER BY runs.run_number DESC;"

echo
echo "=== B0. PREVIOUS direct query (planner inlines x.g -> 8 detoasts per row) ==="
$PSQL -c "EXPLAIN (ANALYZE, BUFFERS, TIMING, SUMMARY)
WITH runs AS (
  SELECT r.run_number, r.comment, rt.name AS run_type_name, st.transition_time AS start_time,
    (SELECT min(sp.transition_time) FROM $S.run_transition sp WHERE sp.run_number=r.run_number AND sp.type_id=1) AS stop_time,
    (SELECT lt.type_id FROM $S.run_transition lt WHERE lt.run_number=r.run_number ORDER BY lt.transition_time DESC LIMIT 1) AS latest_type,
    rei.comment AS end_comment
  FROM $S.run r
  JOIN $S.run_transition st ON st.run_number=r.run_number AND st.type_id=5
  LEFT JOIN $S.run_type rt ON rt.id=r.run_type_id
  LEFT JOIN $S.run_end_info rei ON rei.run_number=r.run_number
  WHERE st.transition_time $WIN
)
SELECT runs.run_number, runs.start_time, runs.run_type_name,
  COALESCE(gw.artdaq_partition,''), COALESCE(gw.host_name,''), COALESCE(gw.config_alias,''),
  COALESCE(runs.comment,''), runs.start_time, runs.stop_time, runs.latest_type,
  COALESCE(sg.subsystem_groups,''), COALESCE(runs.end_comment,'')
FROM runs
LEFT JOIN LATERAL (
  SELECT c.settings->'env'->>'HOSTNAME' AS host_name, c.settings->'env'->>'ARTDAQ_PARTITION' AS artdaq_partition,
         c.settings->>'config_alias' AS config_alias
  FROM $S.config c WHERE c.run_number=runs.run_number AND c.subsystem='Gateway'
  ORDER BY c.create_time DESC LIMIT 1) gw ON true
LEFT JOIN LATERAL (
  SELECT string_agg(c.subsystem
    || '|' || COALESCE(x.g->>'Configuration_alias','')
    || '|' || COALESCE(x.g->>'Configuration_group_name','')
    || '|' || COALESCE(x.g->>'Configuration_group_key','')
    || '|' || COALESCE(x.g->>'Context_group_name','')
    || '|' || COALESCE(x.g->>'Context_group_key','')
    || '|' || COALESCE(x.g->>'Backbone_group_name','')
    || '|' || COALESCE(x.g->>'Backbone_group_key',''), ';' ORDER BY c.subsystem) AS subsystem_groups
  FROM $S.config c CROSS JOIN LATERAL (SELECT c.settings #> '{config,groups}' AS g) x
  WHERE c.run_number=runs.run_number) sg ON true
ORDER BY runs.run_number DESC;"

echo
echo "=== C. NEW without any config/jsonb access (lower bound) ==="
$PSQL -c "EXPLAIN (ANALYZE, BUFFERS, TIMING, SUMMARY)
  SELECT r.run_number, r.comment, rt.name, st.transition_time,
    (SELECT min(sp.transition_time) FROM $S.run_transition sp WHERE sp.run_number=r.run_number AND sp.type_id=1),
    (SELECT lt.type_id FROM $S.run_transition lt WHERE lt.run_number=r.run_number ORDER BY lt.transition_time DESC LIMIT 1),
    rei.comment
  FROM $S.run r
  JOIN $S.run_transition st ON st.run_number=r.run_number AND st.type_id=5
  LEFT JOIN $S.run_type rt ON rt.id=r.run_type_id
  LEFT JOIN $S.run_end_info rei ON rei.run_number=r.run_number
  WHERE st.transition_time $WIN ORDER BY r.run_number DESC;"

echo
echo "=== D. jsonb groups extraction only, for the window's config rows ==="
$PSQL -c "EXPLAIN (ANALYZE, BUFFERS, TIMING, SUMMARY)
  SELECT c.run_number, c.subsystem, c.settings #> '{config,groups}'
  FROM $S.config c
  WHERE c.run_number IN (SELECT run_number FROM $S.run_transition WHERE type_id=5 AND transition_time $WIN);"

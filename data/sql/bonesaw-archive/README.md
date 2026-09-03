# Superseded Bonesaw SQL

Tracked for provenance, deliberately outside the updater's path. The updater
reads `data/sql/base/db_*`, `data/sql/updates/db_*`,
`data/sql/updates/pending_db_*` and `data/sql/custom/db_*`; this directory is
none of those, so nothing here is ever applied.

## ap_full_schema.sql

Creates the 21 original attunement tables. It was applied to the live realm,
and is recorded in `acore_characters.updates`, but its effects have since been
superseded: the attunement system was retired and its tables renamed to
`z_archive_ap_*`. Live now has six `ap_*` tables and thirteen `z_archive_ap_*`
ones.

It was briefly moved into `data/sql/custom/db_world` during the fork migration,
on the reasoning that a clone was missing it. That was wrong: applying it to a
fresh database recreated all 17 retired tables, giving 191 tables against
live's 174. The current schema for everything still in use is captured by
`data/sql/base/db_characters/zzz_bonesaw_base.sql` instead.

Kept because it is the only surviving description of what the attunement system
looked like before it was archived.

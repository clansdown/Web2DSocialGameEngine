# Table Documentation

This directory contains documentation for all SQL tables used in the Ravenest server.

## Adding New Table Documentation

When creating a new table, follow this template for its `.md` file:

```markdown
# table_name Table

[Brief description of what this table stores and its purpose]

## Schema

```sql
CREATE TABLE table_name (
    field_name data_type constraints,
    ...
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY... | ... |

## Indexes

[Any indexes defined]

## Relationships

[Foreign keys and table relationships]

## Notes

[Any implementation notes, TODOs, or usage guidelines]
```

## File Naming Convention

- Use lowercase with underscores: `table_name.md`
- Match the exact SQL table name from database schema
# users Table

Stores user account information and authentication credentials.

## Schema

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    adult INTEGER NOT NULL DEFAULT 0
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique user identifier |
| username | TEXT | UNIQUE NOT NULL | Login username (must be unique) |
| password_hash | TEXT | NOT NULL | Hashed password (yescrypt) |
| created_at | INTEGER | NOT NULL | Unix timestamp of account creation |
| adult | INTEGER | NOT NULL DEFAULT 0 | Whether account is for adults (1=true, 0=false) |

## Indexes

- Unique index on `username`

## Relationships

- One-to-many with `characters` via `user_id` foreign key

## Notes

- Password hashing using glibc `crypt()` with yescrypt prefix `$y$`
- `adult` flag controls whether custom display names can be set on characters
- Accessed by `/api/login`, `/api/createAccount`, `/api/updateUserProfile` endpoints
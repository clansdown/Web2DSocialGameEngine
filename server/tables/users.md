# users Table

Stores user account information and authentication credentials.

## Schema

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    adult INTEGER NOT NULL DEFAULT 0,
    displayName TEXT,
    safeDisplayName TEXT NOT NULL
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
| displayName | TEXT | NULLABLE | Custom display name (only set if adult=true) |
| safeDisplayName | TEXT | NOT NULL | Generated unique safe name from word1+word2 |

## Indexes

- Unique index on `username`
- Unique index on `safeDisplayName` (implied by NOT NULL and application logic)

## Relationships

- One-to-many with `players` via `user_id` foreign key

## Notes

- Password hashing using glibc `crypt()` with yescrypt prefix `$y$`
- `displayName` can only be set when `adult=1`, otherwise cleared
- `safeDisplayName` generated from two safe words (word1 + word2)
- Safe words loaded from `config/safe_words_1.txt` and `config/safe_words_2.txt`
- If duplicate `safeDisplayName` exists, number appended (e.g., "CloudDragon2")
- Accessed by `/api/login`, `/api/createAccount`, `/api/updateProfile` endpoints
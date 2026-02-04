# users Table

Stores user account information and authentication credentials.

## Schema

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique user identifier |
| username | TEXT | UNIQUE NOT NULL | Login username (must be unique) |
| password_hash | TEXT | NOT NULL | Hashed password (bcrypt/Argon2) |
| created_at | INTEGER | NOT NULL | Unix timestamp of account creation |

## Indexes

- Unique index on `username`

## Relationships

- One-to-many with `players` via `user_id` foreign key

## Notes

- Password hashing implementation pending (bcrypt or Argon2)
- Accessed by `/api/login` endpoint
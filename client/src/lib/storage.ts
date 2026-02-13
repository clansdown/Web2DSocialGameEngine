/// client/src/lib/storage.ts

type ConfigValue = any;

/**
 * Gets the root directory handle of the Origin Private File System (OPFS).
 * OPFS provides persistent storage accessible via File System Access API.
 * 
 * @param none
 * @returns Promise<FileSystemDirectoryHandle> - Root directory handle
 * @throws Error if OPFS is not supported in the browser
 * 
 * Usage: Called internally by all other storage functions
 */
export async function getRoot(): Promise<FileSystemDirectoryHandle> {
    if (!('storage' in navigator && 'getDirectory' in (navigator.storage as any))) {
        throw new Error('OPFS not supported in this browser');
    }
    return await (navigator.storage as any).getDirectory();
}

/**
 * Reads the contents of a file from OPFS as a string.
 * Path is relative to OPFS root (e.g., 'config/settings.json').
 * 
 * @param path - File path relative to OPFS root
 * @returns Promise<string | null> - File contents or null if not found
 * @throws Error if path is invalid
 * 
 * Usage: Used to read configuration files and saved data
 */
export async function readFile(path: string): Promise<string | null> {
    const root = await getRoot();
    const parts = path.split('/').filter((p) => p);
    
    if (parts.length === 0) {
        throw new Error('Invalid path');
    }
    
    const fileName = parts.pop()!;
    let currentDir = root;
    
    for (const part of parts) {
        currentDir = await currentDir.getDirectoryHandle(part);
    }
    
    try {
        const fileHandle = await currentDir.getFileHandle(fileName);
        const file = await fileHandle.getFile();
        return await file.text();
    } catch {
        return null;
    }
}

/**
 * Writes string content to a file in OPFS.
 * Creates parent directories if they don't exist.
 * Overwrites existing file if present.
 * 
 * @param path - File path relative to OPFS root
 * @param content - String content to write to file
 * @returns Promise<void>
 * @throws Error if path is invalid
 * 
 * Usage: Used to save configuration files and persistent data
 */
export async function writeFile(path: string, content: string): Promise<void> {
    const root = await getRoot();
    const parts = path.split('/').filter((p) => p);
    
    if (parts.length === 0) {
        throw new Error('Invalid path');
    }
    
    const fileName = parts.pop()!;
    let currentDir = root;
    
    for (const part of parts) {
        currentDir = await currentDir.getDirectoryHandle(part, { create: true });
    }
    
    const fileHandle = await currentDir.getFileHandle(fileName, { create: true });
    const writable = await fileHandle.createWritable();
    await writable.write(content);
    await writable.close();
}

/**
 * Deletes a file from OPFS.
 * 
 * @param path - File path relative to OPFS root
 * @returns Promise<void>
 * @throws Error if path is invalid or file doesn't exist
 * 
 * Usage: Used to remove saved configuration or data files
 */
export async function deleteFile(path: string): Promise<void> {
    const root = await getRoot();
    const parts = path.split('/').filter((p) => p);
    
    if (parts.length === 0) {
        throw new Error('Invalid path');
    }
    
    const fileName = parts.pop()!;
    let currentDir = root;
    
    for (const part of parts) {
        currentDir = await currentDir.getDirectoryHandle(part);
    }
    
    await currentDir.removeEntry(fileName);
}

/**
 * Ensures a directory exists in OPFS, creating it if necessary.
 * Returns handle to the directory for further operations.
 * 
 * @param path - Directory path relative to OPFS root
 * @returns Promise<FileSystemDirectoryHandle> - Handle to the directory
 * @throws Error if path is invalid
 * 
 * Usage: Used before writing files to ensure parent directory exists
 */
export async function ensureDirectory(path: string): Promise<FileSystemDirectoryHandle> {
    const root = await getRoot();
    const parts = path.split('/').filter((p) => p);
    
    if (parts.length === 0) {
        throw new Error('Invalid path');
    }
    
    let currentDir = root;
    for (const part of parts) {
        currentDir = await currentDir.getDirectoryHandle(part, { create: true });
    }
    return currentDir;
}

/**
 * Lists all entries (files and directories) in a directory.
 * 
 * @param path - Directory path relative to OPFS root (empty for root)
 * @returns Promise<string[]> - Array of entry names in the directory
 * @throws Error if path is invalid or not a directory
 * 
 * Usage: Used to enumerate configuration files or saved games
 */
export async function listDirectory(path: string): Promise<string[]> {
    const root = await getRoot();
    const parts = path.split('/').filter((p) => p);
    
    let currentDir = root;
    if (parts.length > 0) {
        for (const part of parts) {
            currentDir = await currentDir.getDirectoryHandle(part);
        }
    }
    
    const entries: string[] = [];
    for await (const entry of currentDir.values()) {
        entries.push(entry.name);
    }
    return entries;
}

/**
 * Deletes a directory and all its contents recursively.
 * 
 * @param path - Directory path relative to OPFS root
 * @returns Promise<void>
 * @throws Error if path is invalid or directory doesn't exist
 * 
 * Usage: Used to remove entire configuration folders or save games
 */
export async function deleteDirectory(path: string): Promise<void> {
    const root = await getRoot();
    const parts = path.split('/').filter((p) => p);
    
    if (parts.length === 0) {
        throw new Error('Invalid path');
    }
    
    const dirName = parts.pop()!;
    let currentDir = root;
    
    for (let i = 0; i < parts.length; i++) {
        currentDir = await currentDir.getDirectoryHandle(parts[i]);
    }
    
    await currentDir.removeEntry(dirName, { recursive: true });
}

/**
 * Reads a JSON configuration value from OPFS config directory.
 * Parses JSON content and returns typed value.
 * 
 * @param key - Configuration key (without .json extension)
 * @param defaultValue - Optional default value if config not found
 * @returns Promise<T | null> - Parsed config value or default/null
 * 
 * Usage: Primary method for reading application configuration
 */
export async function getConfig<T>(key: string, defaultValue?: T): Promise<T | null> {
    const path = `config/${key}.json`;
    const content = await readFile(path);
    if (content === null) {
        return defaultValue ?? null;
    }
    try {
        return JSON.parse(content) as T;
    } catch {
        return defaultValue ?? null;
    }
}

/**
 * Saves a JSON configuration value to OPFS config directory.
 * Serializes value as JSON and writes to config/{key}.json.
 * 
 * @param key - Configuration key (without .json extension)
 * @param value - Value to serialize as JSON and save
 * @returns Promise<void>
 * 
 * Usage: Primary method for saving application configuration
 */
export async function setConfig<T>(key: string, value: T): Promise<void> {
    const path = `config/${key}.json`;
    const content = JSON.stringify(value, null, 2);
    await writeFile(path, content);
}

/**
 * Deletes a configuration value from OPFS config directory.
 * 
 * @param key - Configuration key to delete (without .json extension)
 * @returns Promise<void>
 * 
 * Usage: Used to remove configuration values
 */
export async function deleteConfig(key: string): Promise<void> {
    const path = `config/${key}.json`;
    await deleteFile(path);
}

/**
 * Lists all configuration keys in the config directory.
 * Returns keys without .json extension.
 * 
 * @param none
 * @returns Promise<string[]> - Array of configuration key names
 * 
 * Usage: Used to enumerate available configurations
 */
export async function listConfigs(): Promise<string[]> {
    const configDir = await ensureDirectory('config');
    const files: string[] = [];
    for await (const entry of configDir.values()) {
        if (entry.name.endsWith('.json')) {
            files.push(entry.name.replace('.json', ''));
        }
    }
    return files;
}

/**
 * Clears all configuration values by deleting and recreating config directory.
 * 
 * @param none
 * @returns Promise<void>
 * 
 * Usage: Used to reset all configuration to defaults
 */
export async function clearConfigs(): Promise<void> {
    try {
        await deleteDirectory('config');
    } catch {
        await ensureDirectory('config');
    }
}

/**
 * Reads a numeric configuration value with type safety.
 * Returns default if value is missing or not a number.
 * 
 * @param key - Configuration key (without .json extension)
 * @param defaultValue - Default value if config missing or invalid
 * @returns Promise<number> - Numeric config value or default
 * 
 * Usage: Convenience function for reading number configs
 */
export async function getConfigNumber(key: string, defaultValue: number): Promise<number> {
    const result = await getConfig<number>(key);
    return typeof result === 'number' ? result : defaultValue;
}

/**
 * Reads a string configuration value with type safety.
 * Returns default if value is missing or not a string.
 * 
 * @param key - Configuration key (without .json extension)
 * @param defaultValue - Default value if config missing or invalid
 * @returns Promise<string> - String config value or default
 * 
 * Usage: Convenience function for reading string configs
 */
export async function getConfigString(key: string, defaultValue: string): Promise<string> {
    const result = await getConfig<string>(key);
    return typeof result === 'string' ? result : defaultValue;
}

/**
 * Reads a boolean configuration value with type safety.
 * Returns default if value is missing or not a boolean.
 * 
 * @param key - Configuration key (without .json extension)
 * @param defaultValue - Default value if config missing or invalid
 * @returns Promise<boolean> - Boolean config value or default
 * 
 * Usage: Convenience function for reading boolean configs
 */
export async function getConfigBoolean(key: string, defaultValue: boolean): Promise<boolean> {
    const result = await getConfig<boolean>(key);
    return typeof result === 'boolean' ? result : defaultValue;
}

/// client/src/lib/storage.ts

type ConfigValue = any;

export async function getRoot(): Promise<FileSystemDirectoryHandle> {
    if (!('storage' in navigator && 'getDirectory' in (navigator.storage as any))) {
        throw new Error('OPFS not supported in this browser');
    }
    return await (navigator.storage as any).getDirectory();
}

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

export async function setConfig<T>(key: string, value: T): Promise<void> {
    const path = `config/${key}.json`;
    const content = JSON.stringify(value, null, 2);
    await writeFile(path, content);
}

export async function deleteConfig(key: string): Promise<void> {
    const path = `config/${key}.json`;
    await deleteFile(path);
}

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

export async function clearConfigs(): Promise<void> {
    try {
        await deleteDirectory('config');
    } catch {
        await ensureDirectory('config');
    }
}

export async function getConfigNumber(key: string, defaultValue: number): Promise<number> {
    const result = await getConfig<number>(key);
    return typeof result === 'number' ? result : defaultValue;
}

export async function getConfigString(key: string, defaultValue: string): Promise<string> {
    const result = await getConfig<string>(key);
    return typeof result === 'string' ? result : defaultValue;
}

export async function getConfigBoolean(key: string, defaultValue: boolean): Promise<boolean> {
    const result = await getConfig<boolean>(key);
    return typeof result === 'boolean' ? result : defaultValue;
}

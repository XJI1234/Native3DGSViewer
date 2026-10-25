export interface SyncFile {
    write(data: ArrayBufferView, options: { at: number }): number;
    read(data: ArrayBufferView, options: { at: number }): number;
    truncate(size: number): void;
    flush(): void;
    close(): void;
    getSize(): number;
}
export async function openFile(directory: FileSystemDirectoryHandle, name: string) {
    const file = await directory.getFileHandle(name, { create: true });
    if (!('createSyncAccessHandle' in file))
        throw Error('UnsupportedCapability: synchronous OPFS worker access');
    const access = await (
        file as unknown as { createSyncAccessHandle(): Promise<SyncFile> }
    ).createSyncAccessHandle();
    return { file, access };
}
export function write(access: SyncFile, data: Uint8Array, at: number) {
    let offset = 0;
    while (offset < data.length) {
        const n = access.write(data.subarray(offset), { at: at + offset });
        if (!n) throw Error('ResourceLimit: storage write stalled');
        offset += n;
    }
}
